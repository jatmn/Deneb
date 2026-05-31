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
- Replace the stock welcome/captive-portal splash branding with Deneb assets.
- Replace the stock `Welcome to your new Ultimaker 2+ Connect` boot text with the Deneb splash as the first UI screen on every boot, then automatically advance to the main UI after about 1 second.
- Write the Deneb splash directly to the ILI9341 framebuffer at S11 priority via raw RGB565, covering the ~100s gap before Cygnus starts. Unbind fbcon to prevent kernel console overwrites. Skip the Cygnus welcome screen entirely so the raw splash stays visible until the main menu loads.
- Schedule a reboot watchdog so the stock updating screen cannot remain indefinitely after the package exits.
- Preserve the stock `.img` firmware update path for official firmware images.

It does not add web UI changes, LAN printing, service cleanup, diagnostics collection, or optimization work. UI changes are limited to the USB update lane and Deneb splash branding.

Security note: `deneb` is a known temporary bootstrap password. Use only on a trusted local network and change it after first login.
