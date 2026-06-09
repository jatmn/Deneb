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
        "$root/deneb-printsvc-smoke-verify" \
        "$root/deneb-printsvc-smoke-compare" \
        "$root/deneb-printsvc-smoke-selftest" \
        "$root/deneb-printsvc-cli-selftest" \
        "$root/deneb-printsvc-init-selftest" \
        "$root/deneb-printsvc-release-gate-selftest" \
        "$root/deneb-printsvc-native-audit" \
        "$root/deneb-printsvc-native-audit-selftest" \
        "$root/LVGL_LICENSE_TLSF.txt"
    cat > "$root/deneb-printsvc-smoke" <<'EOF'
#!/bin/sh
PHYSICAL_OK="${DENEB_PRINTSVC_SMOKE_PHYSICAL_OK:-0}"
PHYSICAL_BUNDLE_OK="${DENEB_PRINTSVC_SMOKE_PHYSICAL_BUNDLE_OK:-0}"
guarded_prehome() {
    summary "phase=$1 action=z_home rc=0 reason=pre_physical_home"
}
physical_safety_plan() {
    summary "phase=$1-safety kind=physical axes=$axes required_home=$required_home travel=$travel stop_conditions=$stop_conditions rc=0"
}
summary "phase=physical-safety-gate rc=2 reason=missing_physical_ok"
summary "phase=physical-bundle-safety-gate rc=2 reason=missing_physical_bundle_ok count=2"
EOF
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
        "$root/printsvc/src" "$root/printsvc/init" \
        "$root/tools"

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
DENEB_RELEASE_CHANNEL="${DENEB_RELEASE_CHANNEL:-experimental}"
VERSION="${DENEB_PACKAGE_VERSION_OVERRIDE:-test}"
PRINTSVC_STOCK_SUMMARY="${DENEB_PRINTSVC_STOCK_SUMMARY:-}"
PRINTSVC_NATIVE_SUMMARY="${DENEB_PRINTSVC_NATIVE_SUMMARY:-}"
if [ "$DENEB_RELEASE_CHANNEL" != "experimental" ]; then
    deneb-printsvc-smoke-verify" --full "$PRINTSVC_NATIVE_SUMMARY"
    deneb-printsvc-smoke-compare" --require-reduction "$PRINTSVC_STOCK_SUMMARY" "$PRINTSVC_NATIVE_SUMMARY"
fi
deneb-printsvc-native-audit --source .
deneb-printsvc-native-audit-selftest
deneb-printsvc-release-gate-selftest
find "$STAGING_DIR" \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' \)
EOF
cat > "$root/tools/build-update-release.ps1" <<'EOF'
[ValidateSet("experimental", "nightly", "stable")]
$ReleaseChannel = "experimental"
if ($ReleaseChannel -ne "experimental") {
    throw "Non-experimental release builds require -PrintsvcStockSummary and -PrintsvcNativeSummary."
}
$buildPackageEnv += " DENEB_PRINTSVC_STOCK_SUMMARY='$printsvcStockSummaryWsl'"
$buildPackageEnv += " DENEB_PRINTSVC_NATIVE_SUMMARY='$printsvcNativeSummaryWsl'"
deneb-printsvc-release-gate-selftest
EOF
    cat > "$root/tools/deneb-printsvc-release-gate-selftest.sh" <<'EOF'
#!/bin/sh
PACKAGE_VERSION="release-gate-selftest.$$"
PACKAGE_STAGING="/tmp/Deneb_Update_${PACKAGE_VERSION}"
cleanup() {
    rm -rf "$PACKAGE_STAGING"
}
DENEB_PACKAGE_VERSION_OVERRIDE="$PACKAGE_VERSION"
nightly_invalid_native_summary
EOF
    cat > "$root/tools/deneb-printsvc-smoke.sh" <<'EOF'
#!/bin/sh
PHYSICAL_OK="${DENEB_PRINTSVC_SMOKE_PHYSICAL_OK:-0}"
PHYSICAL_BUNDLE_OK="${DENEB_PRINTSVC_SMOKE_PHYSICAL_BUNDLE_OK:-0}"
guarded_prehome() {
    summary "phase=$1 action=z_home rc=0 reason=pre_physical_home"
}
physical_safety_plan() {
    summary "phase=$1-safety kind=physical axes=$axes required_home=$required_home travel=$travel stop_conditions=$stop_conditions rc=0"
}
summary "phase=physical-safety-gate rc=2 reason=missing_physical_ok"
summary "phase=physical-bundle-safety-gate rc=2 reason=missing_physical_bundle_ok count=2"
EOF
    cat > "$root/tools/deneb-printsvc-smoke-selftest.sh" <<'EOF'
#!/bin/sh
smoke_requires_physical_ack
smoke_rejects_physical_bundle
verify_rejects_missing_physical_safety
EOF
cat > "$root/ui/installer/update.sh" <<'EOF'
#!/bin/sh
deneb-printsvc-native-audit --package-dir /tmp/update
deneb-printsvc-native-audit-selftest
deneb-printsvc-release-gate-selftest
/etc/init.d/deneb-api restart
/etc/init.d/deneb-web restart
cp /tmp/update/deneb-printsvc-release-gate-selftest /usr/bin/deneb-printsvc-release-gate-selftest
native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction
EOF
    cat > "$root/printsvc/init/deneb-printsvc.init" <<'EOF'
#!/bin/sh /etc/rc.common
start() { /usr/bin/deneb-printsvc; }
EOF
cat > "$root/web/init/deneb-web.init" <<'EOF'
#!/bin/sh /etc/rc.common
API_PROG=/usr/bin/deneb-api
API_SOCK=/var/run/deneb-api.sock
start_service() {
    procd_open_instance api
    procd_set_param command "$API_PROG"
    procd_close_instance
    [ -S "$API_SOCK" ] || true
    procd_open_instance web
    /usr/sbin/lighttpd -f /etc/deneb/lighttpd.conf -D
    procd_close_instance
}
EOF
    cat > "$root/web/init/deneb-api.init" <<'EOF'
#!/bin/sh /etc/rc.common
PROG=/usr/bin/deneb-api
start_service() {
    procd_open_instance
    procd_set_param command "$PROG"
    procd_close_instance
}
EOF
    cat > "$root/web/init/deneb-mdns.init" <<'EOF'
#!/bin/sh /etc/rc.common
PROG=/usr/bin/deneb-mdns
start_service() {
    local mdns_enabled=$(uci -q get deneb.mdns.enabled 2>/dev/null || echo "1")
    [ "$mdns_enabled" = "1" ] || return 0
    procd_open_instance
    procd_set_param command "$PROG"
    procd_close_instance
}
EOF
    touch "$root/ui/src/native_client.c" \
        "$root/ui/init/deneb-ui.init" \
        "$root/web/src/native_client.c" \
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

SOURCE_MISSING_STRICT_COMPARE="$TMP_DIR/source-missing-strict-compare"
write_valid_source "$SOURCE_MISSING_STRICT_COMPARE"
grep -v -- '--require-reduction' "$SOURCE_MISSING_STRICT_COMPARE/ui/build-package.sh" > "$SOURCE_MISSING_STRICT_COMPARE/ui/build-package.tmp"
mv "$SOURCE_MISSING_STRICT_COMPARE/ui/build-package.tmp" "$SOURCE_MISSING_STRICT_COMPARE/ui/build-package.sh"
expect_failure rejects_source_missing_strict_compare "$AUDIT" --source "$SOURCE_MISSING_STRICT_COMPARE"

SOURCE_MISSING_RELEASE_GATE_SELFTEST="$TMP_DIR/source-missing-release-gate-selftest"
write_valid_source "$SOURCE_MISSING_RELEASE_GATE_SELFTEST"
grep -v 'deneb-printsvc-release-gate-selftest' "$SOURCE_MISSING_RELEASE_GATE_SELFTEST/ui/build-package.sh" > "$SOURCE_MISSING_RELEASE_GATE_SELFTEST/ui/build-package.tmp"
mv "$SOURCE_MISSING_RELEASE_GATE_SELFTEST/ui/build-package.tmp" "$SOURCE_MISSING_RELEASE_GATE_SELFTEST/ui/build-package.sh"
expect_failure rejects_source_missing_release_gate_selftest "$AUDIT" --source "$SOURCE_MISSING_RELEASE_GATE_SELFTEST"

SOURCE_MISSING_VERSION_OVERRIDE="$TMP_DIR/source-missing-version-override"
write_valid_source "$SOURCE_MISSING_VERSION_OVERRIDE"
grep -v 'DENEB_PACKAGE_VERSION_OVERRIDE' "$SOURCE_MISSING_VERSION_OVERRIDE/ui/build-package.sh" > "$SOURCE_MISSING_VERSION_OVERRIDE/ui/build-package.tmp"
mv "$SOURCE_MISSING_VERSION_OVERRIDE/ui/build-package.tmp" "$SOURCE_MISSING_VERSION_OVERRIDE/ui/build-package.sh"
expect_failure rejects_source_missing_version_override "$AUDIT" --source "$SOURCE_MISSING_VERSION_OVERRIDE"

SOURCE_MISSING_GATE_ISOLATION="$TMP_DIR/source-missing-gate-isolation"
write_valid_source "$SOURCE_MISSING_GATE_ISOLATION"
grep -v 'DENEB_PACKAGE_VERSION_OVERRIDE="\$PACKAGE_VERSION"' "$SOURCE_MISSING_GATE_ISOLATION/tools/deneb-printsvc-release-gate-selftest.sh" > "$SOURCE_MISSING_GATE_ISOLATION/tools/deneb-printsvc-release-gate-selftest.tmp"
mv "$SOURCE_MISSING_GATE_ISOLATION/tools/deneb-printsvc-release-gate-selftest.tmp" "$SOURCE_MISSING_GATE_ISOLATION/tools/deneb-printsvc-release-gate-selftest.sh"
expect_failure rejects_source_missing_gate_isolation "$AUDIT" --source "$SOURCE_MISSING_GATE_ISOLATION"

SOURCE_MISSING_INVALID_SUMMARY_CHECK="$TMP_DIR/source-missing-invalid-summary-check"
write_valid_source "$SOURCE_MISSING_INVALID_SUMMARY_CHECK"
grep -v 'nightly_invalid_native_summary' "$SOURCE_MISSING_INVALID_SUMMARY_CHECK/tools/deneb-printsvc-release-gate-selftest.sh" > "$SOURCE_MISSING_INVALID_SUMMARY_CHECK/tools/deneb-printsvc-release-gate-selftest.tmp"
mv "$SOURCE_MISSING_INVALID_SUMMARY_CHECK/tools/deneb-printsvc-release-gate-selftest.tmp" "$SOURCE_MISSING_INVALID_SUMMARY_CHECK/tools/deneb-printsvc-release-gate-selftest.sh"
expect_failure rejects_source_missing_invalid_summary_check "$AUDIT" --source "$SOURCE_MISSING_INVALID_SUMMARY_CHECK"

SOURCE_MISSING_WEB_API_INSTANCE="$TMP_DIR/source-missing-web-api-instance"
write_valid_source "$SOURCE_MISSING_WEB_API_INSTANCE"
grep -v 'procd_open_instance api' "$SOURCE_MISSING_WEB_API_INSTANCE/web/init/deneb-web.init" > "$SOURCE_MISSING_WEB_API_INSTANCE/web/init/deneb-web.tmp"
mv "$SOURCE_MISSING_WEB_API_INSTANCE/web/init/deneb-web.tmp" "$SOURCE_MISSING_WEB_API_INSTANCE/web/init/deneb-web.init"
expect_failure rejects_source_missing_web_api_instance "$AUDIT" --source "$SOURCE_MISSING_WEB_API_INSTANCE"

SOURCE_MISSING_WEB_LIGHTTPD_INSTANCE="$TMP_DIR/source-missing-web-lighttpd-instance"
write_valid_source "$SOURCE_MISSING_WEB_LIGHTTPD_INSTANCE"
grep -v 'procd_open_instance web' "$SOURCE_MISSING_WEB_LIGHTTPD_INSTANCE/web/init/deneb-web.init" > "$SOURCE_MISSING_WEB_LIGHTTPD_INSTANCE/web/init/deneb-web.tmp"
mv "$SOURCE_MISSING_WEB_LIGHTTPD_INSTANCE/web/init/deneb-web.tmp" "$SOURCE_MISSING_WEB_LIGHTTPD_INSTANCE/web/init/deneb-web.init"
expect_failure rejects_source_missing_web_lighttpd_instance "$AUDIT" --source "$SOURCE_MISSING_WEB_LIGHTTPD_INSTANCE"

SOURCE_MISSING_WEB_API_SOCKET="$TMP_DIR/source-missing-web-api-socket"
write_valid_source "$SOURCE_MISSING_WEB_API_SOCKET"
grep -v '/var/run/deneb-api.sock' "$SOURCE_MISSING_WEB_API_SOCKET/web/init/deneb-web.init" > "$SOURCE_MISSING_WEB_API_SOCKET/web/init/deneb-web.tmp"
mv "$SOURCE_MISSING_WEB_API_SOCKET/web/init/deneb-web.tmp" "$SOURCE_MISSING_WEB_API_SOCKET/web/init/deneb-web.init"
expect_failure rejects_source_missing_web_api_socket "$AUDIT" --source "$SOURCE_MISSING_WEB_API_SOCKET"

SOURCE_WEB_ENABLE_GATE="$TMP_DIR/source-web-enable-gate"
write_valid_source "$SOURCE_WEB_ENABLE_GATE"
BAD_WEB_ENABLE_KEY=enabled
cat >> "$SOURCE_WEB_ENABLE_GATE/web/init/deneb-web.init" <<EOF
uci -q get deneb.web.${BAD_WEB_ENABLE_KEY}
EOF
expect_failure rejects_source_web_enable_gate "$AUDIT" --source "$SOURCE_WEB_ENABLE_GATE"

SOURCE_INSTALLER_WEB_ENABLE_GATE="$TMP_DIR/source-installer-web-enable-gate"
write_valid_source "$SOURCE_INSTALLER_WEB_ENABLE_GATE"
cat >> "$SOURCE_INSTALLER_WEB_ENABLE_GATE/ui/installer/update.sh" <<EOF
uci -q set deneb.web.${BAD_WEB_ENABLE_KEY}='1'
EOF
expect_failure rejects_source_installer_web_enable_gate "$AUDIT" --source "$SOURCE_INSTALLER_WEB_ENABLE_GATE"

SOURCE_MISSING_WRAPPER_PREFLIGHT="$TMP_DIR/source-missing-wrapper-preflight"
write_valid_source "$SOURCE_MISSING_WRAPPER_PREFLIGHT"
grep -v 'Non-experimental release builds require -PrintsvcStockSummary and -PrintsvcNativeSummary' \
    "$SOURCE_MISSING_WRAPPER_PREFLIGHT/tools/build-update-release.ps1" > \
    "$SOURCE_MISSING_WRAPPER_PREFLIGHT/tools/build-update-release.tmp"
mv "$SOURCE_MISSING_WRAPPER_PREFLIGHT/tools/build-update-release.tmp" \
    "$SOURCE_MISSING_WRAPPER_PREFLIGHT/tools/build-update-release.ps1"
expect_failure rejects_source_missing_wrapper_preflight "$AUDIT" --source "$SOURCE_MISSING_WRAPPER_PREFLIGHT"

SOURCE_MISSING_INSTALLER_SELFTEST="$TMP_DIR/source-missing-installer-selftest"
write_valid_source "$SOURCE_MISSING_INSTALLER_SELFTEST"
grep -v 'deneb-printsvc-native-audit-selftest' "$SOURCE_MISSING_INSTALLER_SELFTEST/ui/installer/update.sh" > "$SOURCE_MISSING_INSTALLER_SELFTEST/ui/installer/update.tmp"
mv "$SOURCE_MISSING_INSTALLER_SELFTEST/ui/installer/update.tmp" "$SOURCE_MISSING_INSTALLER_SELFTEST/ui/installer/update.sh"
expect_failure rejects_source_missing_installer_selftest "$AUDIT" --source "$SOURCE_MISSING_INSTALLER_SELFTEST"

SOURCE_MISSING_INSTALLER_RELEASE_GATE="$TMP_DIR/source-missing-installer-release-gate"
write_valid_source "$SOURCE_MISSING_INSTALLER_RELEASE_GATE"
grep -v 'deneb-printsvc-release-gate-selftest' "$SOURCE_MISSING_INSTALLER_RELEASE_GATE/ui/installer/update.sh" > "$SOURCE_MISSING_INSTALLER_RELEASE_GATE/ui/installer/update.tmp"
mv "$SOURCE_MISSING_INSTALLER_RELEASE_GATE/ui/installer/update.tmp" "$SOURCE_MISSING_INSTALLER_RELEASE_GATE/ui/installer/update.sh"
expect_failure rejects_source_missing_installer_release_gate "$AUDIT" --source "$SOURCE_MISSING_INSTALLER_RELEASE_GATE"

SOURCE_MISSING_INSTALLER_WEB_RESTART="$TMP_DIR/source-missing-installer-web-restart"
write_valid_source "$SOURCE_MISSING_INSTALLER_WEB_RESTART"
grep -v '/etc/init.d/deneb-api restart' "$SOURCE_MISSING_INSTALLER_WEB_RESTART/ui/installer/update.sh" |
    grep -v '/etc/init.d/deneb-web restart' > "$SOURCE_MISSING_INSTALLER_WEB_RESTART/ui/installer/update.tmp"
mv "$SOURCE_MISSING_INSTALLER_WEB_RESTART/ui/installer/update.tmp" "$SOURCE_MISSING_INSTALLER_WEB_RESTART/ui/installer/update.sh"
expect_failure rejects_source_missing_installer_web_restart "$AUDIT" --source "$SOURCE_MISSING_INSTALLER_WEB_RESTART"

MISSING_AUDIT="$TMP_DIR/missing-audit"
write_valid_package "$MISSING_AUDIT"
rm -f "$MISSING_AUDIT/deneb-printsvc-native-audit"
expect_failure rejects_missing_audit "$AUDIT" --package-dir "$MISSING_AUDIT"

MISSING_SMOKE_SELFTEST="$TMP_DIR/missing-smoke-selftest"
write_valid_package "$MISSING_SMOKE_SELFTEST"
rm -f "$MISSING_SMOKE_SELFTEST/deneb-printsvc-smoke-selftest"
expect_failure rejects_missing_smoke_selftest "$AUDIT" --package-dir "$MISSING_SMOKE_SELFTEST"

MISSING_PHYSICAL_GATE="$TMP_DIR/missing-physical-gate"
write_valid_package "$MISSING_PHYSICAL_GATE"
grep -v 'physical-safety-gate' "$MISSING_PHYSICAL_GATE/deneb-printsvc-smoke" > "$MISSING_PHYSICAL_GATE/deneb-printsvc-smoke.tmp"
mv "$MISSING_PHYSICAL_GATE/deneb-printsvc-smoke.tmp" "$MISSING_PHYSICAL_GATE/deneb-printsvc-smoke"
expect_failure rejects_missing_physical_gate "$AUDIT" --package-dir "$MISSING_PHYSICAL_GATE"

MISSING_PHYSICAL_BUNDLE_GATE="$TMP_DIR/missing-physical-bundle-gate"
write_valid_package "$MISSING_PHYSICAL_BUNDLE_GATE"
grep -v 'physical-bundle-safety-gate' "$MISSING_PHYSICAL_BUNDLE_GATE/deneb-printsvc-smoke" > "$MISSING_PHYSICAL_BUNDLE_GATE/deneb-printsvc-smoke.tmp"
mv "$MISSING_PHYSICAL_BUNDLE_GATE/deneb-printsvc-smoke.tmp" "$MISSING_PHYSICAL_BUNDLE_GATE/deneb-printsvc-smoke"
expect_failure rejects_missing_physical_bundle_gate "$AUDIT" --package-dir "$MISSING_PHYSICAL_BUNDLE_GATE"

MISSING_PREHOME_GATE="$TMP_DIR/missing-prehome-gate"
write_valid_package "$MISSING_PREHOME_GATE"
grep -v 'guarded_prehome' "$MISSING_PREHOME_GATE/deneb-printsvc-smoke" | \
    grep -v 'pre_physical_home' > "$MISSING_PREHOME_GATE/deneb-printsvc-smoke.tmp"
mv "$MISSING_PREHOME_GATE/deneb-printsvc-smoke.tmp" "$MISSING_PREHOME_GATE/deneb-printsvc-smoke"
expect_failure rejects_missing_prehome_gate "$AUDIT" --package-dir "$MISSING_PREHOME_GATE"

MISSING_PHYSICAL_PLAN="$TMP_DIR/missing-physical-plan"
write_valid_package "$MISSING_PHYSICAL_PLAN"
grep -v 'physical_safety_plan' "$MISSING_PHYSICAL_PLAN/deneb-printsvc-smoke" |
    grep -v 'axes=\$axes required_home=\$required_home travel=\$travel stop_conditions=\$stop_conditions' > "$MISSING_PHYSICAL_PLAN/deneb-printsvc-smoke.tmp"
mv "$MISSING_PHYSICAL_PLAN/deneb-printsvc-smoke.tmp" "$MISSING_PHYSICAL_PLAN/deneb-printsvc-smoke"
expect_failure rejects_missing_physical_plan "$AUDIT" --package-dir "$MISSING_PHYSICAL_PLAN"

MISSING_CLI_SELFTEST="$TMP_DIR/missing-cli-selftest"
write_valid_package "$MISSING_CLI_SELFTEST"
rm -f "$MISSING_CLI_SELFTEST/deneb-printsvc-cli-selftest"
expect_failure rejects_missing_cli_selftest "$AUDIT" --package-dir "$MISSING_CLI_SELFTEST"

MISSING_INIT_SELFTEST="$TMP_DIR/missing-init-selftest"
write_valid_package "$MISSING_INIT_SELFTEST"
rm -f "$MISSING_INIT_SELFTEST/deneb-printsvc-init-selftest"
expect_failure rejects_missing_init_selftest "$AUDIT" --package-dir "$MISSING_INIT_SELFTEST"

MISSING_RELEASE_GATE_SELFTEST="$TMP_DIR/missing-release-gate-selftest"
write_valid_package "$MISSING_RELEASE_GATE_SELFTEST"
rm -f "$MISSING_RELEASE_GATE_SELFTEST/deneb-printsvc-release-gate-selftest"
expect_failure rejects_missing_release_gate_selftest "$AUDIT" --package-dir "$MISSING_RELEASE_GATE_SELFTEST"

MISSING_TLSF_NOTICE="$TMP_DIR/missing-tlsf-notice"
write_valid_package "$MISSING_TLSF_NOTICE"
rm -f "$MISSING_TLSF_NOTICE/LVGL_LICENSE_TLSF.txt"
expect_failure rejects_missing_tlsf_notice "$AUDIT" --package-dir "$MISSING_TLSF_NOTICE"

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
