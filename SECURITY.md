# Security Policy

Deneb is intended for printers owned or controlled by the user. Early builds may expose diagnostic access while the project is being developed.

## Bootstrap SSH Warning

The planned first bootstrap package intentionally enables SSH and sets a known temporary password. That is only for trusted local-network development and diagnostics. The password must be changed after first login.

## Network Controls

Do not expose unauthenticated print, heat, motion, file upload, firmware update, or raw G-code controls on untrusted networks.

## Secrets

Private signing keys, passphrases, GitHub tokens, device credentials, Wi-Fi passwords, and device-unique sensitive identifiers must not be committed.

