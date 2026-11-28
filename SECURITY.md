# Security Policy

Deneb is experimental firmware for printers owned or controlled by the user.
Only the current `main` branch receives security fixes. GitHub Releases may
publish `Deneb_get_started.img` and `DenebUM2CNetworkPrinting.curapackage`.
The bootstrap overlay is not a stable firmware release. The Cura package is
a desktop plugin, not printer firmware.

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

## Dependency and Code Scanning Maintenance

Check dependency update PRs against `docs/SOURCE_PROVENANCE.md` and update
the matching notices, hashes, and lockfiles in the same PR. For the npm, Go,
and pip dependencies below, `tools/check-dependency-notices.sh` enforces
version consistency in `policy-validation`: it reads each locked version and
fails when a provenance row, third-party notice, versioned license file name,
or `REUSE.toml` path names another version, and when the FlexSearch provenance
row does not carry the pinned bundle SHA-256. Review the rest by hand: archive
SHA-256 values and source revisions in `docs/SOURCE_PROVENANCE.md`, wheel
hashes, license text and SPDX identifiers, and GitHub Actions updates, which
have no notices. Review upstream advisories for native sources alongside the
release SBOM.

Dependabot covers the website's Hextra Go module and FlexSearch npm lockfile,
the ESLint tooling used by both web surfaces, the bootstrap Python pin, and
GitHub Actions. The printer Web UI uses first-party browser scripts.

Dependabot cannot read the remaining pins. The monthly `Upstream pins`
workflow runs `tools/check-upstream-pins.sh` and creates or updates one open
issue titled "Upstream pin review" when any of them is behind:

- Mbed TLS (4.x line) and its bundled TF-PSA-Crypto, libzmq, and lighttpd
  1.4.x from `tools/native-dependency-pins.sh`.
- The OpenWrt MT76x8 toolchain release. Its GCC and musl versions are compared
  with the newest OpenWrt release, because they ship inside that toolchain.
  Upstream musl is listed for information only.
- The LVGL submodule against the newest release in its major line and the head
  of its maintenance branch.
- Hugo from `website/vendor-pins/hugo.version`, Gitleaks in `ci.yml`, and the
  `reuse` linter version in `ci.yml` and `AGENTS.md`.

Only Mbed TLS and LVGL are compared within their documented major lines;
every other pin is compared with the newest stable release of any version.
A malformed, missing, or disagreeing pin also needs review. The report always
reads the default branch, and a later clean run rewrites the open issue to say
nothing needs review instead of closing it. The report never changes a pin,
and a failed upstream lookup for a tracked pin fails the run instead of
reporting the pin as current.

- [ZeroMQ security advisories](https://github.com/zeromq/libzmq/security/advisories)
- [Mbed TLS security advisories](https://mbed-tls.readthedocs.io/en/latest/security-advisories/)
- [lighttpd release notes](https://www.lighttpd.net/releases/)
- [LVGL security advisories](https://github.com/lvgl/lvgl/security/advisories)

The build-backed CodeQL workflow compiles the real Linux backends with host
compilers. The pinned MIPS compiler finished its build in the pilot, but its
first-party compilation units were absent from the resulting CodeQL database.
The workflow also scans Python, Actions, and JavaScript/TypeScript, and checks
that CodeQL extracted the tracked scripts under `web/www` and `website`.
The website's Hugo templates, deployment configuration, and Markdown content
have separate build and link checks.

While GitHub default setup is active, the build-backed workflow runs without
uploading results. Once all four jobs pass on `main`, switch the repository's
CodeQL setting to advanced, set the repository Actions variable
`DENEB_CODEQL_UPLOAD` to `true`, and dispatch the workflow on `main`. Confirm
that GitHub receives results for each language. If uploads fail and coverage
cannot be restored promptly, remove the variable and re-enable default setup.
