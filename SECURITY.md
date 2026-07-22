# Security Policy

Deneb is experimental firmware for printers owned or controlled by the user.
Only the current `main` branch receives security fixes; there is no supported
stable release yet.

## Bootstrap SSH Warning

The implemented bootstrap package intentionally enables root SSH with the known
password `deneb`. This is a deliberate development/recovery mode, not an
accidental secret and not suitable for an untrusted network. Operators who need
a hardened deployment must change or disable this access themselves; Deneb does
not currently claim a hardened network posture.

## Network Controls

Do not expose unauthenticated print, heat, motion, file upload, firmware update, or raw G-code controls on untrusted networks.

## Secrets

Private signing keys, passphrases, GitHub tokens, device credentials, Wi-Fi passwords, and device-unique sensitive identifiers must not be committed.

## Reporting a Vulnerability

Report vulnerabilities privately through a GitHub Security Advisory for this
repository when available, or contact the repository owner privately through
their GitHub profile. Do not publish an exploitable printer-control issue in a
public issue before a fix and disclosure plan exist.
