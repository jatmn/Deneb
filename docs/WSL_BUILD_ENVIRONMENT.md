# Deneb WSL Build Environment

Deneb's Windows release lane requires WSL 2 with Debian. The target uses Linux/POSIX APIs and a MIPS little-endian musl cross-compiler; MSVC is not a release-build substitute.

The PowerShell release wrapper always invokes `wsl -d <distro> -u root`. Debian's default user therefore does not need to be root. Build dependencies are intentionally isolated below `/root` inside the WSL distribution.

## Windows and WSL prerequisites

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

## One-time deterministic setup

From PowerShell at the repository root:

```powershell
$repo = '/mnt/' + $PWD.Drive.Name.ToLower() + $PWD.Path.Substring(2).Replace('\', '/')
wsl -d Debian -u root -- sh "$repo/tools/setup-wsl-build.sh" "$repo"
```

The setup script installs Debian host tools, refuses to overwrite incomplete dependency directories, and verifies archives before extracting them.

| Input | Version | SHA-256 |
| --- | --- | --- |
| musl.cc MIPS little-endian cross-toolchain | pinned archive | `82626533bf7e677c225e7cbedf1d5b0d6bc60c3daaf28249e54f0eb805d89b13` |
| mbedTLS | 2.28.8 | `4fef7de0d8d542510d726d643350acb3cdb9dc76ad45611b59c9aa08372b4213` |
| ZeroMQ | 4.3.5 | `6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43` |
| lighttpd | 1.4.76 | `8cbf4296e373cfd0cedfe9d978760b5b05c58fdc4048b4e2bcaf0a61ac8f5011` |

The setup script prepares the toolchain and mbedTLS. The release wrapper downloads and hash-verifies ZeroMQ and lighttpd when their rebuild switches are used.

## Build and audit a release package

The first build creates the pinned ZeroMQ and lighttpd dependency trees:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1 `
  -RebuildZmq -RebuildLighttpd
```

Later experimental builds reuse those trees:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1
```

Nightly and stable packages additionally require verified stock/native evidence summaries through `-PrintsvcStockSummary`, `-PrintsvcNativeSummary`, and, when applicable, `-PrintsvcNativeEvidenceSummary`. Do not bypass those gates.

The wrapper cross-builds all native services, packages `dist/Deneb_Update_<git-short-sha>.deneb`, and audits the archive. A package exists before all checks finish, so the file alone is not proof of success. Trust it only after the wrapper prints `Verified native-only print service package` and exits zero.

## Verification

```powershell
wsl -d Debian -u root -- bash -lc '/root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-gcc --version'
wsl -d Debian -u root -- bash -lc 'test -f /root/deneb-build/mbedtls-2.28.8-mipsel/lib/libmbedtls.a'
wsl -d Debian -u root -- bash -lc 'test -f /root/deneb-build/zeromq-4.3.5/build-musl/lib/libzmq.a'
wsl -d Debian -u root -- bash -lc 'test -x /root/deneb-build/lighttpd-1.4.76/build-musl-static/build/lighttpd'
git submodule status --recursive
git status --short
```

## Backup and recovery

After the environment works, export it before major WSL or dependency changes:

```powershell
wsl --shutdown
wsl --export Debian C:\Backups\deneb-debian-wsl.tar
```

Never run `wsl --unregister Debian` without a verified export. If startup fails, try `wsl --shutdown`, `wsl --update`, a Windows reboot, and an elevated WSL service repair before replacing the distribution.
