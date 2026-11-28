#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Exercise setup-mbedtls.sh moving an earlier install aside after a pin
# change, offline. A curl stub stops setup right after the move, and mv and
# mkdir wrappers act at the moment setup calls them, as a concurrent process
# or an unusual filesystem would. With --overlay (run under `unshare -rm`),
# also check a real OverlayFS rename that fails with EXDEV.

# check() evaluates each single-quoted condition when it runs.
# shellcheck disable=SC2016

set -eu

setup=$(cd "$(dirname "$0")" && pwd)/setup-mbedtls.sh
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/bin"
real_mv=$(command -v mv)
real_mkdir=$(command -v mkdir)
export REAL_MV="$real_mv" REAL_MKDIR="$real_mkdir"

printf '#!/bin/sh\necho curl-stub >&2\nexit 22\n' > "$work/bin/curl"
# MV_ACT="<destination glob>=<action> ...": occupy puts a file in the
# destination first, fail exits 1 without moving, skip0 exits 0 without
# moving, doneerr moves and then exits 1. MV_NO_COPY=absent hides --no-copy.
cat > "$work/bin/mv" <<'STUB'
#!/bin/sh
if [ "${MV_NO_COPY:-}" = absent ]; then
    if [ "${1:-}" = --help ]; then "$REAL_MV" --help | grep -v -e '--no-copy'; exit; fi
    for arg do
        [ "$arg" != --no-copy ] || { echo "mv: unrecognized option '--no-copy'" >&2; exit 1; }
    done
fi
for last do :; done
for rule in ${MV_ACT:-}; do
    # shellcheck disable=SC2254
    case "$last" in
        ${rule%%=*})
            case "${rule#*=}" in
                occupy) "$REAL_MKDIR" -p "$last"; echo racer > "$last/racer" ;;
                fail) echo "mv-stub: failing $last" >&2; exit 1 ;;
                skip0) exit 0 ;;
                doneerr) "$REAL_MV" "$@"; echo "mv-stub: error after renaming $last" >&2; exit 1 ;;
            esac
            ;;
    esac
done
exec "$REAL_MV" "$@"
STUB
# MKDIR_ACT="<path glob> ...": create that empty directory just before
# setup's own mkdir of it.
cat > "$work/bin/mkdir" <<'STUB'
#!/bin/sh
for last do :; done
for glob in ${MKDIR_ACT:-}; do
    # shellcheck disable=SC2254
    case "$last" in ${glob}) "$REAL_MKDIR" "$last" ;; esac
done
exec "$REAL_MKDIR" "$@"
STUB
chmod +x "$work/bin/curl" "$work/bin/mv" "$work/bin/mkdir"

old=1.0.0
failed=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; sed 's/^/    /' "$work/out"; failed=1; }
check() {
    if eval "$2"; then pass "$1"; else fail "$1"; fi
}

# An intact earlier install at $1 with a receipt for Mbed TLS $old.
make_prefix() {
    "$real_mkdir" -p "$1/lib" "$1/include/mbedtls" "$1/include/psa" "$1/include/tf-psa-crypto"
    for file in lib/libmbedtls.a lib/libmbedx509.a lib/libtfpsacrypto.a \
        include/mbedtls/a.h include/psa/b.h include/tf-psa-crypto/c.h; do
        echo "$file" > "$1/$file"
    done
    (cd "$1" && {
        printf '%s\n' "$old" \
            "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$old/mbedtls-$old.tar.bz2" \
            0 0
        sha256sum lib/libmbedtls.a lib/libmbedx509.a lib/libtfpsacrypto.a \
            include/mbedtls/*.h include/psa/*.h include/tf-psa-crypto/*.h
    } > deneb-source.receipt)
}
# The same, plus its -source and -build siblings.
make_install() {
    rm -rf "$1" "$1-source" "$1-build" "$1".replaced-* "$1"-source.replaced-* "$1"-build.replaced-*
    make_prefix "$1"
    "$real_mkdir" -p "$1-source/sub" "$1-build"
    echo source > "$1-source/sub/s"
    echo build > "$1-build/b"
}
run_setup() {
    PATH="$work/bin:$PATH" sh "$setup" "$p" > "$work/out" 2>&1 || :
}
whole() {
    [ -f "$p/lib/libmbedtls.a" ] && [ -f "$p-source/sub/s" ] && [ -f "$p-build/b" ]
}
no_backups() {
    ! ls -d "$p".replaced-* "$p"-source.replaced-* "$p"-build.replaced-* > /dev/null 2>&1
}
said() { grep -qF -- "$1" "$work/out"; }

if [ "${1:-}" = --overlay ]; then
    # The layers live on a tmpfs mounted in this namespace. userxattr turns
    # redirect_dir off, so renaming the lower-layer -source fails with EXDEV.
    "$real_mkdir" "$work/fs"
    mount -t tmpfs tmpfs "$work/fs"
    "$real_mkdir" -p "$work/fs/lower/x-source/sub" "$work/fs/upper" "$work/fs/work" "$work/fs/m"
    echo source > "$work/fs/lower/x-source/sub/s"
    mount -t overlay overlay \
        -o "lowerdir=$work/fs/lower,upperdir=$work/fs/upper,workdir=$work/fs/work,userxattr" "$work/fs/m"
    p=$work/fs/m/x
    make_prefix "$p"
    "$real_mkdir" "$p-build"
    echo build > "$p-build/b"
    run_setup
    check "an EXDEV rename on OverlayFS leaves the earlier install whole" 'whole && no_backups'
    check "an EXDEV rename is reported as a failed move" "said 'left the earlier install in place'"
    umount "$work/fs/m"
    umount "$work/fs"
    exit "$failed"
fi

p=$work/x

make_install "$p"
run_setup
check "an earlier install moves aside" \
    '[ ! -e "$p" ] && [ -f "$p.replaced-$old/lib/libmbedtls.a" ] && [ -f "$p-source.replaced-$old/sub/s" ] && [ -f "$p-build.replaced-$old/b" ]'
check "the move aside is reported" "said 'Moved Mbed TLS $old aside'"

make_install "$p"
MV_NO_COPY=absent run_setup
check "an mv without --no-copy moves nothing" 'whole && no_backups'
check "an mv without --no-copy is reported" "said 'this mv has no --no-copy option'"

make_install "$p"
rm -rf "$p-build"
ln -s "$p-source" "$p-build"
run_setup
check "a symlinked path stops setup before any move" '[ -L "$p-build" ] && [ -f "$p/lib/libmbedtls.a" ] && no_backups'
check "a symlinked path is reported" "said '$p-build is not a directory'"
rm -f "$p-build"

make_install "$p"
MV_ACT="*-build.replaced-$old=occupy" run_setup
check "a backup path filled during the move keeps the install whole" 'whole && [ -f "$p-build.replaced-$old/racer" ]'
check "a failed move is reported" "said 'left the earlier install in place'"

make_install "$p"
MKDIR_ACT="*-source.replaced-$old" run_setup
check "a backup path created before the move keeps the install whole" \
    'whole && [ -d "$p-source.replaced-$old" ] && [ -z "$(ls -A "$p-source.replaced-$old")" ]'

make_install "$p"
MV_ACT="*-source.replaced-$old=skip0" run_setup
check "an mv that skips and exits 0 counts as a failed move" 'whole && no_backups'

make_install "$p"
MV_ACT="*-build.replaced-$old=doneerr" run_setup
check "an mv that moves and exits nonzero counts as a move" \
    '[ ! -e "$p-build" ] && [ -f "$p-build.replaced-$old/b" ] && said "Moved Mbed TLS $old aside"'

make_install "$p"
MV_ACT="*-build.replaced-$old=fail *-source=doneerr" run_setup
check "a restore that moves and exits nonzero counts as restored" \
    "whole && no_backups && said 'left the earlier install in place'"

make_install "$p"
MV_ACT="*-build.replaced-$old=fail" MKDIR_ACT="*/x" run_setup
check "a restore path recreated before the restore is not replaced" \
    '[ -d "$p" ] && [ -z "$(ls -A "$p")" ] && [ -f "$p.replaced-$old/lib/libmbedtls.a" ] && [ -f "$p-source/sub/s" ]'
check "a partial restore names the backup" \
    "said 'Could not restore $p; it is still at $p.replaced-$old' && said 'only partly restored'"

make_install "$p"
MV_ACT="*-build.replaced-$old=fail */x=occupy" run_setup
check "a restore path filled during the restore is not merged" \
    '[ -f "$p/racer" ] && [ ! -e "$p/lib" ] && [ -f "$p.replaced-$old/lib/libmbedtls.a" ]'

# Run the OverlayFS cases only where this user may mount tmpfs and overlay in
# a private namespace; some hosts restrict unprivileged user namespaces.
overlay_available() {
    "$real_mkdir" "$work/probe"
    unshare -rm sh -c '
        mount -t tmpfs tmpfs "$1" &&
        mkdir "$1/l" "$1/u" "$1/w" "$1/m" &&
        mount -t overlay overlay -o "lowerdir=$1/l,upperdir=$1/u,workdir=$1/w,userxattr" "$1/m"
    ' sh "$work/probe" > /dev/null 2>&1
}
if overlay_available; then
    if unshare -rm sh "$0" --overlay > "$work/overlay" 2>&1; then
        cat "$work/overlay"
    else
        cat "$work/overlay"
        grep -q '^FAIL' "$work/overlay" || echo "FAIL: the OverlayFS cases could not run"
        failed=1
    fi
else
    echo "SKIP: OverlayFS EXDEV cases (no unprivileged tmpfs and overlay mounts here)"
fi

if [ "$failed" -ne 0 ]; then
    echo "Mbed TLS setup self-test: FAIL"
    exit 1
fi
echo "Mbed TLS setup self-test: PASS"
