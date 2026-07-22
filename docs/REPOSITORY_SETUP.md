# Repository Setup

This repository is intentionally private. Publication-readiness checks are maintained so a future visibility decision can be made from evidence, but passing them does not authorize changing repository visibility.

## GitHub Settings

- Visibility: private.
- Default branch: `main`.
- Require review before Stable release workflows can sign artifacts.
- Do not allow release signing secrets to run on arbitrary pull requests.
- Keep private keys only in GitHub Actions Secrets or another protected secret store.
- Keep Actions read-only by default and require CI before release-intended merges.

## Files That Must Stay Out Of Git

- Firmware images.
- Extracted root filesystems.
- Modified full firmware images.
- Private keys and signing passphrases.
- Device credentials and identifiers.
- Support log exports unless explicitly scrubbed.

