// SPDX-License-Identifier: MPL-2.0
/*
 * Tiny mDNS advertiser for Cura local printer discovery.
 *
 * Cura's UM3NetworkPrinting plugin browses _ultimaker._tcp.local. and only
 * accepts TXT records with type=printer. Keep this service narrow: advertise
 * Deneb's existing HTTP endpoint on port 80 and avoid a general mDNS daemon.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MDNS_ADDR "224.0.0.251"
#define MDNS_PORT 5353
#define DENEB_HTTP_PORT 80
#define DNS_CLASS_IN 1
#define DNS_CLASS_FLUSH 0x8001
#define DNS_TYPE_A 1
#define DNS_TYPE_PTR 12
#define DNS_TYPE_TXT 16
#define DNS_TYPE_SRV 33
#define TTL_SECONDS 120
#define ROUTE_FLAG_UP 0x1
#define DENEB_DEFAULT_CURA_MACHINE "deneb_um2c"
#define DENEB_DEFAULT_CURA_CONNECT_VERSION "4.0.0"

static volatile sig_atomic_t running = 1;

typedef struct {
    uint8_t data[1500];
    size_t len;
} packet_t;

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static const char *env_or_default(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return (value && value[0]) ? value : fallback;
}

static void sanitize_label(char *value)
{
    for (char *p = value; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') ||
            (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '-') {
            continue;
        }
        *p = '-';
    }
}

static int read_first_line(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    if (!fgets(buf, (int)size, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[strcspn(buf, "\r\n")] = '\0';
    return buf[0] ? 0 : -1;
}

static int read_command_line(const char *command, char *buf, size_t size)
{
    FILE *f = popen(command, "r");
    if (!f)
        return -1;
    if (!fgets(buf, (int)size, f)) {
        pclose(f);
        return -1;
    }
    pclose(f);
    buf[strcspn(buf, "\r\n")] = '\0';
    return buf[0] ? 0 : -1;
}

static void get_printer_name(char *buf, size_t size)
{
    char name[96] = "";

    if (read_command_line("uci -q get ultimaker.option.printer_name 2>/dev/null",
                          name, sizeof(name)) < 0 &&
        read_command_line("uci -q get system.@system[0].hostname 2>/dev/null",
                          name, sizeof(name)) < 0 &&
        read_first_line("/proc/sys/kernel/hostname", name, sizeof(name)) < 0 &&
        read_first_line("/etc/hostname", name, sizeof(name)) < 0) {
        snprintf(name, sizeof(name), "Deneb");
    }

    if (strstr(name, "Deneb UM2C"))
        snprintf(buf, size, "%s", name);
    else
        snprintf(buf, size, "%s (Deneb UM2C)", name);
}

static void get_instance_id(char *buf, size_t size)
{
    char mac[64] = "";
    if (read_first_line("/sys/class/net/eth0/address", mac, sizeof(mac)) < 0)
        (void)read_first_line("/sys/class/net/wlan0/address", mac, sizeof(mac));

    if (mac[0]) {
        char compact[32] = "";
        size_t out = 0;
        for (size_t i = 0; mac[i] && out < sizeof(compact) - 1; i++) {
            if (mac[i] == ':')
                continue;
            compact[out++] = mac[i];
        }
        compact[out] = '\0';
        snprintf(buf, size, "ultimakersystem-%s", compact);
    } else {
        snprintf(buf, size, "ultimakersystem-deneb");
    }
    sanitize_label(buf);
}

static int get_default_interface(char *buf, size_t size)
{
    FILE *f = fopen("/proc/net/route", "r");
    if (!f)
        return -1;

    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }

    int rc = -1;
    while (fgets(line, sizeof(line), f)) {
        char iface[IFNAMSIZ] = "";
        char destination[16] = "";
        unsigned int flags = 0;
        if (sscanf(line, "%15s %15s %*s %x", iface, destination, &flags) < 3)
            continue;
        if (strcmp(destination, "00000000") == 0 && (flags & ROUTE_FLAG_UP)) {
            snprintf(buf, size, "%s", iface);
            rc = 0;
            break;
        }
    }

    fclose(f);
    return rc;
}

static int find_ipv4_on_interface(const char *preferred_ifname, struct in_addr *addr)
{
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) < 0)
        return -1;

    int rc = -1;
    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK))
            continue;
        if (preferred_ifname && strcmp(ifa->ifa_name, preferred_ifname) != 0)
            continue;

        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        *addr = sin->sin_addr;
        rc = 0;
        break;
    }

    freeifaddrs(ifaddr);
    return rc;
}

static int get_primary_ipv4(struct in_addr *addr)
{
    char ifname[IFNAMSIZ] = "";
    if (get_default_interface(ifname, sizeof(ifname)) == 0 &&
        find_ipv4_on_interface(ifname, addr) == 0)
        return 0;

    return find_ipv4_on_interface(NULL, addr);
}

static int put_u8(packet_t *p, uint8_t v)
{
    if (p->len + 1 > sizeof(p->data))
        return -1;
    p->data[p->len++] = v;
    return 0;
}

static int put_u16(packet_t *p, uint16_t v)
{
    if (p->len + 2 > sizeof(p->data))
        return -1;
    p->data[p->len++] = (uint8_t)(v >> 8);
    p->data[p->len++] = (uint8_t)(v & 0xff);
    return 0;
}

static int put_u32(packet_t *p, uint32_t v)
{
    if (put_u16(p, (uint16_t)(v >> 16)) < 0)
        return -1;
    return put_u16(p, (uint16_t)(v & 0xffff));
}

static int put_bytes(packet_t *p, const void *data, size_t len)
{
    if (p->len + len > sizeof(p->data))
        return -1;
    memcpy(p->data + p->len, data, len);
    p->len += len;
    return 0;
}

static int put_name(packet_t *p, const char *name)
{
    const char *start = name;
    while (*start) {
        const char *dot = strchr(start, '.');
        size_t len = dot ? (size_t)(dot - start) : strlen(start);
        if (len > 63)
            return -1;
        if (put_u8(p, (uint8_t)len) < 0 || put_bytes(p, start, len) < 0)
            return -1;
        if (!dot)
            break;
        start = dot + 1;
    }
    return put_u8(p, 0);
}

static int put_record_header(packet_t *p, const char *name, uint16_t type,
                             uint16_t klass, uint32_t ttl, size_t *rdlen_pos)
{
    if (put_name(p, name) < 0 || put_u16(p, type) < 0 ||
        put_u16(p, klass) < 0 || put_u32(p, ttl) < 0)
        return -1;
    *rdlen_pos = p->len;
    return put_u16(p, 0);
}

static int finish_record(packet_t *p, size_t rdlen_pos, size_t rdata_start)
{
    size_t rdlen = p->len - rdata_start;
    if (rdlen > 0xffff)
        return -1;
    p->data[rdlen_pos] = (uint8_t)(rdlen >> 8);
    p->data[rdlen_pos + 1] = (uint8_t)(rdlen & 0xff);
    return 0;
}

static int put_txt_item(packet_t *p, const char *key, const char *value)
{
    char item[255];
    int len = snprintf(item, sizeof(item), "%s=%s", key, value ? value : "");
    if (len < 0 || len > 255)
        return -1;
    if (put_u8(p, (uint8_t)len) < 0)
        return -1;
    return put_bytes(p, item, (size_t)len);
}

static int build_response(packet_t *p, const struct in_addr *addr,
                          const char *instance_id, const char *printer_name,
                          const char *machine, const char *firmware)
{
    char service[] = "_ultimaker._tcp.local";
    char host[128];
    char instance[192];
    char ip[INET_ADDRSTRLEN] = "";
    inet_ntop(AF_INET, addr, ip, sizeof(ip));

    snprintf(host, sizeof(host), "%s.local", instance_id);
    snprintf(instance, sizeof(instance), "%s.%s", instance_id, service);

    memset(p, 0, sizeof(*p));
    if (put_u16(p, 0) < 0 || put_u16(p, 0x8400) < 0 ||
        put_u16(p, 0) < 0 || put_u16(p, 4) < 0 ||
        put_u16(p, 0) < 0 || put_u16(p, 0) < 0)
        return -1;

    size_t rdlen_pos, start;

    if (put_record_header(p, service, DNS_TYPE_PTR, DNS_CLASS_IN, TTL_SECONDS, &rdlen_pos) < 0)
        return -1;
    start = p->len;
    if (put_name(p, instance) < 0 || finish_record(p, rdlen_pos, start) < 0)
        return -1;

    if (put_record_header(p, instance, DNS_TYPE_SRV, DNS_CLASS_FLUSH, TTL_SECONDS, &rdlen_pos) < 0)
        return -1;
    start = p->len;
    if (put_u16(p, 0) < 0 || put_u16(p, 0) < 0 || put_u16(p, DENEB_HTTP_PORT) < 0 ||
        put_name(p, host) < 0 || finish_record(p, rdlen_pos, start) < 0)
        return -1;

    if (put_record_header(p, instance, DNS_TYPE_TXT, DNS_CLASS_FLUSH, TTL_SECONDS, &rdlen_pos) < 0)
        return -1;
    start = p->len;
    if (put_txt_item(p, "type", "printer") < 0 ||
        put_txt_item(p, "name", printer_name) < 0 ||
        put_txt_item(p, "address", ip) < 0 ||
        put_txt_item(p, "machine", machine) < 0 ||
        put_txt_item(p, "firmware_version", firmware) < 0 ||
        put_txt_item(p, "cluster_size", "1") < 0 ||
        finish_record(p, rdlen_pos, start) < 0)
        return -1;

    if (put_record_header(p, host, DNS_TYPE_A, DNS_CLASS_FLUSH, TTL_SECONDS, &rdlen_pos) < 0)
        return -1;
    start = p->len;
    if (put_bytes(p, &addr->s_addr, 4) < 0 || finish_record(p, rdlen_pos, start) < 0)
        return -1;

    return 0;
}

static int open_mdns_socket(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;

    int yes = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    unsigned char ttl = 255;
    (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    unsigned char loop = 0;
    (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(MDNS_PORT);
    if (bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        close(fd);
        return -1;
    }

    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(MDNS_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    (void)setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return fd;
}

static void set_multicast_interface(int fd, const struct in_addr *addr)
{
    (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, addr, sizeof(*addr));
}

static void send_multicast_packet(int fd, const packet_t *packet)
{
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(MDNS_PORT);
    dst.sin_addr.s_addr = inet_addr(MDNS_ADDR);
    (void)sendto(fd, packet->data, packet->len, 0, (struct sockaddr *)&dst, sizeof(dst));
}

static void send_unicast_packet(int fd, const packet_t *packet, const struct sockaddr_in *dst)
{
    (void)sendto(fd, packet->data, packet->len, 0, (const struct sockaddr *)dst, sizeof(*dst));
}

int main(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    char printer_name[128];
    char instance_id[96];
    get_printer_name(printer_name, sizeof(printer_name));
    get_instance_id(instance_id, sizeof(instance_id));

    const char *machine = env_or_default("DENEB_MDNS_MACHINE", DENEB_DEFAULT_CURA_MACHINE);
    const char *firmware = env_or_default("DENEB_MDNS_FIRMWARE", DENEB_DEFAULT_CURA_CONNECT_VERSION);

    int fd = open_mdns_socket();
    if (fd < 0) {
        perror("deneb-mdns: socket");
        return 1;
    }

    time_t next_announce = 0;
    while (running) {
        struct in_addr addr;
        if (get_primary_ipv4(&addr) == 0) {
            packet_t response;
            if (build_response(&response, &addr, instance_id, printer_name, machine, firmware) == 0) {
                time_t now = time(NULL);
                set_multicast_interface(fd, &addr);
                if (now >= next_announce) {
                    send_multicast_packet(fd, &response);
                    next_announce = now + 30;
                }

                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(fd, &rfds);
                struct timeval tv = {1, 0};
                int rc = select(fd + 1, &rfds, NULL, NULL, &tv);
                if (rc > 0 && FD_ISSET(fd, &rfds)) {
                    uint8_t buf[1500];
                    struct sockaddr_in src;
                    socklen_t src_len = sizeof(src);
                    while (1) {
                        src_len = sizeof(src);
                        ssize_t got = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&src, &src_len);
                        if (got <= 0)
                            break;
                        if (got >= 4 && !(buf[2] & 0x80)) {
                            if (ntohs(src.sin_port) == MDNS_PORT) {
                                send_multicast_packet(fd, &response);
                            } else {
                                send_unicast_packet(fd, &response, &src);
                            }
                        }
                    }
                }
                continue;
            }
        }
        sleep(1);
    }

    close(fd);
    return 0;
}
