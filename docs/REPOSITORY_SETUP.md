# Repository Setup

This repository should stay private until the legal boundary, installer shape, and first hardware validation path are clear.

## GitHub Settings

- Visibility: private.
- Default branch: `main`.
- Require review before Stable release workflows can sign artifacts.
- Do not allow release signing secrets to run on arbitrary pull requests.
- Keep private keys only in GitHub Actions Secrets or another protected secret store.

## Files That Must Stay Out Of Git

- Firmware images.
- Extracted root filesystems.
- Modified full firmware images.
- Private keys and signing passphrases.
- Device credentials and identifiers.
- Support log exports unless explicitly scrubbed.

