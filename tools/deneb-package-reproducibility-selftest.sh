#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Run the audited package stage twice with identical fixture binaries.

set -eu

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
PACKAGE_VERSION="reproducibility-selftest.$$"
STAGING_DIR="$REPO_ROOT/build/ui-package/Deneb_Update_$PACKAGE_VERSION"
ARCHIVE="$REPO_ROOT/dist/Deneb_Update_$PACKAGE_VERSION.deneb"
EPOCH=1704067200

cleanup() {
    rm -rf "$TMP_DIR" "$STAGING_DIR"
    rm -f "$ARCHIVE" "$ARCHIVE.sha256"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

mkdir -p "$TMP_DIR/bin"
set --
for member in deneb-ui deneb-api lighttpd deneb-mdns deneb-printsvc deneb-dfsvc; do
    binary="$TMP_DIR/bin/$member"
    printf 'fixture %s\n' "$member" > "$binary"
    set -- "$@" "$binary"
done

build_package() (
    run_umask=$1
    shift
    umask "$run_umask"
    if ! SOURCE_DATE_EPOCH="$EPOCH" TAR_OPTIONS=--format=posix DENEB_RELEASE_CHANNEL=experimental \
        DENEB_PACKAGE_VERSION_OVERRIDE="$PACKAGE_VERSION" STRIP=true \
        sh "$REPO_ROOT/ui/build-package.sh" "$@" > "$TMP_DIR/build.log" 2>&1; then
        cat "$TMP_DIR/build.log"
        exit 1
    fi
    sh "$REPO_ROOT/tools/write-package-checksum.sh" "$ARCHIVE"
    cd "$REPO_ROOT/dist"
    sha256sum -c "${ARCHIVE##*/}.sha256"
)

build_package 022 "$@"
cp "$ARCHIVE" "$TMP_DIR/first.deneb"
cp "$ARCHIVE.sha256" "$TMP_DIR/first.sha256"
sleep 1
build_package 077 "$@"
cmp "$TMP_DIR/first.deneb" "$ARCHIVE"
cmp "$TMP_DIR/first.sha256" "$ARCHIVE.sha256"

tar -xOf "$ARCHIVE" manifest.txt > "$TMP_DIR/manifest"
grep -Fx 'date: 2024-01-01T00:00:00Z' "$TMP_DIR/manifest"
LC_ALL=C TZ=UTC tar --numeric-owner --full-time -tvf "$ARCHIVE" > "$TMP_DIR/members"
awk '
    $2 != "0/0" || $4 != "2024-01-01" || $5 != "00:00:00" { bad = 1 }
    $6 == "update.sh" && $1 == "-rwxr-xr-x" { installer = 1 }
    $6 == "deneb-ui" && $1 == "-rwxr-xr-x" { executable = 1 }
    $6 == "manifest.txt" && $1 == "-rw-r--r--" { manifest = 1 }
    END { exit (bad || !installer || !executable || !manifest) }
' "$TMP_DIR/members"
printf '%s\n' 'PASS: repeat packaging preserves archive bytes, SHA-256, and member metadata'
