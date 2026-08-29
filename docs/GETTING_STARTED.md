# Getting Started with Deneb

This guide is the first-install path from **stock UltiMaker 2+ Connect
firmware** to a working experimental Deneb stack.

Deneb is still experimental. It is not a complete independent firmware image.
The current path is a two-package migration on top of the printer's existing
OpenWrt/Cygnus base:

1. Install `Deneb_get_started.img` once from stock firmware.
2. Install a `Deneb_Update_*.deneb` package to deploy the native UI, print
   service, Web/API runtime, and related services.

If Deneb is already installed and you only need a newer package, use
[Updating Deneb](UPDATING.md) instead.

## What you need

| Item | Notes |
| --- | --- |
| UltiMaker 2+ Connect | Working stock touchscreen firmware update path |
| FAT32 USB drive | Used for both the bootstrap `.img` and later `.deneb` packages |
| Build host | Native Debian/Linux, or Windows 10/11 with Debian WSL 2 |
| Network access for the first build | Toolchain, ZeroMQ, lighttpd, and related pinned deps |
| Trusted local network only | Bootstrap enables SSH with the known password `deneb` and does not force a password change on login |

Optional after install:

- Ethernet or Wi-Fi for SSH, Web UI, Cura discovery, and Digital Factory
- [WiFi setup via USB](WIFI_SETUP.md) and [Ethernet setup via USB](ETH_SETUP.md)

## Safety and expectations

- Deneb controls motion, heating, networking, and updates. Treat the first
  install as hardware-affecting work.
- Do not flash while a print is active.
- Keep the printer on a trusted local network while SSH is reachable with the
  known bootstrap password, and understand the exposed services.
- The bootstrap package intentionally sets the known password `deneb` on
  `root`, and on `ultimaker` only when that Unix login already exists. SSH
  login does **not** force a password change for either account. Changing
  away from `deneb` is optional operator hygiene, not part of the login flow.
- Official UltiMaker firmware remains the recovery path. Deneb is a community
  mod and is not endorsed by UltiMaker.
- Trust only packages you built yourself, or release artifacts whose checksum
  and provenance you verified.

## Install flow overview

```text
Stock UM2+ Connect firmware
        |
        |  USB: Deneb_get_started.img
        v
Bootstrap lane
  - Dropbear SSH enabled
  - known password deneb set on root (and ultimaker if present); login does not force a change
  - stock USB updater accepts .deneb packages
  - stock internet firmware prompts disabled
  - Deneb splash branding installed
        |
        |  USB: Deneb_Update_<version>.deneb
        v
Full experimental Deneb stack
  - native touchscreen UI
  - native print service
  - local Web/API + mDNS
  - native Digital Factory service path
```

The bootstrap package is required the first time because stock firmware only
offers a tar-backed `.img` update lane. `Deneb_get_started.img` is that first
bridge: it unlocks SSH and teaches the stock updater how to accept later
Deneb-owned `.deneb` packages. The full native stack is **not** inside the
bootstrap image.

## Step 1: Clone the repository

```sh
git clone --recurse-submodules https://github.com/jatmn/Deneb.git
cd Deneb
```

If the clone already exists:

```sh
git submodule update --init --recursive
```

## Step 2: Build the get-started bootstrap package

This produces:

- `dist/Deneb_get_started.img`
- `dist/Deneb_get_started.img.sha256`

### Native Debian/Linux

Host packages needed for the bootstrap package:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends ca-certificates python3 python3-pil tar
```

Build:

```sh
bash tools/build-get-started.sh
```

### Windows with Debian WSL 2 or native PowerShell tooling

Python 3 with Pillow is required on the Windows side for the splash conversion:

```powershell
py -3 -m pip install Pillow
powershell -ExecutionPolicy Bypass -File tools/build-get-started.ps1
```

`tools/build-get-started.ps1` is a thin wrapper around
`tools/build-ssh-bootstrap.ps1`.

### What the bootstrap package contains

The package is intentionally narrow. It:

- enables Dropbear SSH at boot with password auth and root login
- intentionally sets the known password `deneb` on `root`
- also sets that same known password on `ultimaker` only if that Unix login
  already exists
- does **not** expire those passwords or force a change on SSH login
- patches the stock USB firmware browser/auto-select path to accept `.deneb`
- skips UltiMaker signature verification only for `.deneb` files and the exact
  `Deneb_get_started.img` reinstall package
- disables stock internet firmware update checks/prompts
- installs Deneb splash branding, including an early framebuffer splash
- preserves the stock `.img` path on the stock/bootstrap USB updater so
  official firmware images can still be selected until a full `.deneb`
  install replaces that screen

It does **not** install the native UI, print service, Web UI, or Cura stack.
Those arrive in the `.deneb` update package.

## Step 3: Install the bootstrap package from stock firmware

1. Copy only `dist/Deneb_get_started.img` to the root of a FAT32 USB drive.
   Keeping one firmware file on the stick avoids ambiguous auto-selection.
2. Insert the USB drive into the printer.
3. On stock firmware open the firmware update flow and choose install from USB.
   The exact stock labels vary by version, but the path is the normal
   Maintenance / Update Firmware USB install.
4. Select `Deneb_get_started.img`.
5. Wait for the package to finish. The installer schedules a reboot watchdog and
   reboots the printer.

### Verify bootstrap success

After reboot:

1. Confirm the printer boots to the stock/main UI with Deneb splash branding.
2. Find the printer on your local network.
3. SSH in:

```sh
ssh root@PRINTER_IP
```

Password:

```text
deneb
```

That known password is intentional. Logging in as `root` or `ultimaker` does
**not** prompt for or require a password change. You can keep using `deneb`,
or optionally set your own passwords later.

4. Optional: if you want non-default SSH passwords, set them yourself from a
   `root` shell. Change every account you care about; `passwd` alone only
   affects the current account:

```sh
passwd                 # changes root when run as root
passwd ultimaker       # only if that account exists and you want it changed
```

Bootstrap does not force either command.

5. Confirm a later `.deneb` file would be visible by checking that the stock
   update browser now accepts `.deneb` packages, or simply continue to the next
   step and install one.

If SSH does not come up, confirm the printer rejoined the network, that you are
using `root`, and that nothing else on the LAN is intercepting port 22. The
bootstrap package is safe to reinstall from USB if needed; the package name
`Deneb_get_started.img` is specifically allowed for reinstalls.

## Step 4: Build the full Deneb update package

The full stack is packaged as `dist/Deneb_Update_<version>.deneb`.

Follow one complete lane from the
[Debian/Linux build environment](WSL_BUILD_ENVIRONMENT.md). Do not mix the
native checkout dependency tree with the Windows/WSL `/root` dependency tree.

### Native Debian/Linux

First-time dependency setup and experimental package build:

```sh
bash tools/setup-linux-build.sh "$PWD"
bash tools/build-update-release.sh --rebuild-zmq --rebuild-lighttpd
```

Later experimental rebuilds:

```sh
bash tools/build-update-release.sh
```

### Windows with Debian WSL 2

Complete the Windows/WSL lane's [Setup](WSL_BUILD_ENVIRONMENT.md#setup) and
[Build and audit](WSL_BUILD_ENVIRONMENT.md#build-and-audit) sections. That guide
owns the first-build dependency switches, later rebuild command, environment
verification, and recovery sequence; follow it from setup through the verified
package result without skipping to the later-build command.

A package is trustworthy only when the release wrapper finishes cleanly and
prints:

```text
Verified native-only print service package: ...
```

The `.deneb` file appearing in `dist/` before that line is not proof of a good
build.

## Step 5: Install the first full Deneb package

1. Copy `dist/Deneb_Update_<version>.deneb` to a FAT32 USB drive.
2. Insert the USB drive into the printer.
3. On the printer open **Maintenance > Update Firmware**.
4. Choose the USB install path and select the `.deneb` package.
5. Wait for installation and reboot.

The full installer deploys the native touchscreen UI, Web/API runtime, print
service, mDNS helper, Digital Factory native service path, locales, and related
init scripts. It also disables the stock Cygnus menu for the next boot and
replaces stock Wi-Fi captive-portal setup with USB import. From that next boot,
**Maintenance > Update Firmware** is the native Deneb updater and lists
`.deneb` packages only.

### Verify the full install

After reboot you should have:

- the native Deneb touchscreen UI
- local services for print, web/API, and mDNS
- **Maintenance > Update Firmware** offering the Deneb `.deneb` package path

Useful next checks:

- Settings > Network, then import Wi-Fi or Ethernet from USB if needed
- open the printer's local Web UI on the LAN
- optional Cura discovery via the Deneb Cura plugin docs

See:

- [WiFi setup](WIFI_SETUP.md)
- [Ethernet setup](ETH_SETUP.md)
- [Web UI](WEB_UI.md)
- [Cura integration](CURA_INTEGRATION.md)
- [Project status](PROJECT_STATUS.md)

## After install

| Task | Where |
| --- | --- |
| Optional: change SSH passwords | Manual `passwd` / `passwd ultimaker` only if you want non-default credentials; login never forces this |
| Configure Wi-Fi | USB `wifi.txt` + Settings > Network |
| Configure Ethernet | USB `eth.txt` + Settings > Network |
| Install a newer Deneb build | [Updating Deneb](UPDATING.md) |
| Understand current gaps | [Project status](PROJECT_STATUS.md) |
| Rebuild packages later | [Build environment](WSL_BUILD_ENVIRONMENT.md) |

## Returning toward official firmware

There are two USB updaters, and only one of them can select `.img` files:

- After bootstrap, while the stock/Cygnus menu is still running, the USB
  updater accepts official UltiMaker `.img` files as well as `.deneb`
  packages.
- After the first full `.deneb` install, the native Deneb update screen lists
  `.deneb` packages only. It will not show official firmware `.img` files or
  `Deneb_get_started.img`.

Deneb does not present its packages as UltiMaker-signed official images.
Factory reset in the UI is a local-settings reset, not a return to a pristine
vendor image.

If you need a clean vendor image:

- If the printer is still on the stock/bootstrap UI, install an official
  UltiMaker firmware `.img` through that USB firmware update flow.
- If the native Deneb UI is already running, use UltiMaker's own recovery
  guidance for a vendor image. The Deneb **Maintenance > Update Firmware**
  screen cannot install `.img` files.

Keep any personal full-flash backups you made before experimental work; Deneb
does not currently ship a complete independent image/rollback product.

## Troubleshooting

| Symptom | Likely cause | What to try |
| --- | --- | --- |
| Stock UI will not show the bootstrap file | Wrong extension, nested folder, or non-FAT32 stick | Put `Deneb_get_started.img` at the USB root on FAT32 |
| Bootstrap installs but SSH fails | Printer offline, wrong user, or password not yet applied | Confirm network, use `root` / `deneb`, reboot once, reinstall bootstrap if needed |
| `.deneb` package is not listed on the stock/bootstrap updater | Bootstrap never installed, or file extension/case/path issue | Reinstall `Deneb_get_started.img` from that same stock/bootstrap USB flow, keep one `*.deneb` at USB root |
| Update package build fails missing toolchain | Build lane not set up | Run the matching setup script in [WSL_BUILD_ENVIRONMENT.md](WSL_BUILD_ENVIRONMENT.md) |
| `.deneb` install fails audit/smoke checks | Incomplete or mixed package | Rebuild with the release wrapper and only flash a verified package |
| Printer stays on "updating firmware" | Update UI process interrupted | Wait for the reboot watchdog; power-cycle only if it remains stuck well beyond the normal update window |

## Related documents

- [Updating Deneb](UPDATING.md)
- [Build environment](WSL_BUILD_ENVIRONMENT.md)
- [Bootstrap package notes](../packages/ssh-bootstrap/README.md)
- [UI package install notes](../ui/README.md)
- Historical bootstrap plan (archived, not the live guide):
  [SSH_BOOTSTRAP_PLAN.md](archive/SSH_BOOTSTRAP_PLAN.md)
