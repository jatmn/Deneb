#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# Exercise both release wrappers and stop the package builder at its evidence
# comparator. Build tools and evidence checks are fixture doubles.

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
fixture="$scratch/repo"
build="$scratch/cross"
mkdir -p "$fixture"/{tools,ui,web,printsvc,dfsvc} "$scratch/bin" "$scratch/evidence with spaces"
cp "$repo_root"/tools/*.sh "$fixture/tools/"
cp "$repo_root/ui/build-package.sh" "$fixture/ui/"
cp "$repo_root/web/lighttpd.conf" "$fixture/web/"
for component in web printsvc dfsvc; do
    cp -r "$repo_root/$component/init" "$fixture/$component/"
done
# Adapt only the checkout location on Linux. Supplemental paths are already WSL paths.
# shellcheck disable=SC2016
sed 's/^\$repoWsl = ConvertTo-WslPath \$repoRoot$/\$repoWsl = \$repoRoot/' \
    "$repo_root/tools/build-update-release.ps1" > "$fixture/tools/build-update-release.ps1"

mkdir -p "$build/toolchain-mipsel_24kc_gcc-14.3.0_musl/bin" \
    "$build/zeromq-4.3.5/build-musl/lib" "$build/zeromq-4.3.5/include" \
    "$build/lighttpd-1.4.85/src" "$build/lighttpd-1.4.85/build-musl-static/build" \
    "$fixture"/{ui,web,printsvc,dfsvc}/build-musl-openwrt
touch "$build/zeromq-4.3.5/build-musl/lib/libzmq.a"
for binary in "$build/toolchain-mipsel_24kc_gcc-14.3.0_musl/bin/mipsel-openwrt-linux-musl-gcc" \
    "$build/lighttpd-1.4.85/build-musl-static/build/lighttpd" \
    "$fixture/ui/build-musl-openwrt/deneb-ui" "$fixture/web/build-musl-openwrt/deneb-api" \
    "$fixture/web/build-musl-openwrt/deneb-mdns" "$fixture/printsvc/build-musl-openwrt/deneb-printsvc" \
    "$fixture/dfsvc/build-musl-openwrt/deneb-dfsvc" "$scratch/bin/cmake" "$scratch/bin/make"; do
    printf '#!/bin/sh\nexit 0\n' > "$binary"
    chmod +x "$binary"
done

cat > "$scratch/bin/wsl" <<'EOF'
#!/bin/sh
set -eu
if [ "$5" = --exec ] && [ "$6" = sh ]; then
    shift 5
    FIXTURE_WSL_ROOT=1 exec "$@"
fi
[ "$#" -eq 8 ] && [ "$6" = bash ] && [ "$7" = -lc ] || exit 2
case "$8" in
    *'deneb-release-package-files.txt'*) [ "$5" = --exec ] && [ "${FIXTURE_PUBLICATION:-0}" = 1 ] ;;
    *'tools/write-package-checksum.sh'*) [ "$5" = --exec ] && exec bash -c "$8" ;;
    *'test -x '*'/lighttpd'*) [ "$5" = --exec ] ;;
    *'bash ui/build-package.sh '*)
        case "$5" in
            # Model the default shell parsing a double-quoted Windows argument.
            --) exec sh -c "bash -c \"$8\"" ;;
            --exec) exec bash -c "$8" ;;
            *) exit 2 ;;
        esac
        ;;
    *) [ "$5" = -- ] ;;
esac
EOF
cat > "$fixture/tools/deneb-printsvc-smoke-verify.sh" <<'EOF'
#!/bin/sh
set -eu
printf 'verify:%s\n' "$@" >> "$EVIDENCE_CALLS"
for arg do
    if [ "$arg" = "${INVALID_EVIDENCE:-}" ]; then
        echo "Rejected invalid fixture evidence: $arg" >&2
        exit 23
    fi
done
EOF
cat > "$fixture/tools/deneb-printsvc-smoke-compare.sh" <<'EOF'
#!/bin/sh
set -eu
printf 'compare:%s\n' "$@" >> "$EVIDENCE_CALLS"
echo 'Stopped at fixture comparator' >&2
exit 23
EOF
chmod +x "$scratch/bin/wsl"
export PATH="$scratch/bin:$PATH"
unset DENEB_PRINTSVC_NATIVE_EVIDENCE_SUMMARIES
export DENEB_BUILD_ROOT="$build"
export EVIDENCE_CALLS="$scratch/calls"
export FIXTURE_REPO="$fixture"
export FIXTURE_BUILD="$build"
export STOCK_SUMMARY="$scratch/stock.summary"
export NATIVE_SUMMARY="$scratch/native.summary"
export FIRST_EVIDENCE="$scratch/evidence with spaces/extra \$HOME.summary"
export SECOND_EVIDENCE="$scratch/second's.summary"
touch "$STOCK_SUMMARY" "$NATIVE_SUMMARY" "$FIRST_EVIDENCE" "$SECOND_EVIDENCE"

run_linux() {
    local -a evidence_args=()
    local summary
    for summary; do
        evidence_args+=(--printsvc-native-evidence-summary "$summary")
    done
    bash "$fixture/tools/build-update-release.sh" --release-channel nightly \
        --printsvc-stock-summary "$STOCK_SUMMARY" --printsvc-native-summary "$NATIVE_SUMMARY" \
        "${evidence_args[@]}"
}

run_powershell() {
    # shellcheck disable=SC2016
    pwsh -NoProfile -Command '
        $PSNativeCommandArgumentPassing = "Legacy"
        & "$env:FIXTURE_REPO/tools/build-update-release.ps1" -ReleaseChannel stable `
            -ZmqRoot "$env:FIXTURE_BUILD/zeromq-4.3.5" -LighttpdRoot "$env:FIXTURE_BUILD/lighttpd-1.4.85" `
            -PrintsvcStockSummary $env:STOCK_SUMMARY -PrintsvcNativeSummary $env:NATIVE_SUMMARY `
            -PrintsvcNativeEvidenceSummary @($env:FIRST_EVIDENCE, $env:SECOND_EVIDENCE)
    '
}

check_failure() {
    local marker=$1
    shift
    : > "$EVIDENCE_CALLS"
    if "$@" > "$scratch/output" 2>&1 || ! grep -Fq -- "$marker" "$scratch/output"; then
        cat "$scratch/output" >&2
        printf 'Expected package builder to stop at %s\n' "$marker" >&2
        exit 1
    fi
    test -z "$(find "$fixture" -name '*.deneb' -print -quit)"
    diff -u "$scratch/expected" "$EVIDENCE_CALLS"
}

with_evidence() {
    printf 'verify:%s\n' --stock --resources "$STOCK_SUMMARY"
    printf 'verify:%s\n' --native --idle --restart --boot-sync --client-proof \
        --firmware-proof --complete-job --resources "$NATIVE_SUMMARY"
    printf 'verify:%s\n' --native "$FIRST_EVIDENCE" --native "$SECOND_EVIDENCE"
}

{
    with_evidence
    printf 'compare:%s\n' --require-reduction "$STOCK_SUMMARY" "$NATIVE_SUMMARY" \
        "$FIRST_EVIDENCE" "$SECOND_EVIDENCE"
} > "$scratch/expected"
check_failure 'Stopped at fixture comparator' run_linux "$FIRST_EVIDENCE" "$SECOND_EVIDENCE"
echo 'PASS: Linux wrapper preserves supplemental verifier and comparator arguments'
check_failure 'Stopped at fixture comparator' run_powershell
echo 'PASS: PowerShell wrapper preserves supplemental verifier and comparator arguments'

with_evidence > "$scratch/expected"
INVALID_EVIDENCE=$SECOND_EVIDENCE check_failure "Rejected invalid fixture evidence: $SECOND_EVIDENCE" \
    run_linux "$FIRST_EVIDENCE" "$SECOND_EVIDENCE"
: > "$scratch/expected"
missing="$scratch/evidence with spaces/missing.summary"
check_failure "native printsvc evidence summary not found: $missing" run_linux "$FIRST_EVIDENCE" "$missing"
DENEB_PRINTSVC_NATIVE_EVIDENCE_SUMMARIES=$FIRST_EVIDENCE \
    check_failure 'pass supplemental native evidence paths as arguments after the six binaries' run_linux

{
    printf 'verify:%s\n' --stock --resources "$STOCK_SUMMARY" --full "$NATIVE_SUMMARY"
    printf 'compare:%s\n' --require-reduction "$STOCK_SUMMARY" "$NATIVE_SUMMARY"
} > "$scratch/expected"
check_failure 'Stopped at fixture comparator' run_linux
echo 'PASS: missing, invalid, empty, and legacy supplemental evidence handling'

# Exercise the WSL wrapper's real metadata write after a fixture archive audit.
# Host Git can see the checkout; Git under the WSL stand-in rejects it.
export FIXTURE_HOST_GIT
FIXTURE_HOST_GIT=$(command -v git)
cat > "$scratch/bin/git" <<'EOF'
#!/bin/sh
if [ "${FIXTURE_WSL_ROOT:-0}" = 1 ]; then
    echo 'fixture: Git refuses a checkout owned by another user' >&2
    exit 125
fi
exec "$FIXTURE_HOST_GIT" "$@"
EOF
chmod +x "$scratch/bin/git"
mkdir -p "$fixture/ui/lib/lvgl" "$fixture/dist"
git -C "$fixture/ui/lib/lvgl" init -q
git -C "$fixture/ui/lib/lvgl" -c user.name=Fixture -c user.email=fixture@example.invalid \
    commit --allow-empty --no-gpg-sign -qm LVGL
lvgl_revision=$(git -C "$fixture/ui/lib/lvgl" rev-parse HEAD)
git -C "$fixture" init -q
git -C "$fixture" update-index --add --cacheinfo "160000,$lvgl_revision,ui/lib/lvgl"
git -C "$fixture" -c user.name=Fixture -c user.email=fixture@example.invalid commit --no-gpg-sign -qm Deneb
revision=$(git -C "$fixture" rev-parse HEAD)
export FIXTURE_PUBLICATION=1
cat > "$fixture/ui/build-package.sh" <<'EOF'
package="$PWD/dist/Deneb_Update_fixture.deneb"
printf '%s\n' 'fixture archive' > "$package"
printf 'Package path (base64): '
printf '%s' "$package" | base64 -w0
printf '\n'
EOF
package="$fixture/dist/Deneb_Update_fixture.deneb"
mkdir "$package.sbom.cdx.json"
if run_powershell > "$scratch/output" 2>&1 ||
    ! grep -Fq 'release package SBOM generation failed' "$scratch/output"; then
    cat "$scratch/output" >&2
    echo 'Expected SBOM publication to fail at the fixture output directory' >&2
    exit 1
fi
test ! -e "$package.sha256"
rmdir "$package.sbom.cdx.json"
run_powershell
grep -Fq "\"version\": \"$revision\"" "$package.sbom.cdx.json"
grep -Fq "\"version\": \"$lvgl_revision\"" "$package.sbom.cdx.json"
test "$(grep -c '"value": "unverified"' "$package.sbom.cdx.json")" -eq 6
(cd "$fixture/dist" && sha256sum -c "Deneb_Update_fixture.deneb.sha256")
echo 'PASS: Windows checkout revisions survive WSL ownership and SBOM failure leaves no checksum'

# A receipt is reusable only for the matching pin, output and input header.
# shellcheck source=tools/native-dependency-pins.sh
# shellcheck disable=SC1091
. "$repo_root/tools/native-dependency-pins.sh"
zmq_root="$build/zeromq-$ZMQ_VERSION"
printf '%s\n' 'fixture header' > "$zmq_root/include/zmq.h"
record_native_dependency libzmq "$zmq_root"
native_dependency_verified libzmq "$zmq_root"
if ZMQ_VERSION=0.0.0 native_dependency_verified libzmq "$zmq_root"; then
    echo 'A receipt from a previous pin was accepted' >&2
    exit 1
fi
printf '%s\n' 'changed fixture header' > "$zmq_root/include/zmq.h"
if native_dependency_verified libzmq "$zmq_root"; then
    echo 'A modified dependency tree was accepted' >&2
    exit 1
fi
echo 'PASS: mismatched pins and modified dependency inputs invalidate receipts'

# A pinned toolchain receipt identifies the static runtimes in the SBOM.
toolchain_root="$build/$MIPS_TOOLCHAIN_MEMBER"
gcc_lib="$toolchain_root/lib/gcc/$MIPS_TOOLCHAIN_TARGET/$MIPS_TOOLCHAIN_GCC_VERSION"
mkdir -p "$gcc_lib"
touch "$toolchain_root/info.mk" \
    "$toolchain_root/bin/$MIPS_TOOLCHAIN_TARGET-gcc.bin" \
    "$toolchain_root/bin/$MIPS_TOOLCHAIN_TARGET-g++.bin" \
    "$toolchain_root/bin/$MIPS_TOOLCHAIN_TARGET-wrapper.sh" \
    "$toolchain_root/lib/libc.a" "$toolchain_root/lib/libstdc++.a" \
    "$toolchain_root/lib/libssp_nonshared.a" "$gcc_lib/libgcc.a" "$gcc_lib/libgcc_eh.a"
record_native_dependency mips-toolchain "$toolchain_root"
native_dependency_verified mips-toolchain "$toolchain_root"

write_fixture_sbom() {
    sh "$fixture/tools/write-native-sbom.sh" "$package" "$zmq_root" "$build/lighttpd-$LIGHTTPD_VERSION" \
        "$build/mbedtls-$MBEDTLS_VERSION-mipsel" "$toolchain_root" "$revision" "$lvgl_revision"
}
write_fixture_sbom
grep -Fq "$MIPS_TOOLCHAIN_URL" "$package.sbom.cdx.json"
grep -Fq "pkg:generic/musl@$MIPS_TOOLCHAIN_MUSL_VERSION" "$package.sbom.cdx.json"
grep -Fq "pkg:generic/libstdc%2B%2B@$MIPS_TOOLCHAIN_GCC_VERSION" "$package.sbom.cdx.json"
grep -Fq "pkg:generic/libgcc@$MIPS_TOOLCHAIN_GCC_VERSION" "$package.sbom.cdx.json"
printf '%s\n' 'changed fixture runtime' > "$gcc_lib/libgcc_eh.a"
write_fixture_sbom
if grep -Fq "$MIPS_TOOLCHAIN_URL" "$package.sbom.cdx.json"; then
    echo 'A modified static runtime retained pinned provenance in the SBOM' >&2
    exit 1
fi
echo 'PASS: runtime SBOM provenance requires the matching toolchain receipt and libraries'
