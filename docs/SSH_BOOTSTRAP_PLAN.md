# SSH Bootstrap Plan

The first implementation target is a minimal SSH-only update package. It exists to unlock live-device inspection so later work can be based on real RAM, CPU, boot, storage, service, and hardware data.

## Confirmed From Extracted Firmware

- `/etc/passwd` contains `root` as the only interactive login account.
- No `ultimaker` Unix login account is present in the extracted image.
- `/etc/config/dropbear` enables password authentication and root password authentication on port 22.
- `/etc/uci-defaults/97_disable_wifi_services` disables Dropbear unless `ultimaker.version.channel` is `internal`.
- The touchscreen firmware update flow accepts a tar-backed `.img`, extracts it to `/tmp/update`, and runs `/tmp/update/update.sh`.

## Package Scope

- Only enable/start Dropbear.
- Only set the bootstrap SSH password.
- Do not include UI changes.
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
- Confirm SSH starts immediately after install.
- Reboot and confirm SSH starts at boot.
- Confirm `root` login with password `deneb`.
- Confirm `ultimaker` login only if that account exists on the live device.
- Change the password after first login.

