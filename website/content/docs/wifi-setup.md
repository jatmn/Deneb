---
title: Wi-Fi setup
weight: 90
---


Deneb replaces the stock WiFi setup (AP captive portal) with a simple file-based
approach: create a `wifi.txt` file on a USB drive, insert it into the printer,
and import via the touchscreen.

Related: [Ethernet setup](/docs/ethernet-setup/), [UI README](https://github.com/jatmn/Deneb/blob/main/ui/README.md), and
the [wifi.txt example template](https://github.com/jatmn/Deneb/blob/main/ui/wifi.txt.example).

## Quick Start

1. Create a file called `wifi.txt` on a USB drive (FAT32 formatted)
2. Add at minimum your network name:

```
ssid=MyHomeNetwork
password=MyPassword
```

3. Insert USB drive into the printer
4. On the touchscreen: **Settings > Network > Load WiFi Settings from USB**
5. Wait for confirmation. WiFi will connect automatically.

## Supported Fields

All fields use `key=value` format, one per line. Keys are **case-insensitive**.
Lines starting with `#` are comments. Blank lines are ignored.

### Required

| Field    | Description                        | Example            |
|----------|------------------------------------|--------------------|
| `ssid`   | WiFi network name (case-sensitive) | `ssid=MyNetwork`   |

### Optional — Authentication

| Field        | Description                   | Values                                    | Default  |
|--------------|-------------------------------|-------------------------------------------|----------|
| `password`   | WiFi password                 | Any string                                | (empty)  |
| `encryption` | Authentication type           | See [Encryption Types](#encryption-types) | `psk2` with password, otherwise `none` |
| `country`    | 2-letter country code         | `US` (United States), or your country’s code              | `US`     |

### Optional — IP Configuration

If omitted, DHCP is used (the default for most networks).

| Field      | Description              | Example                | Default           |
|------------|--------------------------|------------------------|--------------------|
| `ip`       | Static IP address        | `ip=192.168.1.100`     | (DHCP)             |
| `netmask`  | Subnet mask              | `netmask=255.255.255.0`| `255.255.255.0`   |
| `gateway`  | Default gateway          | `gateway=192.168.1.1`  | (empty)            |
| `dns`      | DNS server(s)            | `dns=8.8.8.8 8.8.4.4` | (from DHCP)        |

### Optional — Device Identity

| Field      | Description              | Example                | Default          |
|------------|--------------------------|------------------------|-------------------|
| `hostname` | Device network name      | `hostname=MyPrinter`   | `Ultimaker-2C`   |

The hostname appears on the network and in DHCP leases. It is set on both
the WAN interface (`network.wwan.hostname`) and the system hostname
(`system.@system[0].hostname`). Only alphanumeric characters, hyphens, and
dots are recommended.

### Optional — Time

The examples use the [US NTP pool](https://www.ntppool.org/zone/us).
Leaving out `ntp` keeps the existing time-server list; it does not reset it.
This setting is shared by Wi-Fi and Ethernet. Importing an `ntp` line saves
the new list and restarts the time service so sync uses it (same apply step as
[change time servers over SSH](#advanced-change-time-servers-over-ssh)).

| Field        | Description                  | Example                         | Default                |
|--------------|------------------------------|---------------------------------|------------------------|
| `ntp`        | NTP time server(s)           | `ntp=0.us.pool.ntp.org`             | Existing system setting (unchanged)|

Multiple NTP servers can be space-separated: `ntp=0.us.pool.ntp.org 1.us.pool.ntp.org`

## Encryption Types

| Value in wifi.txt    | Alias for | Description                |
|----------------------|-----------|----------------------------|
| `psk2`               | —         | WPA2-Personal (most common)|
| `wpa2`               | `psk2`    | WPA2-Personal              |
| `wpa2-psk`           | `psk2`    | WPA2-Personal              |
| `wpa-psk`            | `psk`     | WPA-Personal               |
| `psk`                | —         | WPA-Personal (older)       |
| `wpa`                | `psk`     | WPA-Personal               |
| `wpa1`               | `psk`     | WPA-Personal               |
| `sae`                | —         | WPA3-Personal              |
| `wpa3`               | `sae`     | WPA3-Personal              |
| `wpa3-sae`           | `sae`     | WPA3-Personal              |
| `wep`                | —         | WEP (legacy, not recommended)|
| `none`               | —         | Open network (no password) |
| `open`               | `none`    | Open network               |

If `encryption` is omitted and a `password` is set, WPA2 (`psk2`) is assumed.
If `encryption` is omitted and no `password` is set, open (`none`) is assumed.

## Field Name Aliases

For convenience, several field names are accepted for the same setting:

| Setting      | Accepted keys                                          |
|--------------|-------------------------------------------------------|
| SSID         | `ssid`                                                |
| Password     | `password`, `pass`, `key`, `psk`                      |
| Encryption   | `encryption`, `encr`, `auth`, `security`              |
| IP address   | `ip`, `ip_address`, `ipaddress`, `static_ip`          |
| Netmask      | `netmask`, `mask`, `subnet`                           |
| Gateway      | `gateway`, `gw`, `router`                             |
| DNS          | `dns`, `nameserver`, `dns_server`                     |
| NTP server   | `ntp`, `ntp_server`, `ntpserver`, `timeserver`, `time_server` |
| Hostname     | `hostname`, `host`, `name`, `device_name`, `printer_name`     |
| Country      | `country`, `country_code`, `region`                           |

## Examples

### Simple WPA2 network (most common)

```
ssid=MyHomeWiFi
password=MySecretPassword
```

### Open network (no password)

```
ssid=GuestNetwork
encryption=none
```

### Static IP with custom DNS, NTP, and hostname

```
ssid=OfficeNetwork
password=OfficePass123
hostname=Printer-3D-01
ip=10.0.0.50
netmask=255.255.255.0
gateway=10.0.0.1
dns=10.0.0.1 8.8.8.8
ntp=0.us.pool.ntp.org 1.us.pool.ntp.org
country=US
```

### WPA3 network with DHCP

```
ssid=SecureNetwork
password=WPA3Passphrase
encryption=sae
country=US
```

### WEP legacy network (not recommended)

```
ssid=OldRouter
password=abcde
encryption=wep
```

## Behavior Notes

- **Boot behavior**: WiFi station mode is disabled until a successful
  `wifi.txt` import. The legacy AP/captive-portal setup path is disabled and
  hidden from the live filesystem view, including AP-side DHCP/DNS and IPv6
  router-advertisement services and the AP DHCP scope. The stock service
  binaries remain in the read-only base image, but they are not run by Deneb.
  Once configured, WiFi auto-connects on subsequent boots as a client.

- **Changing WiFi**: Create a new wifi.txt and import again. The old config is
  overwritten.

- **Disconnecting WiFi**: Use **Settings > Network > Disconnect WiFi** on the
  touchscreen. This clears the saved config and disables the radio.

- **USB drive**: Any FAT32-formatted USB drive works. The system checks
  `/mnt/sda1`, `/mnt/usb`, and `/media/usb` for the file.

- **File name**: Must be exactly `wifi.txt` (case-sensitive on Linux).

- **Encoding**: Use plain ASCII or UTF-8. No BOM. Use Unix or Windows line
  endings (both work).

- **NTP**: Importing an `ntp` line replaces the saved time-server list and
  restarts the time service. Leaving that line out keeps the existing list; it
  does not restore defaults. This setting is shared by Wi-Fi and Ethernet.

## Advanced: change time servers over SSH

You can set US time servers directly instead of importing an `ntp` line.
Connect to the printer as `root` over SSH (see the
[technical installation guide](/docs/getting-started/#verify-bootstrap-success)).
Run these commands **on the printer**, one line at a time:

```sh
uci -q delete system.ntp.server
uci add_list system.ntp.server='0.us.pool.ntp.org'
uci add_list system.ntp.server='1.us.pool.ntp.org'
uci add_list system.ntp.server='2.us.pool.ntp.org'
uci add_list system.ntp.server='3.us.pool.ntp.org'
uci commit system
/etc/init.d/sysntpd restart
uci get system.ntp.server
```

The delete command removes the previous list; it can report no change if no
list was saved. The commit command saves the new list, and restarting the time
service applies it. The final command should display all four US server names
above. This confirms the saved configuration; time synchronization also requires
a working internet connection and may take a little time.
