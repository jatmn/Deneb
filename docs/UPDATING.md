# Updating Deneb

This guide covers day-2 package updates after the first successful Deneb
install. For the stock-firmware bootstrap path, start with
[Getting Started](GETTING_STARTED.md).

## Update model

Deneb currently uses two package kinds:

| Package | When you need it | Extension | Result |
| --- | --- | --- | --- |
| `Deneb_get_started.img` | First install from stock, or repair of the bootstrap/SSH/`.deneb` lane | `.img` | Enables SSH and the Deneb USB update lane |
| `Deneb_Update_<version>.deneb` | First full install and every later Deneb stack update | `.deneb` | Installs/replaces the native UI and services |

Once bootstrap is in place, normal project updates are **only** `.deneb`
packages. You do not rebuild or reflash the bootstrap image for ordinary UI,
print-service, Web, or Digital Factory changes.

```text
Already bootstrapped printer
        |
        |  USB: Deneb_Update_<new-version>.deneb
        v
Updated experimental Deneb stack
```

## Before you update

- Finish or cancel any active print.
- Use a FAT32 USB drive.
- Prefer a single update file on the stick.
- Update only on a trusted local network.
- Know how you built or obtained the package. Prefer packages produced by the
  release wrappers in this repository.
- Read [Project status](PROJECT_STATUS.md) before moving between significantly
  different revisions; experimental builds can still change behavior.

## Build a newer update package

Use one complete build lane from
[Debian/Linux build environment](WSL_BUILD_ENVIRONMENT.md).

### Native Debian/Linux

```sh
bash tools/build-update-release.sh
```

First-time hosts still need setup and dependency bootstrap:

```sh
bash tools/setup-linux-build.sh "$PWD"
bash tools/build-update-release.sh --rebuild-zmq --rebuild-lighttpd
```

### Windows with Debian WSL 2

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1
```

### Release channels

The package manifest records a channel:

| Channel | Intended meaning | Extra build requirements |
| --- | --- | --- |
| `experimental` | Default development packages | None beyond a clean release-wrapper build |
| `nightly` | Higher-bar packaged builds | Verified stock/native print-service summaries |
| `stable` | Highest current packaging bar | Verified stock/native print-service summaries |

Example non-experimental native build:

```sh
bash tools/build-update-release.sh \
  --release-channel nightly \
  --printsvc-stock-summary /absolute/path/to/stock-summary \
  --printsvc-native-summary /absolute/path/to/native-summary
```

Only trust a package when the wrapper exits zero and prints a verification line
similar to:

```text
Verified native-only print service package: /path/to/dist/Deneb_Update_<version>.deneb
```

## Install a newer `.deneb` package

1. Copy `dist/Deneb_Update_<version>.deneb` to USB.
2. Insert the USB drive into the printer.
3. On Deneb open **Maintenance > Update Firmware**.
4. Select the new `.deneb` package.
5. Wait for installation and reboot.

The installer validates required binaries and audits, backs up selected stock
files on first transition, replaces Deneb-managed binaries/init scripts, and
reboots into the updated stack. Live Deneb UI processes may be restarted as
part of the install path; do not interrupt power during the update.

### What an update replaces

A current `.deneb` package refreshes the Deneb-managed runtime, including:

- `deneb-ui`
- `deneb-printsvc`
- `deneb-api` / Web assets / lighttpd front end
- `deneb-mdns`
- `deneb-dfsvc` and the native Digital Factory init path
- package manifest, locales, macros, and installer helper/audit tools

Stock read-only vendor image contents are not turned into a fully independent
Deneb OS image by this process. Deneb still overlays and replaces selected
runtime paths on the existing platform.

## Verify after updating

After reboot, spot-check:

1. Touchscreen boots into Deneb UI.
2. Settings > Network still shows expected connectivity.
3. Local Web UI responds on the LAN, if you use it.
4. A simple non-production status check works before any unattended print.
5. SSH still works if you rely on it for recovery or logs.

If you keep SSH enabled, confirm your non-default password(s) still work.
Normal `.deneb` updates are not the bootstrap password-reset path. Reinstalling
`Deneb_get_started.img` intentionally restores the known temporary `deneb`
password on `root` and on `ultimaker` when that account exists, so rotate those
accounts again afterward.

## When to rebuild or reinstall bootstrap

Reinstall `Deneb_get_started.img` only when you need the bootstrap lane itself:

- first migration from stock firmware
- SSH/Dropbear bootstrap repair
- stock USB updater no longer lists `.deneb` packages
- you intentionally want the bootstrap splash/update-lane patches reapplied

Build it with:

```sh
# Native Debian/Linux
bash tools/build-get-started.sh

# Windows
powershell -ExecutionPolicy Bypass -File tools/build-get-started.ps1
```

Then install it from the USB firmware update flow exactly as in
[Getting Started](GETTING_STARTED.md). The filename `Deneb_get_started.img` is
part of the allowed reinstall path.

Bootstrap reinstall does not by itself replace a full native stack. Install or
reinstall a `.deneb` package afterward if the UI/services need to be restored or
updated.

## Updating from stock again

If the printer has been returned to official UltiMaker firmware, it is back on
the first-install path:

1. Install `Deneb_get_started.img`.
2. Install `Deneb_Update_<version>.deneb`.

Do not expect a lone `.deneb` file to install on pure stock firmware. Stock
needs the bootstrap bridge first.

## Official firmware and recovery notes

- After bootstrap, official `.img` firmware files remain selectable in the USB
  update flow.
- Installing official firmware is the intentional escape hatch back toward
  vendor software.
- Deneb package signatures/branding must never be treated as UltiMaker
  signatures.
- Keep your own known-good backups if you do low-level recovery work. A future
  independent-image rollback product is planned; it is not the current update
  mechanism.

## Common update failures

| Symptom | Likely cause | What to try |
| --- | --- | --- |
| No `.deneb` files shown | Bootstrap lane missing or USB layout issue | Reinstall `Deneb_get_started.img`; place one file at USB root |
| Installer rejects package | Incomplete build or failed package audits | Rebuild with `build-update-release`; use only verified output |
| Services missing after reboot | Partial copy, wrong file flashed, or interrupted update | Reflash a verified `.deneb`; check SSH logs if available |
| Unexpected stock UI after update | Update failed closed and rolled the menu path back, or bootstrap-only state | Install a verified full `.deneb` package |
| Network features gone | Wi-Fi/Ethernet config not reapplied or USB import needed | Re-import `wifi.txt` / `eth.txt` |

## Related documents

- [Getting Started](GETTING_STARTED.md)
- [Build environment](WSL_BUILD_ENVIRONMENT.md)
- [Project status](PROJECT_STATUS.md)
- [UI package notes](../ui/README.md)
- [Web UI](WEB_UI.md)
- [Cura integration](CURA_INTEGRATION.md)
