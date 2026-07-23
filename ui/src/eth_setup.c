/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Ethernet setup via USB import.
 * Reads eth.txt from a USB drive and configures wired Ethernet via UCI.
 * No external library dependencies — uses stdio/stdlib/string only.
 */

#include "eth_setup.h"
#include "net_utils.h"
#include "locale.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Use shared helpers from net_utils. */
#define uci_escape        net_uci_escape
#define parse_config_line net_parse_key_value_line

/* ------------------------------------------------------------------ */
/* Find eth.txt on USB                                                 */
/* ------------------------------------------------------------------ */

eth_result_t eth_setup_find_config(char *path_out, int path_size)
{
    switch (net_find_usb_config("eth.txt", path_out, path_size)) {
    case NET_USB_CONFIG_FOUND:
        return ETH_OK;
    case NET_USB_CONFIG_NO_USB:
        return ETH_ERR_NO_USB;
    case NET_USB_CONFIG_NO_FILE:
    default:
        return ETH_ERR_NO_FILE;
    }
}

/* ------------------------------------------------------------------ */
/* Parse eth.txt                                                       */
/* ------------------------------------------------------------------ */

eth_result_t eth_setup_parse(const char *path, eth_config_t *cfg)
{
    FILE *fp;
    char line[ETH_SETUP_MAX_LINE];

    memset(cfg, 0, sizeof(*cfg));

    fp = fopen(path, "r");
    if (!fp)
        return ETH_ERR_NO_FILE;

    while (fgets(line, sizeof(line), fp)) {
        char *key;
        char *val;
        if (!parse_config_line(line, &key, &val))
            continue;

        if (strcmp(key, "ip") == 0 ||
            strcmp(key, "ip_address") == 0 ||
            strcmp(key, "ipaddress") == 0 ||
            strcmp(key, "static_ip") == 0) {
            strncpy(cfg->ip_address, val, sizeof(cfg->ip_address) - 1);
        } else if (strcmp(key, "netmask") == 0 ||
                   strcmp(key, "mask") == 0 ||
                   strcmp(key, "subnet") == 0) {
            strncpy(cfg->netmask, val, sizeof(cfg->netmask) - 1);
        } else if (strcmp(key, "gateway") == 0 ||
                   strcmp(key, "gw") == 0 ||
                   strcmp(key, "router") == 0) {
            strncpy(cfg->gateway, val, sizeof(cfg->gateway) - 1);
        } else if (strcmp(key, "dns") == 0 ||
                   strcmp(key, "nameserver") == 0 ||
                   strcmp(key, "dns_server") == 0) {
            strncpy(cfg->dns, val, sizeof(cfg->dns) - 1);
        } else if (strcmp(key, "ntp") == 0 ||
                   strcmp(key, "ntp_server") == 0 ||
                   strcmp(key, "ntpserver") == 0 ||
                   strcmp(key, "timeserver") == 0 ||
                   strcmp(key, "time_server") == 0) {
            strncpy(cfg->ntp_server, val, sizeof(cfg->ntp_server) - 1);
        } else if (strcmp(key, "hostname") == 0 ||
                   strcmp(key, "host") == 0 ||
                   strcmp(key, "name") == 0 ||
                   strcmp(key, "device_name") == 0 ||
                   strcmp(key, "printer_name") == 0) {
            strncpy(cfg->hostname, val, sizeof(cfg->hostname) - 1);
        }
        /* Unknown keys silently ignored */
    }

    fclose(fp);
    cfg->valid = true;
    return ETH_OK;
}

/* ------------------------------------------------------------------ */
/* Apply config via UCI                                                */
/* ------------------------------------------------------------------ */

eth_result_t eth_setup_apply(const eth_config_t *cfg)
{
    char cmd[512];
    int rc;

    if (!cfg || !cfg->valid)
        return ETH_ERR_UCI_FAIL;

    if (cfg->ip_address[0] != '\0') {
        /* Static IP configuration */
        char escaped_ip[ETH_SETUP_MAX_IP * 4 + 4];
        char escaped_netmask[ETH_SETUP_MAX_IP * 4 + 4];
        char escaped_gateway[ETH_SETUP_MAX_IP * 4 + 4];
        uci_escape(escaped_ip, sizeof(escaped_ip), cfg->ip_address);
        uci_escape(escaped_netmask, sizeof(escaped_netmask), cfg->netmask);
        uci_escape(escaped_gateway, sizeof(escaped_gateway), cfg->gateway);

        system("uci set network.wan.proto='static'");
        system("uci delete network.wan.gateway 2>/dev/null");
        system("uci delete network.wan.dns 2>/dev/null");

        snprintf(cmd, sizeof(cmd),
                 "uci set network.wan.ipaddr=%s", escaped_ip);
        system(cmd);

        if (cfg->netmask[0] != '\0') {
            snprintf(cmd, sizeof(cmd),
                     "uci set network.wan.netmask=%s", escaped_netmask);
            system(cmd);
        } else {
            system("uci set network.wan.netmask='255.255.255.0'");
        }

        if (cfg->gateway[0] != '\0') {
            snprintf(cmd, sizeof(cmd),
                     "uci set network.wan.gateway=%s", escaped_gateway);
            system(cmd);
        }

        if (cfg->dns[0] != '\0') {
            system("uci delete network.wan.dns 2>/dev/null");
            char dns_copy[ETH_SETUP_MAX_DNS];
            strncpy(dns_copy, cfg->dns, sizeof(dns_copy) - 1);
            dns_copy[sizeof(dns_copy) - 1] = '\0';
            char *tok = strtok(dns_copy, " ,;");
            while (tok) {
                char escaped_dns[ETH_SETUP_MAX_DNS * 4 + 4];
                uci_escape(escaped_dns, sizeof(escaped_dns), tok);
                snprintf(cmd, sizeof(cmd),
                         "uci add_list network.wan.dns=%s", escaped_dns);
                system(cmd);
                tok = strtok(NULL, " ,;");
            }
        }
    } else {
        /* DHCP (default) */
        system("uci set network.wan.proto='dhcp'");
        system("uci delete network.wan.ipaddr 2>/dev/null");
        system("uci delete network.wan.netmask 2>/dev/null");
        system("uci delete network.wan.gateway 2>/dev/null");
        system("uci delete network.wan.dns 2>/dev/null");
    }

    /* Set hostname */
    if (cfg->hostname[0] != '\0') {
        char escaped_host[ETH_SETUP_MAX_HOST * 4 + 4];
        uci_escape(escaped_host, sizeof(escaped_host), cfg->hostname);
        snprintf(cmd, sizeof(cmd),
                 "uci set network.wan.hostname=%s", escaped_host);
        system(cmd);
        snprintf(cmd, sizeof(cmd),
                 "uci set system.@system[0].hostname=%s", escaped_host);
        system(cmd);
    }

    /* Configure NTP server if specified */
    if (cfg->ntp_server[0] != '\0') {
        system("uci delete system.ntp.server 2>/dev/null");
        char ntp_copy[ETH_SETUP_MAX_NTP];
        strncpy(ntp_copy, cfg->ntp_server, sizeof(ntp_copy) - 1);
        ntp_copy[sizeof(ntp_copy) - 1] = '\0';
        char *tok = strtok(ntp_copy, " ,;");
        while (tok) {
            char escaped_ntp[ETH_SETUP_MAX_NTP * 4 + 4];
            uci_escape(escaped_ntp, sizeof(escaped_ntp), tok);
            snprintf(cmd, sizeof(cmd),
                     "uci add_list system.ntp.server=%s", escaped_ntp);
            system(cmd);
            tok = strtok(NULL, " ,;");
        }
    }

    /* Commit all changes */
    rc = system("uci commit");
    if (rc != 0)
        return ETH_ERR_UCI_FAIL;

    /* Restart networking to apply */
    rc = system("/etc/init.d/network restart >/dev/null 2>&1");
    if (rc != 0)
        return ETH_ERR_NET_FAIL;

    return ETH_OK;
}

/* ------------------------------------------------------------------ */
/* Full import flow                                                    */
/* ------------------------------------------------------------------ */

eth_result_t eth_setup_import(char *status_msg, int msg_size)
{
    char path[160];
    eth_config_t cfg;
    eth_result_t res;

    if (status_msg && msg_size > 0)
        status_msg[0] = '\0';

    res = eth_setup_find_config(path, sizeof(path));
    if (res != ETH_OK) {
        if (status_msg) {
            if (res == ETH_ERR_NO_USB)
                snprintf(status_msg, msg_size, "%s",
                         locale_get("network.no_usb"));
            else
                snprintf(status_msg, msg_size,
                         "%s", locale_get("network.no_eth_txt"));
        }
        return res;
    }

    res = eth_setup_parse(path, &cfg);
    if (res != ETH_OK) {
        if (status_msg)
            snprintf(status_msg, msg_size, "%s",
                     locale_get("network.eth_parse_error"));
        return res;
    }

    res = eth_setup_apply(&cfg);
    if (status_msg) {
        switch (res) {
        case ETH_OK:
            if (cfg.ip_address[0] != '\0')
                locale_format_s(status_msg, msg_size,
                                "network.eth_static_fmt", cfg.ip_address);
            else
                snprintf(status_msg, msg_size,
                         "%s", locale_get("network.eth_dhcp_set"));
            break;
        case ETH_ERR_UCI_FAIL:
            snprintf(status_msg, msg_size,
                     "%s", locale_get("network.eth_save_failed"));
            break;
        case ETH_ERR_NET_FAIL:
            snprintf(status_msg, msg_size,
                     "%s", locale_get("network.eth_restarting"));
            break;
        default:
            snprintf(status_msg, msg_size, "%s",
                     locale_get("network.eth_setup_failed"));
            break;
        }
    }
    return res;
}

/* ------------------------------------------------------------------ */
/* Status queries                                                      */
/* ------------------------------------------------------------------ */

bool eth_setup_is_static(void)
{
    char buf[32] = {0};
    net_read_command(buf, sizeof(buf),
                     "uci -q get network.wan.proto 2>/dev/null");
    return strcmp(buf, "static") == 0;
}

void eth_setup_get_status(char *buf, int size)
{
    if (!buf || size <= 0)
        return;

    buf[0] = '\0';

    /* Get IP on eth0 */
    char ip[64] = {0};
    net_read_command(ip, sizeof(ip),
                     "ip -4 addr show eth0 2>/dev/null | "
                     "grep -o 'inet [^ ]*' | cut -d' ' -f2 | cut -d/ -f1");

    char proto[32] = {0};
    net_read_command(proto, sizeof(proto),
                     "uci -q get network.wan.proto 2>/dev/null");

    if (ip[0] != '\0') {
        if (strcmp(proto, "static") == 0)
            locale_format_s(buf, size, "network.eth_status_static_fmt", ip);
        else
            locale_format_s(buf, size, "network.eth_status_dhcp_fmt", ip);
    } else {
        snprintf(buf, size, "%s", locale_get("network.not_connected"));
    }
}

/* ------------------------------------------------------------------ */
/* Reset to DHCP                                                       */
/* ------------------------------------------------------------------ */

eth_result_t eth_setup_clear(void)
{
    system("uci set network.wan.proto='dhcp'");
    system("uci delete network.wan.ipaddr 2>/dev/null");
    system("uci delete network.wan.netmask 2>/dev/null");
    system("uci delete network.wan.gateway 2>/dev/null");
    system("uci delete network.wan.dns 2>/dev/null");
    system("uci delete network.wan.hostname 2>/dev/null");

    int rc = system("uci commit");
    system("/etc/init.d/network restart >/dev/null 2>&1");

    return rc == 0 ? ETH_OK : ETH_ERR_UCI_FAIL;
}
