#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Negative fixtures for deneb-printsvc-native-audit.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TMP_DIR="${TMPDIR:-/tmp}/deneb-native-audit-selftest.$$"
AUDIT="${SCRIPT_DIR}/deneb-printsvc-native-audit.sh"

if [ ! -f "$AUDIT" ]; then
    AUDIT="${SCRIPT_DIR}/deneb-printsvc-native-audit"
fi

cleanup() {
    rm -rf "$TMP_DIR"
}

trap cleanup EXIT HUP INT TERM
mkdir -p "$TMP_DIR"

expect_failure() {
    label="$1"
    shift
    if "$@" > "$TMP_DIR/$label.out" 2>&1; then
        cat "$TMP_DIR/$label.out"
        echo "FAIL: expected native audit failure: $label" >&2
        exit 1
    fi
    echo "PASS: expected native audit failure: $label"
}

write_valid_package() {
    root="$1"
    mkdir -p "$root/deneb-printsvc-macros"
    touch "$root/deneb-printsvc" \
        "$root/deneb-printsvc-smoke" \
        "$root/deneb-printsvc-smoke-verify" \
        "$root/deneb-printsvc-smoke-compare" \
        "$root/deneb-printsvc-native-audit" \
        "$root/deneb-printsvc-native-audit-selftest"
    cat > "$root/manifest.txt" <<'EOF'
package: Deneb_Update_test
version: test
channel: experimental
native_printsvc: experimental
native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction
contents:
  no Python driver files are packaged; native deneb-printsvc owns marlindriver
EOF
}

write_valid_source() {
    root="$1"
    mkdir -p "$root/common/print" \
        "$root/ui/src" "$root/ui/installer" "$root/ui/init" \
        "$root/web/src" "$root/web/init" \
        "$root/printsvc/src" "$root/printsvc/init"

    cat > "$root/common/print/print_backend_route.c" <<'EOF'
#include "print_backend_route.h"
deneb_print_backend_route_t deneb_print_backend_route_default(void) {
    return deneb_print_backend_route(DENEB_PRINT_BACKEND_NATIVE);
}
EOF
    cat > "$root/common/print/print_backend_route.h" <<'EOF'
#define DENEB_PRINT_BACKEND_NATIVE 0
#define DENEB_PRINTSVC_STATUS_URL "tcp://127.0.0.1:5555"
#define DENEB_PRINTSVC_COMMAND_URL "tcp://127.0.0.1:5556"
typedef struct { int backend; } deneb_print_backend_route_t;
deneb_print_backend_route_t deneb_print_backend_route(int backend);
EOF
    cat > "$root/ui/build-package.sh" <<'EOF'
#!/bin/sh
deneb-printsvc-native-audit --source .
deneb-printsvc-native-audit-selftest
find "$STAGING_DIR" \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' \)
EOF
    cat > "$root/ui/installer/update.sh" <<'EOF'
#!/bin/sh
deneb-printsvc-native-audit --package-dir /tmp/update
deneb-printsvc-native-audit-selftest
native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction
EOF
    cat > "$root/printsvc/init/deneb-printsvc.init" <<'EOF'
#!/bin/sh /etc/rc.common
start() { /usr/bin/deneb-printsvc; }
EOF
    touch "$root/ui/src/native_client.c" \
        "$root/ui/init/deneb-ui.init" \
        "$root/web/src/native_client.c" \
        "$root/web/init/deneb-api.init" \
        "$root/printsvc/src/native_service.c"
}

VALID="$TMP_DIR/valid"
write_valid_package "$VALID"
"$AUDIT" --package-dir "$VALID" >/tmp/deneb-native-audit-selftest-valid.log
echo "PASS: valid package fixture accepted"

VALID_SOURCE="$TMP_DIR/source-valid"
write_valid_source "$VALID_SOURCE"
"$AUDIT" --source "$VALID_SOURCE" >/tmp/deneb-native-audit-selftest-source-valid.log
echo "PASS: valid source fixture accepted"

SOURCE_STOCK_SELECTOR="$TMP_DIR/source-stock-selector"
write_valid_source "$SOURCE_STOCK_SELECTOR"
cat >> "$SOURCE_STOCK_SELECTOR/common/print/print_backend_route.c" <<'EOF'
/* Regression fixture: DENEB_PRINT_BACKEND_STOCK must stay rejected. */
EOF
expect_failure rejects_source_stock_selector "$AUDIT" --source "$SOURCE_STOCK_SELECTOR"

SOURCE_PYTHON_LAUNCH="$TMP_DIR/source-python-launch"
write_valid_source "$SOURCE_PYTHON_LAUNCH"
cat >> "$SOURCE_PYTHON_LAUNCH/ui/src/native_client.c" <<'EOF'
const char *bad_driver = "/usr/bin/python3 /home/cygnus/marlindriver/print_service.py";
EOF
expect_failure rejects_source_python_launcher "$AUDIT" --source "$SOURCE_PYTHON_LAUNCH"

SOURCE_MISSING_PACKAGE_AUDIT="$TMP_DIR/source-missing-package-audit"
write_valid_source "$SOURCE_MISSING_PACKAGE_AUDIT"
grep -v 'deneb-printsvc-native-audit' "$SOURCE_MISSING_PACKAGE_AUDIT/ui/build-package.sh" > "$SOURCE_MISSING_PACKAGE_AUDIT/ui/build-package.tmp"
mv "$SOURCE_MISSING_PACKAGE_AUDIT/ui/build-package.tmp" "$SOURCE_MISSING_PACKAGE_AUDIT/ui/build-package.sh"
expect_failure rejects_source_missing_package_audit "$AUDIT" --source "$SOURCE_MISSING_PACKAGE_AUDIT"

SOURCE_MISSING_INSTALLER_SELFTEST="$TMP_DIR/source-missing-installer-selftest"
write_valid_source "$SOURCE_MISSING_INSTALLER_SELFTEST"
grep -v 'deneb-printsvc-native-audit-selftest' "$SOURCE_MISSING_INSTALLER_SELFTEST/ui/installer/update.sh" > "$SOURCE_MISSING_INSTALLER_SELFTEST/ui/installer/update.tmp"
mv "$SOURCE_MISSING_INSTALLER_SELFTEST/ui/installer/update.tmp" "$SOURCE_MISSING_INSTALLER_SELFTEST/ui/installer/update.sh"
expect_failure rejects_source_missing_installer_selftest "$AUDIT" --source "$SOURCE_MISSING_INSTALLER_SELFTEST"

MISSING_AUDIT="$TMP_DIR/missing-audit"
write_valid_package "$MISSING_AUDIT"
rm -f "$MISSING_AUDIT/deneb-printsvc-native-audit"
expect_failure rejects_missing_audit "$AUDIT" --package-dir "$MISSING_AUDIT"

DRIVER_ARTIFACT="$TMP_DIR/driver-artifact"
write_valid_package "$DRIVER_ARTIFACT"
touch "$DRIVER_ARTIFACT/print_service.py"
expect_failure rejects_driver_artifact "$AUDIT" --package-dir "$DRIVER_ARTIFACT"

MISSING_GATE="$TMP_DIR/missing-gate"
write_valid_package "$MISSING_GATE"
grep -v '^native_printsvc_release_gate:' "$MISSING_GATE/manifest.txt" > "$MISSING_GATE/manifest.tmp"
mv "$MISSING_GATE/manifest.tmp" "$MISSING_GATE/manifest.txt"
expect_failure rejects_missing_manifest_gate "$AUDIT" --package-dir "$MISSING_GATE"

ARCHIVE_DIR="$TMP_DIR/archive-bad"
write_valid_package "$ARCHIVE_DIR"
touch "$ARCHIVE_DIR/stock_driver.py"
(cd "$ARCHIVE_DIR" && tar cf "$TMP_DIR/archive-bad.deneb" .)
expect_failure rejects_archive_driver_artifact "$AUDIT" --archive "$TMP_DIR/archive-bad.deneb"

echo "deneb-printsvc native audit selftest passed"
