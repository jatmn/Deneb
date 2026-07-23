# Deneb Debian/Linux Build Environment

Deneb release builds run on Debian or another Debian-compatible Linux host. The target uses Linux/POSIX APIs and a MIPS little-endian musl cross-compiler; MSVC is not a release-build substitute. Windows users may use the same Debian environment through WSL 2, but WSL is not required.

Native build dependencies are stored under the checkout's ignored `build/deneb-cross/` directory and all setup and package builds run as the checkout user. Only installing missing Debian host packages requires `sudo`. The Windows/WSL wrapper retains its isolated `/root` dependency layout.

## Choose a build lane

| Host checkout | Setup command | Release command |
| --- | --- | --- |
| Native Debian/Linux | `bash tools/setup-linux-build.sh "$PWD"` | `bash tools/build-update-release.sh` |
| Windows checkout with Debian WSL 2 | PowerShell invoking `tools/setup-wsl-build.sh` in WSL | `tools/build-update-release.ps1` |

Do not mix the lanes: the native scripts keep dependencies in the checkout's
`build/deneb-cross/` directory, while the PowerShell wrapper uses `/root`
inside the WSL distribution.

## Lane 1: native Debian/Linux

Install Git, clone the repository, and initialize its pinned submodules:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential ca-certificates cmake curl file git make pkg-config python3 tar xz-utils
git clone --recurse-submodules https://github.com/jatmn/Deneb.git
cd Deneb
```

Prepare the pinned cross-toolchain and mbedTLS dependency, then create and audit an experimental package:

```sh
bash tools/setup-linux-build.sh "$PWD"
bash tools/build-update-release.sh --rebuild-zmq --rebuild-lighttpd
```

Later experimental builds reuse those dependency trees:

```sh
bash tools/build-update-release.sh
```

The script cross-builds all native services, packages `dist/Deneb_Update_<git-describe>.deneb`, and audits the archive. Trust a package only after it prints `Verified native-only print service package` and exits zero.

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
| musl.cc MIPS little-endian cross-toolchain | pinned archive | `82626533bf7e677c225e7cbedf1d5b0d6bc60c3daaf28249e54f0eb805d89b13` |
| mbedTLS | 2.28.8 | `4fef7de0d8d542510d726d643350acb3cdb9dc76ad45611b59c9aa08372b4213` |
| ZeroMQ | 4.3.5 | `6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43` |
| lighttpd | 1.4.76 | `8cbf4296e373cfd0cedfe9d978760b5b05c58fdc4048b4e2bcaf0a61ac8f5011` |

The setup script prepares the toolchain and mbedTLS. The release wrapper downloads and hash-verifies ZeroMQ and lighttpd when their rebuild switches are used.

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

The PowerShell wrapper remains available for Windows worktrees. A package exists before all checks finish, so the file alone is not proof of success.

## Verify either lane

For native Debian/Linux:

```sh
test -x build/deneb-cross/mipsel-linux-musl-cross/bin/mipsel-linux-musl-gcc
test -f build/deneb-cross/mbedtls-2.28.8-mipsel/lib/libmbedtls.a
test -f build/deneb-cross/zeromq-4.3.5/build-musl/lib/libzmq.a
test -x build/deneb-cross/lighttpd-1.4.76/build-musl-static/build/lighttpd
git submodule status --recursive
git status --short
```

For Windows/WSL:

```powershell
wsl -d Debian -u root -- bash -lc '/root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-gcc --version'
wsl -d Debian -u root -- bash -lc 'test -f /root/deneb-build/mbedtls-2.28.8-mipsel/lib/libmbedtls.a'
wsl -d Debian -u root -- bash -lc 'test -f /root/deneb-build/zeromq-4.3.5/build-musl/lib/libzmq.a'
wsl -d Debian -u root -- bash -lc 'test -x /root/deneb-build/lighttpd-1.4.76/build-musl-static/build/lighttpd'
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
