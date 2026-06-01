/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * WiFi setup via USB import.
 * Reads wifi.txt from a USB drive and configures wireless via UCI.
 * No external library dependencies — uses stdio/stdlib/string only.
 */

#include "wifi_setup.h"
#include "net_utils.h"
#include "locale.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Use shared helpers from net_utils. */
#define trim             net_trim
#define uci_escape       net_uci_escape
#define parse_config_line net_parse_key_value_line

/* ------------------------------------------------------------------ */
/* Find wifi.txt on USB                                                */
/* ------------------------------------------------------------------ */

wifi_result_t wifi_setup_find_config(char *path_out, int path_size)
{
    switch (net_find_usb_config("wifi.txt", path_out, path_size)) {
    case NET_USB_CONFIG_FOUND:
        return WIFI_OK;
    case NET_USB_CONFIG_NO_USB:
        return WIFI_ERR_NO_USB;
    case NET_USB_CONFIG_NO_FILE:
    default:
        return WIFI_ERR_NO_FILE;
    }
}

/* ------------------------------------------------------------------ */
/* Parse wifi.txt                                                      */
/* ------------------------------------------------------------------ */

wifi_result_t wifi_setup_parse(const char *path, wifi_config_t *cfg)
{
    FILE *fp;
    char line[WIFI_SETUP_MAX_LINE];
    int got_ssid = 0;
    int got_password = 0;
    int got_encryption = 0;

    memset(cfg, 0, sizeof(*cfg));
    /* Default country. Encryption is chosen after password parsing. */
    strncpy(cfg->country, "US", sizeof(cfg->country) - 1);

    fp = fopen(path, "r");
    if (!fp)
        return WIFI_ERR_NO_FILE;

    while (fgets(line, sizeof(line), fp)) {
        char *key;
        char *val;
        if (!parse_config_line(line, &key, &val))
            continue;

        if (strcmp(key, "ssid") == 0) {
            strncpy(cfg->ssid, val, sizeof(cfg->ssid) - 1);
            got_ssid = 1;
        } else if (strcmp(key, "password") == 0 ||
                   strcmp(key, "pass") == 0 ||
                   strcmp(key, "key") == 0 ||
                   strcmp(key, "psk") == 0) {
            strncpy(cfg->password, val, sizeof(cfg->password) - 1);
            got_password = cfg->password[0] != '\0';
        } else if (strcmp(key, "encryption") == 0 ||
                   strcmp(key, "encr") == 0 ||
                   strcmp(key, "auth") == 0 ||
                   strcmp(key, "security") == 0) {
            strncpy(cfg->encryption, val, sizeof(cfg->encryption) - 1);
            net_str_lower(cfg->encryption);
            got_encryption = cfg->encryption[0] != '\0';
        } else if (strcmp(key, "ip") == 0 ||
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
        } else if (strcmp(key, "country") == 0 ||
                   strcmp(key, "country_code") == 0 ||
                   strcmp(key, "region") == 0) {
            /* Take first 2 chars, uppercase */
            if (strlen(val) >= 2) {
                cfg->country[0] = toupper((unsigned char)val[0]);
                cfg->country[1] = toupper((unsigned char)val[1]);
                cfg->country[2] = '\0';
            }
        }
        /* Unknown keys are silently ignored — forward compatible */
    }

    fclose(fp);

    if (!got_ssid || cfg->ssid[0] == '\0')
        return WIFI_ERR_NO_SSID;

    /* Reject whitespace-only SSID */
    {
        char tmp[WIFI_SETUP_MAX_SSID];
        strncpy(tmp, cfg->ssid, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        trim(tmp);
        if (tmp[0] == '\0')
            return WIFI_ERR_NO_SSID;
    }

    if (!got_encryption)
        strncpy(cfg->encryption, got_password ? "psk2" : "none",
                sizeof(cfg->encryption) - 1);

    /* Normalize encryption type */
    if (strcmp(cfg->encryption, "wpa2") == 0 ||
        strcmp(cfg->encryption, "wpa2-psk") == 0) {
        strncpy(cfg->encryption, "psk2", sizeof(cfg->encryption) - 1);
    } else if (strcmp(cfg->encryption, "wpa") == 0 ||
               strcmp(cfg->encryption, "wpa1") == 0 ||
               strcmp(cfg->encryption, "wpa-psk") == 0) {
        strncpy(cfg->encryption, "psk", sizeof(cfg->encryption) - 1);
    } else if (strcmp(cfg->encryption, "wpa3") == 0 ||
               strcmp(cfg->encryption, "wpa3-sae") == 0 ||
               strcmp(cfg->encryption, "sae") == 0) {
        strncpy(cfg->encryption, "sae", sizeof(cfg->encryption) - 1);
    } else if (strcmp(cfg->encryption, "open") == 0 ||
               strcmp(cfg->encryption, "none") == 0) {
        strncpy(cfg->encryption, "none", sizeof(cfg->encryption) - 1);
    } else if (strcmp(cfg->encryption, "wep") == 0) {
        strncpy(cfg->encryption, "wep", sizeof(cfg->encryption) - 1);
    }
    /* Default to psk2 if unrecognized and password is set */
    if (strcmp(cfg->encryption, "none") != 0 &&
        strcmp(cfg->encryption, "psk2") != 0 &&
        strcmp(cfg->encryption, "psk") != 0 &&
        strcmp(cfg->encryption, "sae") != 0 &&
        strcmp(cfg->encryption, "wep") != 0) {
        if (got_password)
            strncpy(cfg->encryption, "psk2", sizeof(cfg->encryption) - 1);
        else
            strncpy(cfg->encryption, "none", sizeof(cfg->encryption) - 1);
    }

    cfg->valid = true;
    return WIFI_OK;
}

/* ------------------------------------------------------------------ */
/* Apply config via UCI                                                */
/* ------------------------------------------------------------------ */

wifi_result_t wifi_setup_apply(const wifi_config_t *cfg)
{
    char escaped_ssid[WIFI_SETUP_MAX_SSID * 4 + 4];
    char escaped_pass[WIFI_SETUP_MAX_PASS * 4 + 4];
    char cmd[512];
    int rc;

    if (!cfg || !cfg->valid || cfg->ssid[0] == '\0')
        return WIFI_ERR_NO_SSID;

    uci_escape(escaped_ssid, sizeof(escaped_ssid), cfg->ssid);
    uci_escape(escaped_pass, sizeof(escaped_pass), cfg->password);

    /* Enable the radio */
    rc = system("uci set wireless.radio0.disabled='0'");
    if (rc != 0)
        return WIFI_ERR_UCI_FAIL;

    /* Set country code */
    snprintf(cmd, sizeof(cmd),
             "uci set wireless.radio0.country='%s'", cfg->country);
    rc = system(cmd);
    if (rc != 0)
        return WIFI_ERR_UCI_FAIL;

    /* Configure STA interface */
    snprintf(cmd, sizeof(cmd),
             "uci set wireless.sta.ssid=%s", escaped_ssid);
    rc = system(cmd);
    if (rc != 0)
        return WIFI_ERR_UCI_FAIL;

    snprintf(cmd, sizeof(cmd),
             "uci set wireless.sta.encryption='%s'", cfg->encryption);
    rc = system(cmd);
    if (rc != 0)
        return WIFI_ERR_UCI_FAIL;

    system("uci delete wireless.sta.key 2>/dev/null");

    /* Set password (skip for open networks) */
    if (strcmp(cfg->encryption, "none") != 0 && cfg->password[0] != '\0') {
        snprintf(cmd, sizeof(cmd),
                 "uci set wireless.sta.key=%s", escaped_pass);
        rc = system(cmd);
        if (rc != 0)
            return WIFI_ERR_UCI_FAIL;
    }

    /* Enable STA, disable AP */
    system("uci set wireless.sta.disabled='0'");
    system("uci set wireless.ap.disabled='1'");
    system("uci set wireless.ap.hidden='1'");

    /* Configure network interface (static or dhcp) */
    if (cfg->ip_address[0] != '\0') {
        /* Static IP configuration */
        char escaped_ip[WIFI_SETUP_MAX_IP * 4 + 4];
        char escaped_netmask[WIFI_SETUP_MAX_IP * 4 + 4];
        char escaped_gateway[WIFI_SETUP_MAX_IP * 4 + 4];
        uci_escape(escaped_ip, sizeof(escaped_ip), cfg->ip_address);
        uci_escape(escaped_netmask, sizeof(escaped_netmask), cfg->netmask);
        uci_escape(escaped_gateway, sizeof(escaped_gateway), cfg->gateway);

        system("uci set network.wwan.proto='static'");
        system("uci delete network.wwan.gateway 2>/dev/null");
        system("uci delete network.wwan.dns 2>/dev/null");

        snprintf(cmd, sizeof(cmd),
                 "uci set network.wwan.ipaddr=%s", escaped_ip);
        system(cmd);

        if (cfg->netmask[0] != '\0') {
            snprintf(cmd, sizeof(cmd),
                     "uci set network.wwan.netmask=%s", escaped_netmask);
            system(cmd);
        } else {
            /* Default netmask for static IP */
            system("uci set network.wwan.netmask='255.255.255.0'");
        }

        if (cfg->gateway[0] != '\0') {
            snprintf(cmd, sizeof(cmd),
                     "uci set network.wwan.gateway=%s", escaped_gateway);
            system(cmd);
        }

        if (cfg->dns[0] != '\0') {
            /* Remove existing DNS entries and add new ones */
            system("uci delete network.wwan.dns 2>/dev/null");
            /* DNS can be space-separated; add each as a separate list entry */
            char dns_copy[WIFI_SETUP_MAX_DNS];
            strncpy(dns_copy, cfg->dns, sizeof(dns_copy) - 1);
            dns_copy[sizeof(dns_copy) - 1] = '\0';
            char *tok = strtok(dns_copy, " ,;");
            while (tok) {
                char escaped_dns[WIFI_SETUP_MAX_DNS * 4 + 4];
                uci_escape(escaped_dns, sizeof(escaped_dns), tok);
                snprintf(cmd, sizeof(cmd),
                         "uci add_list network.wwan.dns=%s", escaped_dns);
                system(cmd);
                tok = strtok(NULL, " ,;");
            }
        }
    } else {
        /* DHCP (default) */
        system("uci set network.wwan.proto='dhcp'");
        /* Clear any static config leftovers */
        system("uci delete network.wwan.ipaddr 2>/dev/null");
        system("uci delete network.wwan.netmask 2>/dev/null");
        system("uci delete network.wwan.gateway 2>/dev/null");
        system("uci delete network.wwan.dns 2>/dev/null");
    }

    /* Set hostname */
    if (cfg->hostname[0] != '\0') {
        char escaped_host[WIFI_SETUP_MAX_HOST * 4 + 4];
        uci_escape(escaped_host, sizeof(escaped_host), cfg->hostname);
        snprintf(cmd, sizeof(cmd),
                 "uci set network.wwan.hostname=%s", escaped_host);
        system(cmd);
        /* Also set system hostname */
        snprintf(cmd, sizeof(cmd),
                 "uci set system.@system[0].hostname=%s", escaped_host);
        system(cmd);
    } else {
        system("uci set network.wwan.hostname='Ultimaker-2C'");
    }

    /* Configure NTP server if specified */
    if (cfg->ntp_server[0] != '\0') {
        /* Remove existing NTP servers and add new ones */
        system("uci delete system.ntp.server 2>/dev/null");
        char ntp_copy[WIFI_SETUP_MAX_NTP];
        strncpy(ntp_copy, cfg->ntp_server, sizeof(ntp_copy) - 1);
        ntp_copy[sizeof(ntp_copy) - 1] = '\0';
        char *tok = strtok(ntp_copy, " ,;");
        while (tok) {
            char escaped_ntp[WIFI_SETUP_MAX_NTP * 4 + 4];
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
        return WIFI_ERR_UCI_FAIL;

    /* Restart networking to apply */
    rc = system("(wifi reload; /etc/init.d/network restart) >/dev/null 2>&1");
    if (rc != 0)
        return WIFI_ERR_NET_FAIL;

    return WIFI_OK;
}

/* ------------------------------------------------------------------ */
/* Full import flow                                                    */
/* ------------------------------------------------------------------ */

wifi_result_t wifi_setup_import(char *status_msg, int msg_size)
{
    char path[160];
    wifi_config_t cfg;
    wifi_result_t res;

    if (status_msg && msg_size > 0)
        status_msg[0] = '\0';

    /* Find wifi.txt on USB */
    res = wifi_setup_find_config(path, sizeof(path));
    if (res != WIFI_OK) {
        if (status_msg) {
            if (res == WIFI_ERR_NO_USB)
                snprintf(status_msg, msg_size, "%s",
                         locale_get("network.no_usb"));
            else
                snprintf(status_msg, msg_size, "%s",
                         locale_get("network.no_wifi_txt"));
        }
        return res;
    }

    /* Parse wifi.txt */
    res = wifi_setup_parse(path, &cfg);
    if (res != WIFI_OK) {
        if (status_msg) {
            switch (res) {
            case WIFI_ERR_NO_SSID:
                snprintf(status_msg, msg_size, "%s",
                         locale_get("network.wifi_no_ssid"));
                break;
            default:
                snprintf(status_msg, msg_size, "%s",
                         locale_get("network.wifi_parse_error"));
                break;
            }
        }
        return res;
    }

    /* Apply configuration */
    res = wifi_setup_apply(&cfg);
    if (status_msg) {
        switch (res) {
        case WIFI_OK:
            snprintf(status_msg, msg_size,
                     locale_get("network.wifi_configured_fmt"), cfg.ssid);
            break;
        case WIFI_ERR_UCI_FAIL:
            snprintf(status_msg, msg_size, "%s",
                     locale_get("network.wifi_save_failed"));
            break;
        case WIFI_ERR_NET_FAIL:
            snprintf(status_msg, msg_size, "%s",
                     locale_get("network.wifi_restarting"));
            break;
        default:
            snprintf(status_msg, msg_size, "%s",
                     locale_get("network.wifi_setup_failed"));
            break;
        }
    }

    return res;
}

/* ------------------------------------------------------------------ */
/* Status queries                                                      */
/* ------------------------------------------------------------------ */

bool wifi_setup_is_configured(void)
{
    /* Check if STA has an SSID configured */
    char buf[128] = {0};

    net_read_command(buf, sizeof(buf),
                     "uci -q get wireless.sta.ssid 2>/dev/null");

    return buf[0] != '\0';
}

bool wifi_setup_is_enabled(void)
{
    char buf[128] = {0};
    int radio_disabled = 1;
    int sta_disabled = 1;

    if (!wifi_setup_is_configured())
        return false;

    net_read_command(buf, sizeof(buf),
                     "uci -q get wireless.radio0.disabled 2>/dev/null");
    radio_disabled = atoi(buf);

    buf[0] = '\0';
    net_read_command(buf, sizeof(buf),
                     "uci -q get wireless.sta.disabled 2>/dev/null");
    sta_disabled = atoi(buf);

    return radio_disabled != 1 && sta_disabled != 1;
}

void wifi_setup_get_status(char *buf, int size)
{
    if (!buf || size <= 0)
        return;

    buf[0] = '\0';

    if (!wifi_setup_is_configured()) {
        snprintf(buf, size, "%s", locale_get("network.wifi_not_configured"));
        return;
    }

    /* Get SSID */
    char ssid[128] = {0};
    net_read_command(ssid, sizeof(ssid),
                     "uci -q get wireless.sta.ssid 2>/dev/null");

    if (!wifi_setup_is_enabled()) {
        if (ssid[0] != '\0')
            snprintf(buf, size, locale_get("network.wifi_off_fmt"), ssid);
        else
            snprintf(buf, size, "%s", locale_get("network.wifi_disabled"));
        return;
    }

    /* Check if actually connected (has IP on apcli0) */
    char ip[64] = {0};
    net_read_command(ip, sizeof(ip),
                     "ip -4 addr show apcli0 2>/dev/null | "
                     "grep -o 'inet [^ ]*' | cut -d' ' -f2 | cut -d/ -f1");

    if (ip[0] != '\0')
        snprintf(buf, size, locale_get("network.wifi_connected_fmt"),
                 ssid, ip);
    else if (ssid[0] != '\0')
        snprintf(buf, size, locale_get("network.wifi_connecting_fmt"),
                 ssid);
    else
        snprintf(buf, size, "%s", locale_get("network.wifi_configured"));
}

/* ------------------------------------------------------------------ */
/* Clear WiFi config                                                   */
/* ------------------------------------------------------------------ */

wifi_result_t wifi_setup_clear(void)
{
    /* Clear STA config */
    system("uci set wireless.sta.ssid=''");
    system("uci set wireless.sta.key=''");
    system("uci set wireless.sta.disabled='1'");

    /* Disable radio */
    system("uci set wireless.radio0.disabled='1'");

    /* Reset network to DHCP */
    system("uci set network.wwan.proto='dhcp'");
    system("uci delete network.wwan.ipaddr 2>/dev/null");
    system("uci delete network.wwan.netmask 2>/dev/null");
    system("uci delete network.wwan.gateway 2>/dev/null");
    system("uci delete network.wwan.dns 2>/dev/null");

    int rc = system("uci commit");

    /* Bring down WiFi */
    system("(wifi down) >/dev/null 2>&1");

    return rc == 0 ? WIFI_OK : WIFI_ERR_UCI_FAIL;
}

wifi_result_t wifi_setup_set_enabled(bool enabled)
{
    int rc;

    if (enabled && !wifi_setup_is_configured())
        return WIFI_ERR_NO_SSID;

    if (enabled) {
        system("uci set wireless.radio0.disabled='0'");
        system("uci set wireless.sta.disabled='0'");
        system("uci set wireless.ap.disabled='1'");
        system("uci set wireless.ap.hidden='1'");
    } else {
        system("uci set wireless.sta.disabled='1'");
        system("uci set wireless.radio0.disabled='1'");
    }

    rc = system("uci commit");
    if (rc != 0)
        return WIFI_ERR_UCI_FAIL;

    if (enabled)
        rc = system("(wifi reload; /etc/init.d/network restart) >/dev/null 2>&1");
    else
        rc = system("(wifi down) >/dev/null 2>&1");

    return rc == 0 ? WIFI_OK : WIFI_ERR_NET_FAIL;
}
