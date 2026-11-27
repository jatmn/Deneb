// SPDX-License-Identifier: MPL-2.0
#define main deneb_mdns_main
#include "../src/mdns_advertiser.c"
#undef main

#include <assert.h>

#define FIXTURE_SOCKET 9

static const uint8_t cura_query[] = {
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    10, '_', 'u', 'l', 't', 'i', 'm', 'a', 'k', 'e', 'r',
    4, '_', 't', 'c', 'p', 5, 'l', 'o', 'c', 'a', 'l', 0, 0, 12, 0, 1,
};
static const uint64_t query_times[] = {1250, 1300, 11250, 16250, 22250};
static uint64_t now;
static size_t query_index;
static bool pending_query;
static int route_reads, address_reads, builds, interface_sets, joins, drops;
static int joined;
static struct in_addr outgoing;
static packet_t announced;

static const struct {
    uint64_t when;
    const char *address;
    uint16_t port;
} expected_sends[] = {
    {1000, "192.0.2.1", 5353},
    {1250, "192.0.2.1", 50321},
    {11000, "198.51.100.1", 5353},
    {11250, "198.51.100.1", 5353},
    {16000, "198.51.100.8", 5353},
    {16250, "198.51.100.8", 50321},
    {22000, "192.0.2.9", 5353},
    {22250, "192.0.2.9", 50321},
    {52000, "192.0.2.9", 5353},
};
static size_t sent;

FILE *__wrap_popen(const char *command, const char *mode)
{
    (void)command;
    (void)mode;
    return NULL;
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
    static char hostname[] = "Fixture\n";
    static char mac[] = "00:11:22:33:44:55\n";
    static char eth_route[] = "Iface Destination Gateway Flags\neth0 00000000 00000000 0001\n";
    static char wifi_route[] = "Iface Destination Gateway Flags\nwlan0 00000000 00000000 0001\n";

    if (strcmp(path, "/proc/net/route") == 0) {
        route_reads++;
        return now < 11000 ? fmemopen(eth_route, sizeof(eth_route) - 1, mode) :
                             fmemopen(wifi_route, sizeof(wifi_route) - 1, mode);
    }
    if (strcmp(path, "/sys/class/net/eth0/address") == 0)
        return fmemopen(mac, sizeof(mac) - 1, mode);
    assert(strcmp(path, "/proc/sys/kernel/hostname") == 0);
    return fmemopen(hostname, sizeof(hostname) - 1, mode);
}

int __wrap_getifaddrs(struct ifaddrs **result)
{
    static struct sockaddr_in eth, wifi;
    static struct ifaddrs interfaces[2];
    address_reads++;
    *result = NULL;
    if (now < 1000 || (now >= 21000 && now < 22000))
        return 0;
    eth.sin_family = wifi.sin_family = AF_INET;
    eth.sin_addr.s_addr = inet_addr(now < 22000 ? "192.0.2.1" : "192.0.2.9");
    wifi.sin_addr.s_addr = inet_addr(now < 16000 ? "198.51.100.1" : "198.51.100.8");
    interfaces[1] = (struct ifaddrs){
        .ifa_name = "wlan0", .ifa_flags = IFF_UP, .ifa_addr = (struct sockaddr *)&wifi,
    };
    interfaces[0] = (struct ifaddrs){
        .ifa_next = now >= 11000 && now < 21000 ? &interfaces[1] : NULL,
        .ifa_name = "eth0", .ifa_flags = IFF_UP, .ifa_addr = (struct sockaddr *)&eth,
    };
    *result = &interfaces[0];
    return 0;
}

void __wrap_freeifaddrs(struct ifaddrs *list) { (void)list; }

unsigned int __wrap_if_nametoindex(const char *name)
{
    return strcmp(name, "eth0") == 0 ? 2 : 3;
}

const char *__real_inet_ntop(int family, const void *addr, char *text, socklen_t size);
const char *__wrap_inet_ntop(int family, const void *addr, char *text, socklen_t size)
{
    builds++;
    return __real_inet_ntop(family, addr, text, size);
}

int __wrap_socket(int domain, int type, int protocol)
{
    assert(domain == AF_INET && type == SOCK_DGRAM && protocol == 0);
    return FIXTURE_SOCKET;
}

int __wrap_bind(int fd, const struct sockaddr *addr, socklen_t size)
{
    assert(fd == FIXTURE_SOCKET && size == sizeof(struct sockaddr_in));
    assert(ntohs(((const struct sockaddr_in *)addr)->sin_port) == 5353);
    return 0;
}

int __wrap_fcntl(int fd, int command, ...)
{
    assert(fd == FIXTURE_SOCKET && (command == F_GETFL || command == F_SETFL));
    return 0;
}

int __wrap_close(int fd)
{
    assert(fd == FIXTURE_SOCKET);
    return 0;
}

int __wrap_setsockopt(int fd, int level, int option, const void *value, socklen_t size)
{
    assert(fd == FIXTURE_SOCKET);
    if (level != IPPROTO_IP ||
        (option != IP_ADD_MEMBERSHIP && option != IP_DROP_MEMBERSHIP && option != IP_MULTICAST_IF))
        return 0;

    assert(size == sizeof(struct ip_mreqn));
    const struct ip_mreqn *request = value;
    if (option == IP_ADD_MEMBERSHIP) {
        assert(!joined && request->imr_multiaddr.s_addr == inet_addr("224.0.0.251"));
        assert(request->imr_ifindex == (now >= 11000 && now < 21000 ? 3 : 2));
        joined = request->imr_ifindex;
        joins++;
    } else if (option == IP_DROP_MEMBERSHIP) {
        assert(joined == request->imr_ifindex);
        joined = 0;
        drops++;
    } else {
        assert(joined == request->imr_ifindex && joined > 0);
        outgoing = request->imr_address;
        interface_sets++;
    }
    return 0;
}

int __wrap_clock_gettime(clockid_t clock_id, struct timespec *time)
{
    assert(clock_id == CLOCK_MONOTONIC);
    *time = (struct timespec){now / 1000, (now % 1000) * 1000000};
    return 0;
}

int __wrap_select(int nfds, fd_set *readfds, fd_set *writefds,
                  fd_set *exceptfds, struct timeval *timeout)
{
    assert(writefds == NULL && exceptfds == NULL);
    assert(nfds == (joined ? FIXTURE_SOCKET + 1 : 0));
    uint64_t deadline = now + timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
    assert(deadline > now && deadline - now <= (joined ? 5000 : 1000));
    if (query_index < sizeof(query_times) / sizeof(query_times[0]) &&
        query_times[query_index] <= deadline) {
        assert(readfds && FD_ISSET(FIXTURE_SOCKET, readfds));
        now = query_times[query_index];
        pending_query = true;
        return 1;
    }
    now = deadline;
    if (now >= 54000)
        on_signal(SIGTERM);
    return 0;
}

ssize_t __wrap_recvfrom(int fd, void *data, size_t size, int flags,
                        struct sockaddr *src, socklen_t *srclen)
{
    assert(fd == FIXTURE_SOCKET && size >= sizeof(cura_query) && flags == 0);
    if (!pending_query)
        return -1;

    struct sockaddr_in *address = (struct sockaddr_in *)src;
    *address = (struct sockaddr_in){.sin_family = AF_INET,
        .sin_port = htons(now == 11250 ? 5353 : 50321),
        .sin_addr.s_addr = inet_addr("192.0.2.2")};
    *srclen = sizeof(*address);
    if (now == 1300) {
        memcpy(data, announced.data, announced.len);  /* A response receives no reply. */
        size = announced.len;
    } else {
        memcpy(data, cura_query, sizeof(cura_query));
        size = sizeof(cura_query);
    }
    pending_query = false;
    query_index++;
    return (ssize_t)size;
}

ssize_t __wrap_sendto(int fd, const void *data, size_t size, int flags,
                      const struct sockaddr *dst, socklen_t dstlen)
{
    assert(fd == FIXTURE_SOCKET && flags == 0 && dstlen == sizeof(struct sockaddr_in));
    assert(sent < sizeof(expected_sends) / sizeof(expected_sends[0]));
    const struct sockaddr_in *address = (const struct sockaddr_in *)dst;
    assert(now == expected_sends[sent].when && ntohs(address->sin_port) == expected_sends[sent].port);
    assert(address->sin_addr.s_addr == inet_addr(ntohs(address->sin_port) == 5353 ?
                                                 "224.0.0.251" : "192.0.2.2"));
    assert(outgoing.s_addr == inet_addr(expected_sends[sent].address));
    assert(size > 12 && size <= sizeof(announced.data));
    const uint8_t *bytes = data;
    assert(bytes[2] == 0x84 && bytes[7] == 4);
    assert(memcmp(bytes + size - 4, &outgoing.s_addr, 4) == 0);
    if (now % 1000 == 0) {
        memcpy(announced.data, data, size);
        announced.len = size;
    } else {
        assert(size == announced.len && memcmp(data, announced.data, size) == 0);
    }
    sent++;
    return (ssize_t)size;
}

int main(void)
{
    assert(deneb_mdns_main() == 0);
    assert(sent == sizeof(expected_sends) / sizeof(expected_sends[0]));
    assert(route_reads == 13 && address_reads == route_reads);
    assert(builds == 4 && interface_sets == builds && joins == 3 && drops == 2);
    return 0;
}
