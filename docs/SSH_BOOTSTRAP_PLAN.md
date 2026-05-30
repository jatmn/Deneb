# Deneb Get Started Installer

The first implementation target is `Deneb_get_started.img`, a minimal USB update package that prepares a stock Ultimaker 2+ Connect for Deneb work.

It unlocks SSH/root access and installs the Deneb package lane. After it runs, future Deneb packages can use `.deneb` files instead of masquerading as official UltiMaker firmware images.

It also disables the stock internet firmware update prompt/path for now. Deneb will replace that later with its own update flow, and the stock internet path can otherwise hide the USB update option when the printer believes `latest` differs from `nr`.

## Confirmed From Extracted Firmware

- `/etc/passwd` contains `root` as the only interactive login account.
- No `ultimaker` Unix login account is present in the extracted image.
- `/etc/config/dropbear` enables password authentication and root password authentication on port 22.
- `/etc/uci-defaults/97_disable_wifi_services` disables Dropbear unless `ultimaker.version.channel` is `internal`.
- The stock touchscreen firmware update flow accepts a tar-backed `.img`, extracts it to `/tmp/update`, and runs `/tmp/update/update.sh`.
- The stock signature verification path is currently non-blocking, but it still logs signature failures for unsigned tar-backed packages.

## Package Scope

- Enable/start Dropbear.
- Set the bootstrap SSH password.
- Patch the USB update file browser to show `.deneb` files.
- Patch single-file USB auto-selection to accept `.deneb` files.
- Skip UltiMaker signature verification only for `.deneb` files and the exact `Deneb_get_started.img` reinstall package.
- Disable stock internet firmware update checks and clear stale upstream update state.
- Replace the stock welcome/captive-portal splash branding with Deneb assets.
- Schedule a reboot watchdog so the stock updating screen cannot remain indefinitely after the package exits.
- Preserve the official `.img` firmware-update lane.
- Do not include UI changes beyond the USB update lane and Deneb splash branding.
- Do not include web UI changes.
- Do not include LAN printing changes.
- Do not include service cleanup.
- Do not include diagnostics collection.
- Do not include optimization work.

## Account Plan

- Treat `root` as the confirmed default login user unless live inspection proves otherwise.
- Set `root` bootstrap password to `deneb`.
- If a live device has an existing `ultimaker` Unix login account, set that account password to `deneb` too and verify it has an SSH-capable shell.
- Do not create a new `ultimaker` user in the bootstrap package.

## Verification

- Install from USB through the existing touchscreen update flow.
- Confirm the installer reboots after applying changes.
- Reboot and confirm SSH starts at boot.
- Confirm `root` login with password `deneb`.
- Confirm `ultimaker` login only if that account exists on the live device.
- Confirm a tar-backed `.deneb` package on USB appears in the firmware browser.
- Confirm a `.deneb` package extracts to `/tmp/update` and runs `/tmp/update/update.sh` without the UltiMaker signature-verification step.
- Confirm `Deneb_get_started.img` can be reinstalled from USB after Deneb is already installed.
- Confirm Settings -> Update firmware still offers the USB update path when the printer is online.
- Confirm the Deneb welcome/captive-portal splash assets are installed.
- Change the password after first login.

## Build

Build the tar-backed get-started `.img` package from the repo root:

```powershell
.\tools\build-get-started.ps1
```

The generated file is `dist\Deneb_get_started.img`, with `dist\Deneb_get_started.img.sha256` beside it. The `dist/` directory is intentionally ignored by git.
