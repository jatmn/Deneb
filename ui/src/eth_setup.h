/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Ethernet setup via USB import.
 * Reads eth.txt from a USB drive and configures the wired Ethernet interface
 * (network.wan / eth0) via UCI.
 *
 * No external library dependencies — uses system() for UCI calls.
 */

#ifndef ETH_SETUP_H
#define ETH_SETUP_H

#include <stdbool.h>

#define ETH_SETUP_MAX_IP    48
#define ETH_SETUP_MAX_DNS   128
#define ETH_SETUP_MAX_NTP   128
#define ETH_SETUP_MAX_HOST  64
#define ETH_SETUP_MAX_LINE  256

/* Ethernet configuration parsed from eth.txt */
typedef struct {
    char ip_address[ETH_SETUP_MAX_IP];     /* static IP, empty=dhcp */
    char netmask[ETH_SETUP_MAX_IP];
    char gateway[ETH_SETUP_MAX_IP];
    char dns[ETH_SETUP_MAX_DNS];           /* space-separated DNS servers */
    char ntp_server[ETH_SETUP_MAX_NTP];    /* custom NTP server(s), space-sep */
    char hostname[ETH_SETUP_MAX_HOST];     /* device hostname */
    bool valid;
} eth_config_t;

/* Result codes for eth_setup operations */
typedef enum {
    ETH_OK = 0,
    ETH_ERR_NO_USB,        /* No USB drive found */
    ETH_ERR_NO_FILE,       /* eth.txt not found on USB */
    ETH_ERR_UCI_FAIL,      /* UCI command failed */
    ETH_ERR_NET_FAIL,      /* Network restart failed */
} eth_result_t;

/**
 * Scan USB mount points for eth.txt.
 * Checks /mnt/sda1, /mnt/usb, /media/usb in order.
 */
eth_result_t eth_setup_find_config(char *path_out, int path_size);

/**
 * Parse an eth.txt file into an eth_config_t struct.
 * Format: key=value, one per line. # comments supported.
 * Keys: ip, netmask, gateway, dns, ntp, hostname
 */
eth_result_t eth_setup_parse(const char *path, eth_config_t *cfg);

/**
 * Apply a parsed Ethernet configuration via UCI and restart networking.
 * Sets network.wan to static or dhcp.
 */
eth_result_t eth_setup_apply(const eth_config_t *cfg);

/**
 * Full import flow: find eth.txt on USB, parse, apply.
 */
eth_result_t eth_setup_import(char *status_msg, int msg_size);

/**
 * Check if Ethernet is currently configured with a static IP.
 */
bool eth_setup_is_static(void);

/**
 * Get current Ethernet connection status string.
 */
void eth_setup_get_status(char *buf, int size);

/**
 * Reset Ethernet to DHCP.
 */
eth_result_t eth_setup_clear(void);

#endif /* ETH_SETUP_H */
