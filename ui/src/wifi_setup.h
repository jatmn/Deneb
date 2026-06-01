/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * WiFi setup via USB import.
 * Reads wifi.txt from a USB drive and configures the wireless interface
 * via UCI. Replaces the stock AP/captive-portal WiFi setup flow.
 *
 * No external library dependencies — uses system() for UCI calls.
 */

#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include <stdbool.h>

#define WIFI_SETUP_MAX_SSID     64
#define WIFI_SETUP_MAX_PASS     128
#define WIFI_SETUP_MAX_ENCR     16
#define WIFI_SETUP_MAX_IP       48
#define WIFI_SETUP_MAX_DNS      128
#define WIFI_SETUP_MAX_NTP      128
#define WIFI_SETUP_MAX_HOST     64
#define WIFI_SETUP_MAX_LINE     256

/* WiFi configuration parsed from wifi.txt */
typedef struct {
    char ssid[WIFI_SETUP_MAX_SSID];
    char password[WIFI_SETUP_MAX_PASS];
    char encryption[WIFI_SETUP_MAX_ENCR];   /* psk2, psk, wep, none, sae */
    char ip_address[WIFI_SETUP_MAX_IP];     /* static IP, empty=dhcp */
    char netmask[WIFI_SETUP_MAX_IP];
    char gateway[WIFI_SETUP_MAX_IP];
    char dns[WIFI_SETUP_MAX_DNS];           /* space-separated DNS servers */
    char ntp_server[WIFI_SETUP_MAX_NTP];    /* custom NTP server(s), space-sep */
    char hostname[WIFI_SETUP_MAX_HOST];     /* device hostname */
    char country[4];                        /* 2-letter country code */
    bool valid;
} wifi_config_t;

/* Result codes for wifi_setup operations */
typedef enum {
    WIFI_OK = 0,
    WIFI_ERR_NO_USB,        /* No USB drive found */
    WIFI_ERR_NO_FILE,       /* wifi.txt not found on USB */
    WIFI_ERR_NO_SSID,       /* SSID not specified */
    WIFI_ERR_UCI_FAIL,      /* UCI command failed */
    WIFI_ERR_NET_FAIL,      /* Network restart failed */
} wifi_result_t;

/**
 * Scan USB mount points for wifi.txt.
 * Checks /mnt/sda1, /mnt/usb, /media/usb in order.
 *
 * @param path_out  Buffer to receive full path to wifi.txt (min 160 bytes)
 * @param path_size Size of path_out buffer
 * @return WIFI_OK if found, WIFI_ERR_NO_USB or WIFI_ERR_NO_FILE
 */
wifi_result_t wifi_setup_find_config(char *path_out, int path_size);

/**
 * Parse a wifi.txt file into a wifi_config_t struct.
 * Format: key=value, one per line. # comments supported.
 * Keys: ssid, password, encryption, ip, netmask, gateway, dns, ntp, hostname, country
 *
 * @param path  Path to wifi.txt
 * @param cfg   Output config struct
 * @return WIFI_OK on success, error code on failure
 */
wifi_result_t wifi_setup_parse(const char *path, wifi_config_t *cfg);

/**
 * Apply a parsed WiFi configuration via UCI and restart networking.
 * Enables the radio, configures STA interface, disables AP.
 *
 * @param cfg   Parsed WiFi configuration
 * @return WIFI_OK on success, error code on failure
 */
wifi_result_t wifi_setup_apply(const wifi_config_t *cfg);

/**
 * Full import flow: find wifi.txt on USB, parse, apply.
 * Convenience wrapper around find + parse + apply.
 *
 * @param status_msg  Buffer for human-readable status (min 128 bytes)
 * @param msg_size    Size of status_msg buffer
 * @return WIFI_OK on success, error code on failure
 */
wifi_result_t wifi_setup_import(char *status_msg, int msg_size);

/**
 * Check if WiFi STA is currently configured and enabled.
 *
 * @return true if wireless.sta.disabled != '1' and SSID is set
 */
bool wifi_setup_is_configured(void);

/**
 * Get current WiFi connection status string.
 * Writes a brief status like "Connected to MyNetwork" or "Not configured".
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 */
void wifi_setup_get_status(char *buf, int size);

/**
 * Remove WiFi configuration and disable radio.
 * Clears SSID/password from UCI, disables STA, disables radio.
 *
 * @return WIFI_OK on success
 */
wifi_result_t wifi_setup_clear(void);

#endif /* WIFI_SETUP_H */
