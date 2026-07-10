# Deneb Get Started Installer

> **Archived:** This is retained for historical traceability. It is not the
> current project status or work queue. See [../PROJECT_STATUS.md](../PROJECT_STATUS.md).

The first implementation target is `Deneb_get_started.img`, a minimal USB update package that prepares a stock Ultimaker 2+ Connect for Deneb work.

It unlocks SSH/root access and installs the Deneb package lane. After it runs, future Deneb packages use `.deneb` files instead of masquerading as official UltiMaker firmware images.

It also disables the stock internet firmware update prompt/path. Deneb only updates to Deneb-owned firmware/packages; future release checks should point at Deneb releases instead of UltiMaker firmware.

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
- Replace the stock `Welcome to your new Ultimaker 2+ Connect` boot text with the Deneb splash as the first UI screen on every boot, then automatically advance to the main UI after about 1 second.
- Write the Deneb splash directly to the ILI9341 framebuffer (/dev/fb0) at S11 priority (~2s) via raw RGB565 data, covering the ~100s gap before Cygnus starts at S96. Unbind fbcon to prevent kernel console from overwriting the splash. Skip the Cygnus welcome screen entirely so the raw splash stays visible until the main menu loads.
- Schedule a reboot watchdog so the stock updating screen cannot remain indefinitely after the package exits.
- Preserve the recovery/bootstrap `.img` path only for `Deneb_get_started.img`
  and documented stock recovery images. Runtime UI updates are Deneb `.deneb`
  packages only.
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
- Confirm Maintenance -> Update Firmware offers the Deneb USB package path when Deneb UI is installed.
- Confirm the Deneb welcome/captive-portal splash assets are installed.
- Confirm the Deneb splash replaces the stock welcome text on every boot and automatically advances to the main UI.
- Confirm the Deneb splash appears on the ILI9341 display within ~2s of power-on (S11 init script), before Cygnus starts.
- Change the password after first login.

## Build

Build the tar-backed get-started `.img` package from the repo root:

```powershell
.\tools\build-get-started.ps1
```

The generated file is `dist\Deneb_get_started.img`, with `dist\Deneb_get_started.img.sha256` beside it. The `dist/` directory is intentionally ignored by git.
