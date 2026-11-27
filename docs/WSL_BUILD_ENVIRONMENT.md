# Deneb Debian/Linux Build Environment

Deneb release builds run on Debian or another Debian-compatible Linux host. The target uses Linux/POSIX APIs and a MIPS little-endian musl cross-compiler; MSVC is not a release-build substitute. Windows users may use the same Debian environment through WSL 2, but WSL is not required.

Native build dependencies are stored under the checkout's ignored `build/deneb-cross/` directory and all setup and package builds run as the checkout user. Only installing missing Debian host packages requires `sudo`. The Windows/WSL wrapper retains its isolated `/root` dependency layout.

## Choose a build lane

| Host checkout | Setup command | Release command |
| --- | --- | --- |
| Native Debian/Linux | `bash tools/setup-linux-build.sh "$PWD"` | `bash tools/build-update-release.sh` |
| Windows checkout with Debian WSL 2 | PowerShell invoking `tools/setup-wsl-build.sh` in WSL | `tools/build-update-release.ps1` |

First-time printer installation also needs the stock-firmware bootstrap package
from `tools/build-get-started.sh` or `tools/build-get-started.ps1`. That package
does not require the MIPS cross toolchain. Host bootstrap Python is in
[Getting Started](GETTING_STARTED.md). Operator USB install and later `.deneb`
updates: https://deneb3d.dev/docs/getting-started/ and
https://deneb3d.dev/docs/updating/

Do not mix the lanes: the native scripts keep dependencies in the checkout's
`build/deneb-cross/` directory, while the PowerShell wrapper uses `/root`
inside the WSL distribution.

## Lane 1: native Debian/Linux

Install Git, clone the repository, and initialize the pinned LVGL submodule:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential bzip2 ca-certificates cmake curl file git make pkg-config python3 tar xz-utils zstd
git clone https://github.com/jatmn/Deneb.git
cd Deneb
sh tools/setup-lvgl.sh
```

Native CI uses the same command. In a fresh checkout it fetches LVGL's pinned
`src/` and `include/` trees plus root files, including the license notices.
Rerun it after updating Deneb to select the new recorded pin. Existing full
LVGL checkouts can also use the command and keep their layout.

Prepare the pinned cross-toolchain and Mbed TLS dependency, then create and audit an experimental package:

```sh
bash tools/setup-linux-build.sh "$PWD"
bash tools/build-update-release.sh --rebuild-zmq --rebuild-lighttpd
```

The setup uses separate OpenWrt dependency and component build directories.
Rerun the setup and first-build commands when upgrading from the older
musl.cc toolchain to build all inputs for the MT7688 soft-float ABI.

Later experimental builds reuse those dependency trees:

```sh
bash tools/build-update-release.sh
```

The script cross-builds all native services, packages
`dist/Deneb_Update_<git-describe>.deneb`, and audits the archive. Trust a
package only after it prints `Verified native-only print service package`,
exits zero, and publishes the matching post-audit `.deneb.sha256` sidecar.
Before installation, follow the source-and-USB verification handoff at
https://deneb3d.dev/docs/updating/#verify-and-copy-the-update-package.

Nightly and stable packages require verified stock/native evidence summaries:

```sh
bash tools/build-update-release.sh \
  --release-channel nightly \
  --printsvc-stock-summary /absolute/path/to/stock-summary \
  --printsvc-native-summary /absolute/path/to/native-summary \
  --printsvc-native-evidence-summary /absolute/path/to/evidence-summary
```

## Lane 2: Windows checkout with Debian WSL 2

### Prerequisites

Use Windows 10 2004 or later or Windows 11 with virtualization enabled, Git, PowerShell, administrator access for WSL installation/repair, and internet access for the initial dependency setup.

```powershell
wsl --install -d Debian
wsl --update
wsl --status
wsl --list --verbose
wsl -d Debian -u root -- whoami
```

Reboot if requested and ensure Debian reports WSL version 2. The final command must print `root`; no `/etc/wsl.conf` default-user change is required.

Initialize the pinned repository submodules from Windows:

```powershell
git submodule update --init --recursive
```

Keep the checkout on a drive-letter path such as `C:\temp\Deneb`. The wrapper supports paths exposed to WSL as `/mnt/<drive>/...`; UNC-only paths are not supported.

### Setup

From PowerShell at the repository root, run the WSL setup command inside Debian:

```powershell
$repo = '/mnt/' + $PWD.Drive.Name.ToLower() + $PWD.Path.Substring(2).Replace('\', '/')
wsl -d Debian -u root -- sh "$repo/tools/setup-wsl-build.sh" "$repo"
```

The setup script installs Debian host tools (including Python 3 for host UI
screenshot conversion), refuses to overwrite incomplete dependency directories,
and verifies archives before extracting them.

| Input | Version | SHA-256 |
| --- | --- | --- |
| OpenWrt ramips/mt76x8 cross-toolchain | 25.12.6 | `628559363d3e41da6cc298ed0a27b81fbc3843a2758228c7697861defd324da3` |
| Mbed TLS (includes TF-PSA-Crypto 1.1.2) | 4.1.2 | `392215e5e4846df04e2f4c4516079303bd73edde837d8f3e68fb4d4d7ddf0126` |
| ZeroMQ | 4.3.5 | `6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43` |
| lighttpd | 1.4.85 | `18de51b393bac4a6827879e1a7ff377c169e414bae92cd245091d80fc2601d13` |

The setup script prepares the toolchain and Mbed TLS. The release wrapper downloads and hash-verifies ZeroMQ and lighttpd when their rebuild switches are used.

### Build and audit

The first build creates the pinned ZeroMQ and lighttpd dependency trees:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1 `
  -RebuildZmq -RebuildLighttpd
```

Later experimental builds reuse those trees:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1
```

Nightly and stable packages additionally require verified stock/native evidence summaries through `-PrintsvcStockSummary`, `-PrintsvcNativeSummary`, and, when applicable, `-PrintsvcNativeEvidenceSummary`. The native Linux equivalents are `--printsvc-stock-summary`, `--printsvc-native-summary`, and `--printsvc-native-evidence-summary`. Do not bypass those gates.

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1 `
  -ReleaseChannel nightly `
  -PrintsvcStockSummary C:\evidence\stock-summary `
  -PrintsvcNativeSummary C:\evidence\native-summary `
  -PrintsvcNativeEvidenceSummary C:\evidence\native-evidence-summary
```

The PowerShell wrapper remains available for Windows worktrees. A package
exists before all checks finish, so the file alone is not proof of success.
The same zero-exit, verification-message, and matching `.deneb.sha256` sidecar
contract applies before the
[verified USB handoff](https://deneb3d.dev/docs/updating/#verify-and-copy-the-update-package).

## Reproducible update packages

Both build lanes use the same package stage. The byte-for-byte check uses
Linux staging with ordinary POSIX mode handling and the same version, channel,
source files, member modes and compiled binaries. Inherited default ACLs and
Windows drive permission mapping can change the staged modes. The manifest
date and all tar member times use the committer timestamp of `HEAD` by
default, or the Unix epoch if Git history is unavailable.
Set `SOURCE_DATE_EPOCH` to a nonnegative Unix timestamp in seconds in the Linux
packaging environment to override both. The archive uses a fixed member order,
bytewise sorting for directory contents, and numeric owner and group IDs of zero.
Archive creation pins the GNU tar format and clears inherited `TAR_OPTIONS`.
The release wrappers run their archive audits, write the `.deneb.sbom.cdx.json`
CycloneDX file, then publish the checksum. The SBOM records the package SHA-256,
Deneb commit, LVGL revision, and the native dependencies, including the
statically linked musl, libstdc++, and libgcc runtimes. The build records
receipts after verifying the source and toolchain archives.
The SBOM includes a version and source hash only when the receipt matches the
current pin and the dependency's build outputs. Older caches and custom builds
without matching receipts are marked unverified. Static runtime provenance also
requires receipts that match the packaged link outputs. For a verified
toolchain, a missing or stale link receipt triggers a relink of the affected
binary while keeping compiled objects available for reuse. The selected CMake
compiler must match the verified toolchain before the build can record a link
receipt. Both release
wrappers enforce the MT7688 executable ABI even when provenance is unverified.
Rebuild ZeroMQ and lighttpd with the release wrapper's rebuild flags. To generate a receipt for a previously
cached Mbed TLS build, run the setup script with a fresh dependency build tree.

The repeat-package test uses identical prebuilt binaries. End-to-end
reproducibility across independent cross-compilations remains unverified.

## Stock menu compatibility audit

All `.deneb` build channels run the stock menu prune fixture from the repository
using synthetic inputs. Both stock-menu checks stay on the build host. When
changing which stock menu files Deneb retains or which services consume them,
run the separate import inventory against a local extraction of the stock
firmware version you are investigating.

```sh
bash tools/deneb-stock-menu-import-check.sh --repo-root /path/to/stock-workspace
```

Use the directory that contains `rootfs/home/cygnus`. Keep the extraction
outside Git and record the stock firmware version with your compatibility
evidence. The inventory checks readable source files for single-line imports
and literal `menu_settings.NAME` references. Check multiline imports, aliases,
and dynamic imports separately before removing retained files or constants.

## Verify either lane

For native Debian/Linux:

```sh
test -x build/deneb-cross/openwrt-25.12.6/toolchain-mipsel_24kc_gcc-14.3.0_musl/bin/mipsel-openwrt-linux-musl-gcc
test -f build/deneb-cross/openwrt-25.12.6/mbedtls-4.1.2-mipsel/lib/libmbedtls.a
test -f build/deneb-cross/openwrt-25.12.6/zeromq-4.3.5/build-musl/lib/libzmq.a
test -x build/deneb-cross/openwrt-25.12.6/lighttpd-1.4.85/build-musl-static/build/lighttpd
git submodule status --recursive
git status --short
```

For Windows/WSL:

```powershell
wsl -d Debian -u root -- bash -lc '/root/deneb-build/openwrt-25.12.6/toolchain-mipsel_24kc_gcc-14.3.0_musl/bin/mipsel-openwrt-linux-musl-gcc --version'
wsl -d Debian -u root -- bash -lc 'test -f /root/deneb-build/openwrt-25.12.6/mbedtls-4.1.2-mipsel/lib/libmbedtls.a'
wsl -d Debian -u root -- bash -lc 'test -f /root/deneb-build/openwrt-25.12.6/zeromq-4.3.5/build-musl/lib/libzmq.a'
wsl -d Debian -u root -- bash -lc 'test -x /root/deneb-build/openwrt-25.12.6/lighttpd-1.4.85/build-musl-static/build/lighttpd'
git submodule status --recursive
git status --short
```

## Windows/WSL backup and recovery

After the environment works, export it before major WSL or dependency changes:

```powershell
wsl --shutdown
wsl --export Debian C:\Backups\deneb-debian-wsl.tar
```

Never run `wsl --unregister Debian` without a verified export. If startup fails, try `wsl --shutdown`, `wsl --update`, a Windows reboot, and an elevated WSL service repair before replacing the distribution.
