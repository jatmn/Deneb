---
title: Technical installation
weight: 15
---

Want the simpler download-and-USB instructions? Start with the
[Getting started (easy guide)](/docs/quick-start/).

This technical guide covers building your own packages, detailed verification,
SSH checks, and recovery. If you used the prebuilt image already, continue at
[Step 4: Build the full Deneb update package](#step-4-build-the-full-deneb-update-package).

This guide is the first-install path from **stock UltiMaker 2+ Connect
firmware** to a working experimental Deneb stack.

Deneb is still experimental. It is not a complete independent firmware image.
The current path is a two-package migration on top of the printer's existing
OpenWrt/Cygnus base:

1. Install `Deneb_get_started.img` once from stock firmware.
2. Install a `Deneb_Update_*.deneb` package to deploy the native UI, print
   service, Web/API runtime, and related services.

If Deneb is already installed and you only need a newer package, use
[Updating Deneb](/docs/updating/) instead.

## What you need

| Item | Notes |
| --- | --- |
| UltiMaker 2+ Connect | Working stock touchscreen firmware update path |
| FAT32 USB drive | Used for both the bootstrap `.img` and later `.deneb` packages |
| Build host | Only if you rebuild `Deneb_get_started.img` or a `.deneb` package: native Debian/Linux, or Windows 10/11 with Debian WSL 2 |
| Network access for a local build | Toolchain, ZeroMQ, lighttpd, and related pinned deps |
| Trusted local network only | Bootstrap enables SSH with the known password `deneb` and does not force a password change on login |

Optional after install:

- Ethernet or Wi-Fi for SSH, Web UI, Cura discovery, and Digital Factory
- [WiFi setup via USB](/docs/wifi-setup/) and [Ethernet setup via USB](/docs/ethernet-setup/)

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

## Download or build the bootstrap image

GitHub Releases attach `Deneb_get_started.img` (Deneb overlay only; no
UltiMaker firmware). Prefer the `get-started` release; it updates on `main`
pushes when bootstrap sources change. The `nightly-get-started` pre-release
uses that same skip-if-unchanged rule on the daily schedule or a
`nightly=true` dispatch, not on ordinary `main` pushes.

1. Open https://github.com/jatmn/Deneb/releases
2. Download `Deneb_get_started.img` and `Deneb_get_started.img.sha256` into
   the same folder
3. Verify the checksum in that folder:

   Native Debian/Linux:

   ```sh
   sha256sum --check Deneb_get_started.img.sha256
   ```

   Windows PowerShell:

   ```powershell
   $expected = (Get-Content Deneb_get_started.img.sha256).Split()[0].ToLowerInvariant()
   $actual = (Get-FileHash Deneb_get_started.img -Algorithm SHA256).Hash.ToLowerInvariant()
   if ($actual -ne $expected) { throw "Deneb_get_started.img checksum mismatch" }
   ```

4. Continue at
   [Step 3](#step-3-install-the-bootstrap-package-from-stock-firmware). Set
   `img_dir` / `$imgDir` there to this download folder (do not use `dist/`
   unless you rebuilt in Step 2).

Do not copy the image if verification fails. Re-download both files and verify
again before continuing. Skip clone and Step 2 unless you want to rebuild the
image. `.deneb` stack packages are not published from this automation yet.

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

Host packages needed for the bootstrap package (Python 3.10 or newer):

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends ca-certificates python3 python3-venv tar
python3 -m venv build/bootstrap-python
build/bootstrap-python/bin/python -m pip install --disable-pip-version-check \
  --only-binary=:all: --require-hashes -r tools/bootstrap-requirements.txt
```

Build:

```sh
DENEB_BOOTSTRAP_PYTHON="$PWD/build/bootstrap-python/bin/python" \
  bash tools/build-get-started.sh
```

### Windows with Debian WSL 2 or native PowerShell tooling

Python 3.10 or newer with Pillow is required on the Windows side for the
splash conversion:

```powershell
py -3 -m venv build/bootstrap-python
build/bootstrap-python/Scripts/python.exe -m pip install --disable-pip-version-check `
  --only-binary=:all: --require-hashes -r tools/bootstrap-requirements.txt
$env:DENEB_BOOTSTRAP_PYTHON = (Resolve-Path build/bootstrap-python/Scripts/python.exe).Path
powershell -ExecutionPolicy Bypass -File tools/build-get-started.ps1
```

Both lanes install the repository-locked Pillow wheels with verified hashes.
The builder must see that exact interpreter through `DENEB_BOOTSTRAP_PYTHON`.
System Python is not the locked bootstrap environment.

The builders also reject generated RGB565 bytes that do not match the audited
digest in `assets/branding/deneb-splash.rgb565.sha256`.
Before changing a password, enabling SSH, or writing branding/UI files, the
target installer dry-runs its exact Cygnus patch transformation and aborts if
the stock layout is incompatible.

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
- installs Deneb splash branding, including an early framebuffer splash, and
  skips the Cygnus LVGL welcome screen so that splash stays until the main menu
- preserves the stock `.img` path on the stock/bootstrap USB updater so
  official firmware images can still be selected until a full `.deneb`
  install replaces that screen

It does **not** install the native UI, print service, Web UI, or Cura stack.
Those arrive in the `.deneb` update package.

## Step 3: Install the bootstrap package from stock firmware

Before copying the package to USB, verify the image you will copy. Set
`img_dir` / `$imgDir` to the GitHub Releases download folder from above, or to
`dist` only after a local rebuild.

Native Debian/Linux:

```sh
img_dir=/path/to/download-folder   # folder that holds both GitHub Releases files
# img_dir=dist                     # after a local rebuild from Step 2
(cd "$img_dir" && sha256sum --check Deneb_get_started.img.sha256)
```

Windows PowerShell:

```powershell
$imgDir = "C:\Users\YOU\Downloads"   # folder that holds both GitHub Releases files
# $imgDir = "dist"                   # after a local rebuild from Step 2
$expected = (Get-Content (Join-Path $imgDir "Deneb_get_started.img.sha256")).Split()[0].ToLowerInvariant()
$actual = (Get-FileHash (Join-Path $imgDir "Deneb_get_started.img") -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw "Deneb_get_started.img checksum mismatch" }
```

Do not install or copy the image if verification fails. Rebuild or re-download
it and verify the replacement before continuing.

1. Copy only that verified `Deneb_get_started.img` (from `$img_dir` / `$imgDir`,
   not a nested folder) to the root of a FAT32 USB drive. Keeping one firmware
   file on the stick avoids ambiguous auto-selection.
2. Before ejecting the drive, verify the copy that is actually on the USB
   filesystem. Replace the example mount or drive letter with yours.

   Native Debian/Linux:

   ```sh
   usb_image=/media/USERNAME/USB_LABEL/Deneb_get_started.img
   expected=$(awk '{print $1}' "$img_dir/Deneb_get_started.img.sha256")
   actual=$(sha256sum "$usb_image" | awk '{print $1}')
   [ "$actual" = "$expected" ] || { echo "USB image checksum mismatch" >&2; false; }
   ```

   Windows PowerShell:

   ```powershell
   $usbImage = "E:\Deneb_get_started.img"
   $expected = (Get-Content (Join-Path $imgDir "Deneb_get_started.img.sha256")).Split()[0].ToLowerInvariant()
   $actual = (Get-FileHash $usbImage -Algorithm SHA256).Hash.ToLowerInvariant()
   if ($actual -ne $expected) { throw "USB image checksum mismatch" }
   ```

   Do not continue if this destination check fails. Copy the verified source
   again and recheck the USB file.
3. Safely eject the USB drive, then insert it into the printer.
4. On stock firmware open the firmware update flow and choose install from USB.
   The exact stock labels vary by version, but the path is the normal
   Maintenance / Update Firmware USB install.
   If stock firmware only offers an internet update, disconnect the printer
   from Ethernet and Wi-Fi, then leave and reopen this screen to show the USB
   option. You can reconnect after the bootstrap install.
5. Select `Deneb_get_started.img`.
6. Wait for the package to finish. The installer schedules a reboot watchdog and
   reboots the printer.

### Verify bootstrap success

After reboot:

1. Confirm the printer boots to the stock/main UI with Deneb splash branding.
2. Find the printer on your local network.
3. SSH in:

```sh
ssh -o HostKeyAlgorithms=+ssh-rsa \
    -o PubkeyAcceptedAlgorithms=+ssh-rsa \
    root@PRINTER_IP
```

The stock Dropbear server uses legacy RSA algorithms that modern OpenSSH
clients disable by default, so these options are required for this bootstrap
login.

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

If you installed the prebuilt image without cloning the repository, complete
[Step 1](#step-1-clone-the-repository) first. Run the build commands from that
checkout. You can skip Step 2 when the prebuilt image is already installed.

The full stack is packaged as `dist/Deneb_Update_<version>.deneb`, with a
post-audit `.deneb.sha256` sidecar published by the successful release wrapper.

Follow one complete lane from the
[Debian/Linux build environment](/docs/build-environment/). Do not mix the
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

Start with the Windows/WSL lane's
[Prerequisites](/docs/build-environment/#prerequisites), then complete
[Setup](/docs/build-environment/#setup) and
[Build and audit](/docs/build-environment/#build-and-audit) sections. That guide
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

1. Complete [Verify and copy the update package](/docs/updating/#verify-and-copy-the-update-package).
2. Safely eject the verified FAT32 USB drive and insert it into the printer.
3. On the printer open **Maintenance > Update Firmware**.
4. Choose the USB install path and select the verified `.deneb` package.
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

- [WiFi setup](/docs/wifi-setup/)
- [Ethernet setup](/docs/ethernet-setup/)
- [Web UI](/docs/web-ui/)
- [Cura integration](/docs/cura/)
- [Project status](https://github.com/jatmn/Deneb/blob/main/docs/PROJECT_STATUS.md)

## After install

| Task | Where |
| --- | --- |
| Optional: change SSH passwords | Manual `passwd` / `passwd ultimaker` only if you want non-default credentials; login never forces this |
| Configure Wi-Fi | USB `wifi.txt` + Settings > Network |
| Configure Ethernet | USB `eth.txt` + Settings > Network |
| Install a newer Deneb build | [Updating Deneb](/docs/updating/) |
| Understand current gaps | [Project status](https://github.com/jatmn/Deneb/blob/main/docs/PROJECT_STATUS.md) |
| Rebuild packages later | [Build environment](/docs/build-environment/) |

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
| Update package build fails missing toolchain | Build lane not set up | Run the matching setup script in [Build environment](/docs/build-environment/) |
| `.deneb` install fails audit/smoke checks | Incomplete or mixed package | Rebuild with the release wrapper and only flash a verified package |
| Printer stays on "updating firmware" | Update UI process interrupted | Wait for the reboot watchdog; power-cycle only if it remains stuck well beyond the normal update window |

## Related documents

- [Updating Deneb](/docs/updating/)
- [Build environment](/docs/build-environment/)
- [Bootstrap package notes](https://github.com/jatmn/Deneb/blob/main/packages/ssh-bootstrap/README.md)
- [UI package install notes](https://github.com/jatmn/Deneb/blob/main/ui/README.md)
- Historical bootstrap plan (archived, not the live guide):
  [SSH_BOOTSTRAP_PLAN.md](https://github.com/jatmn/Deneb/blob/main/docs/archive/SSH_BOOTSTRAP_PLAN.md)
