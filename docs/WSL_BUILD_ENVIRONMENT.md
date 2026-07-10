# Deneb WSL Build Environment

Deneb's Windows build lane requires WSL 2 with a Debian distribution. This is
not an optional convenience layer: the C targets use Linux/POSIX headers, shell
packaging tools, and a MIPS little-endian musl cross-compiler. Visual
Studio/MSVC is not a supported substitute.

The release wrapper currently also assumes that Debian's default WSL user is
`root`. It invokes `wsl -d Debian` without `-u root` and hard-codes build
dependencies below `/root`. Until the wrapper is made user-independent, a WSL
installation that defaults to an unprivileged user is not a compatible release
environment.

## 1. Windows prerequisites

- Windows 10 version 2004 or later, or Windows 11, with hardware virtualization
  enabled.
- Administrator access for installing or repairing WSL.
- Git and PowerShell on Windows.
- Internet access from Debian for the first dependency build.

Microsoft's current installation reference is
[Install WSL](https://learn.microsoft.com/windows/wsl/install), and the command
reference is [Basic commands for WSL](https://learn.microsoft.com/windows/wsl/basic-commands).

From an elevated PowerShell prompt:

```powershell
wsl --install -d Debian
wsl --update
wsl --status
wsl --list --verbose
```

Reboot Windows if the installer requests it. Confirm that Debian reports WSL
version 2 in `wsl --list --verbose`. Convert it if necessary:

```powershell
wsl --set-version Debian 2
```

## 2. Configure the required default user

Check the current default:

```powershell
wsl -d Debian -- whoami
```

The current release wrapper requires this to print `root`. If it does not,
configure Debian's `/etc/wsl.conf` with the following content, then terminate
and restart the distribution:

```ini
[user]
default=root
```

```powershell
wsl --terminate Debian
wsl -d Debian -- whoami
```

The configuration format is documented in Microsoft's
[Advanced settings configuration in WSL](https://learn.microsoft.com/windows/wsl/wsl-config).
Requiring a root default user is build-script debt, not a desirable general WSL
policy. A future wrapper should pass an explicit user or use configurable paths.

## 3. Install Debian host tools

Run in Debian as root:

```bash
apt-get update
apt-get install --no-install-recommends \
  build-essential ca-certificates cmake curl file git make pkg-config \
  tar xz-utils
git --version
cmake --version
curl --version
```

Initialize the repository's pinned submodules from Windows PowerShell:

```powershell
git submodule update --init --recursive
```

Keep the repository on the Windows filesystem, such as `C:\temp\Deneb`. The
wrapper converts drive-letter paths to `/mnt/<drive>/...`; UNC-only paths are
not supported.

## 4. Install the MIPS musl toolchain

The production toolchain file expects this exact location:

```text
/root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-gcc
```

Install the `mipsel-linux-musl-cross` archive from
[musl.cc](https://musl.cc/) in Debian:

```bash
cd /tmp
curl --fail --location --output mipsel-linux-musl-cross.tgz \
  https://musl.cc/mipsel-linux-musl-cross.tgz
test ! -e /root/mipsel-linux-musl-cross || {
  echo "Refusing to overwrite /root/mipsel-linux-musl-cross" >&2
  exit 1
}
tar xzf mipsel-linux-musl-cross.tgz
mv mipsel-linux-musl-cross /root/mipsel-linux-musl-cross
/root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-gcc --version
```

For a reproducible release environment, record the downloaded archive's
SHA-256 and retain the archive or mirror it in an approved dependency cache.
The project currently pins the target tuple and path but not an archive digest.

## 5. Build the missing mbedTLS prerequisite

`tools/build-update-release.ps1` expects a static mbedTLS 2.28.8 installation
at `/root/deneb-build/mbedtls-2.28.8-mipsel`, but it does not create it. This is
a real bootstrap gap; a fresh WSL environment must prepare it manually.

From the repository root in Debian, where `$PWD` is the WSL path to Deneb:

```bash
set -eu
repo="$PWD"
mkdir -p /root/deneb-build
cd /root/deneb-build
curl --fail --location --output mbedtls-2.28.8.tar.gz \
  https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v2.28.8.tar.gz
for path in mbedtls-2.28.8 mbedtls-2.28.8-build mbedtls-2.28.8-mipsel; do
  test ! -e "$path" || {
    echo "Refusing to overwrite /root/deneb-build/$path" >&2
    exit 1
  }
done
tar xzf mbedtls-2.28.8.tar.gz
cmake -S mbedtls-2.28.8 -B mbedtls-2.28.8-build \
  -DCMAKE_TOOLCHAIN_FILE="$repo/ui/cmake/mipsel-musl-toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_INSTALL_PREFIX=/root/deneb-build/mbedtls-2.28.8-mipsel \
  -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
  -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON
cmake --build mbedtls-2.28.8-build --parallel
cmake --install mbedtls-2.28.8-build
test -f /root/deneb-build/mbedtls-2.28.8-mipsel/lib/libmbedtls.a
test -f /root/deneb-build/mbedtls-2.28.8-mipsel/lib/libmbedx509.a
test -f /root/deneb-build/mbedtls-2.28.8-mipsel/lib/libmbedcrypto.a
```

The source release is maintained by
[Mbed TLS](https://github.com/Mbed-TLS/mbedtls/releases/tag/v2.28.8). As with
the musl toolchain, retain and hash the exact source archive used for releases.

## 6. First release build

From Windows PowerShell in the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1 `
  -RebuildZmq -RebuildLighttpd
```

This first run downloads and builds the wrapper's pinned ZeroMQ 4.3.5 and
lighttpd 1.4.76 sources under `/root/deneb-build`, then cross-builds
`deneb-api`, `deneb-mdns`, `deneb-printsvc`, `deneb-dfsvc`, and `deneb-ui`.
It packages and audits:

```text
dist/Deneb_Update_<git-short-sha>.deneb
```

The rebuild switches clear only the wrapper's exact versioned dependency leaf
directories. Do not point `-ZmqRoot` or `-LighttpdRoot` at a directory containing
unrelated data.

Later experimental builds reuse the dependency trees:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1
```

Nightly and stable packages additionally require verified stock/native evidence
summaries through `-PrintsvcStockSummary`, `-PrintsvcNativeSummary`, and, where
needed, `-PrintsvcNativeEvidenceSummary`. Do not bypass those release gates.

## 7. Verification checklist

Before trusting a build environment:

```powershell
wsl -d Debian -- whoami
wsl -d Debian -- bash -lc '/root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-gcc --version'
wsl -d Debian -- bash -lc 'test -f /root/deneb-build/zeromq-4.3.5/build-musl/lib/libzmq.a'
wsl -d Debian -- bash -lc 'test -x /root/deneb-build/lighttpd-1.4.76/build-musl-static/build/lighttpd'
wsl -d Debian -- bash -lc 'test -f /root/deneb-build/mbedtls-2.28.8-mipsel/lib/libmbedtls.a'
git submodule status --recursive
git status --short
```

The release wrapper must finish its archive audit and print
`Verified native-only print service package`. A generated package alone is not
proof that verification completed.

## 8. Backup and recovery

Once the environment works, export it before major Windows, WSL, or dependency
changes:

```powershell
wsl --shutdown
wsl --export Debian C:\Backups\deneb-debian-wsl.tar
```

Do not run `wsl --unregister Debian` unless a verified export exists; unregister
deletes that distribution and all `/root/deneb-build` dependencies.

Useful non-destructive recovery commands are:

```powershell
wsl --status
wsl --list --verbose
wsl --shutdown
wsl --update
```

If the WSL service is unhealthy, restart Windows first. An elevated service
restart may also be appropriate, but do not reinstall or unregister Debian as a
first response.

## Current workstation note (2026-07-10)

On the workstation used for the 2026-07-10 hardware audit, both `wsl --status`
and `wsl --list --verbose` failed at distribution enumeration with:

```text
Wsl/EnumerateDistros/Service/E_ACCESSDENIED
```

`WslService` was running, but the current session did not have permission to
repair it. Therefore current-source MIPS binaries could not be built or deployed
from that workstation. This is an environment failure, not evidence that the
source builds successfully, and it must remain visible until a clean WSL build
passes the verification checklist above.
