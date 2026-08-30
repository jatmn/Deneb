# Deneb Get Started Package

This package prepares an Ultimaker 2+ Connect for Deneb development and follow-up Deneb packages.

It is intentionally narrow:

- Enable Dropbear at boot.
- Enable password authentication.
- Enable root password authentication and root login.
- Set the `root` bootstrap password to `deneb`.
- If an `ultimaker` Unix login account already exists on the target device, set its bootstrap password to `deneb` and ensure it has `/bin/ash`.
- Patch the touchscreen USB update flow to accept `.deneb` package files.
- Skip UltiMaker firmware signature verification only for `.deneb` package files and the exact `Deneb_get_started.img` reinstall package.
- Disable stock internet firmware update checks and prompts for now.
- Replace the stock nodogsplash/captive-portal splash with Deneb assets.
- Install the early-boot framebuffer splash via `/etc/init.d/deneb-splash` (raw RGB565 to `/dev/fb0` at S11, covering the gap before Cygnus). Skip the Cygnus LVGL welcome flow and go straight to the main menu so that splash stays visible until the menu is ready.
- Schedule a reboot watchdog so the stock updating screen cannot remain indefinitely after the package exits.
- Preserve the stock `.img` firmware update path for official firmware images.

It does not add web UI changes, LAN printing, service cleanup, diagnostics collection, or optimization work. UI changes are limited to the USB update lane and Deneb splash branding.

Security note: `deneb` is an intentional known bootstrap password for `root`,
and for `ultimaker` only when that login already exists. Use only on a trusted
local network. SSH login does not force a password change. Optionally run
`passwd` / `passwd ultimaker` yourself if you want non-default credentials.

## Build

From the repository root, complete
[Getting Started: Step 2](../../docs/GETTING_STARTED.md#step-2-build-the-get-started-bootstrap-package)
for your host. That procedure installs the hash-locked Pillow wheels in the
checkout venv, selects that exact interpreter, and runs the matching builder.
Do not invoke a bare builder on a fresh host; system Python is not the locked
bootstrap environment.

## Install

This package is step one of the stock-firmware migration. The full operator
sequence is documented in [Getting Started](../../docs/GETTING_STARTED.md).
Later Deneb stack updates use `.deneb` packages and are covered by
[Updating Deneb](../../docs/UPDATING.md).
