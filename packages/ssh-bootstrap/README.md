# Deneb SSH Bootstrap Package

This package enables SSH access for early Deneb development and diagnostics.

It is intentionally narrow:

- Enable Dropbear at boot.
- Enable password authentication.
- Enable root password authentication and root login.
- Set the `root` bootstrap password to `deneb`.
- If an `ultimaker` Unix login account already exists on the target device, set its bootstrap password to `deneb` and ensure it has `/bin/ash`.

It does not add UI changes, web UI changes, LAN printing, service cleanup, diagnostics collection, or optimization work.

Security note: `deneb` is a known temporary bootstrap password. Use only on a trusted local network and change it after first login.

