# Ethernet Configuration via USB (eth.txt)

Deneb supports configuring the wired Ethernet interface via a file-based
approach: create an `eth.txt` file on a USB drive, insert it into the printer,
and import via the touchscreen.

Related: [WiFi setup](WIFI_SETUP.md), [UI README](../ui/README.md), and the
[eth.txt example template](../ui/eth.txt.example).

## Quick Start

1. Create a file called `eth.txt` on a USB drive (FAT32 formatted)
2. Add your desired settings (at minimum, just `ip=` for static):

```
ip=192.168.1.100
gateway=192.168.1.1
```

3. Insert USB drive into the printer
4. On the touchscreen: **Settings > Network > Load Ethernet Settings from USB**
5. Wait for confirmation.

If no `eth.txt` is present or all fields are omitted, the Ethernet interface
uses DHCP (the default).

## Supported Fields

All fields use `key=value` format, one per line. Keys are **case-insensitive**.
Lines starting with `#` are comments. Blank lines are ignored.

### Optional — IP Configuration

If all IP fields are omitted, DHCP is used.

| Field      | Description              | Example                | Default           |
|------------|--------------------------|------------------------|--------------------|
| `ip`       | Static IP address        | `ip=192.168.1.100`     | (DHCP)             |
| `netmask`  | Subnet mask              | `netmask=255.255.255.0`| `255.255.255.0`   |
| `gateway`  | Default gateway          | `gateway=192.168.1.1`  | (empty)            |
| `dns`      | DNS server(s)            | `dns=8.8.8.8 8.8.4.4` | (from DHCP)        |

### Optional — Device Identity

| Field      | Description              | Example                | Default          |
|------------|--------------------------|------------------------|-------------------|
| `hostname` | Device network name      | `hostname=MyPrinter`   | (unchanged)       |

The hostname is set on both the WAN interface and the system hostname.
Only alphanumeric characters, hyphens, and dots are recommended.

### Optional — Time

| Field  | Description          | Example                  | Default                |
|--------|----------------------|--------------------------|------------------------|
| `ntp`  | NTP time server(s)   | `ntp=pool.ntp.org`      | `0-3.lede.pool.ntp.org`|

Multiple NTP servers can be space-separated: `ntp=0.pool.ntp.org 1.pool.ntp.org`

## Field Name Aliases

| Setting      | Accepted keys                                          |
|--------------|-------------------------------------------------------|
| IP address   | `ip`, `ip_address`, `ipaddress`, `static_ip`          |
| Netmask      | `netmask`, `mask`, `subnet`                           |
| Gateway      | `gateway`, `gw`, `router`                             |
| DNS          | `dns`, `nameserver`, `dns_server`                     |
| NTP server   | `ntp`, `ntp_server`, `ntpserver`, `timeserver`, `time_server` |
| Hostname     | `hostname`, `host`, `name`, `device_name`, `printer_name`     |

## Examples

### Simple static IP

```
ip=192.168.1.100
gateway=192.168.1.1
dns=8.8.8.8
```

### Full static config with hostname and NTP

```
hostname=Printer-3D-01
ip=10.0.0.50
netmask=255.255.255.0
gateway=10.0.0.1
dns=10.0.0.1 8.8.8.8
ntp=ntp.example.com
```

### Reset to DHCP (empty file or no ip field)

```
# This file resets Ethernet to DHCP
# (omit ip= to use DHCP)
```

Or simply delete `eth.txt` from the USB and use **Settings > Network >
Reset Ethernet to DHCP** on the touchscreen.

## Behavior Notes

- **Default**: Ethernet uses DHCP out of the box.
- **Changing config**: Create a new `eth.txt` and import again. The old static
  config is overwritten.
- **Reset to DHCP**: Use **Settings > Network > Reset Ethernet to DHCP** on the
  touchscreen, or import an `eth.txt` with no `ip=` field.
- **USB drive**: Any FAT32-formatted USB drive works. The system checks
  `/mnt/sda1`, `/mnt/usb`, and `/media/usb` for the file.
- **File name**: Must be exactly `eth.txt` (case-sensitive on Linux).
- **Encoding**: Use plain ASCII or UTF-8. No BOM. Unix or Windows line endings.
- **Both interfaces**: You can have `wifi.txt` and `eth.txt` on the same USB
  drive. Import them separately via their respective buttons.
