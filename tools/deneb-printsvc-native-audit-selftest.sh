#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Negative fixtures for deneb-printsvc-native-audit.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TMP_DIR="${TMPDIR:-/tmp}/deneb-native-audit-selftest.$$"
AUDIT="${SCRIPT_DIR}/deneb-printsvc-native-audit.sh"
RUNTIME_INVENTORY="${SCRIPT_DIR}/deneb-python-runtime-inventory.sh"

if [ ! -f "$AUDIT" ]; then
    AUDIT="${SCRIPT_DIR}/deneb-printsvc-native-audit"
fi

if [ ! -f "$RUNTIME_INVENTORY" ]; then
    RUNTIME_INVENTORY="${SCRIPT_DIR}/deneb-runtime-inventory"
fi

cleanup() {
    rm -rf "$TMP_DIR"
}

trap cleanup EXIT HUP INT TERM
mkdir -p "$TMP_DIR"

expect_failure() {
    label="$1"
    shift
    if sh "$@" > "$TMP_DIR/$label.out" 2>&1; then
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
        "$root/deneb-dfsvc" \
        "$root/digitalfactory.init" \
        "$root/deneb-printsvc-smoke-verify" \
        "$root/deneb-printsvc-smoke-compare" \
        "$root/deneb-printsvc-smoke-selftest" \
        "$root/deneb-printsvc-stability" \
        "$root/deneb-active-physical-soak-runner" \
        "$root/deneb-printsvc-stock-baseline" \
        "$root/deneb-printsvc-cli-selftest" \
        "$root/deneb-printsvc-init-selftest" \
        "$root/deneb-printsvc-native-audit" \
        "$root/deneb-printsvc-native-audit-selftest" \
        "$root/deneb-printsvc-integration-audit" \
        "$root/LVGL_LICENSE_TLSF.txt" \
        "$root/LODEPNG_LICENSE.txt"
    cat > "$root/deneb-printsvc-smoke" <<'EOF'
#!/bin/sh
PHYSICAL_OK="${DENEB_PRINTSVC_SMOKE_PHYSICAL_OK:-0}"
PHYSICAL_BUNDLE_OK="${DENEB_PRINTSVC_SMOKE_PHYSICAL_BUNDLE_OK:-0}"
ABORT_SETTLE_TIMEOUT="${DENEB_PRINTSVC_SMOKE_ABORT_SETTLE_TIMEOUT:-60}"
guarded_prehome() {
    summary "phase=$1 action=z_home rc=0 reason=pre_physical_home"
}
physical_safety_plan() {
    summary "phase=$1-safety kind=physical axes=$axes required_home=$required_home travel=$travel stop_conditions=$stop_conditions rc=0"
}
write_active_fixture() {
    :
}
write_preheat_abort_fixture() {
    :
}
write_representative_fixture() {
    :
}
wait_for_abort_idle() {
    :
}
summary "phase=physical-safety-gate rc=2 reason=missing_physical_ok"
summary "phase=physical-bundle-safety-gate rc=2 reason=missing_physical_bundle_ok count=2"
summary "phase=representative-fixture-safety-gate rc=2 reason=requires_all_axis_home"
EOF
    cat > "$root/deneb-printsvc-stability" <<'EOF'
#!/bin/sh
deneb-printsvc-smoke --native-no-restart
echo "--complete-job requires --physical-ok"
EOF
    cat > "$root/deneb-active-physical-soak-runner" <<'EOF'
#!/bin/sh
deneb-printsvc-smoke --native-no-restart --heat --motion --macro home \
    --complete-job "$FIXTURE"
EOF
    cat > "$root/deneb-printsvc-stock-baseline" <<'EOF'
#!/bin/sh
ALLOW_STOCK_SWITCH="${DENEB_PRINTSVC_STOCK_BASELINE_OK:-0}"
restore_native() {
    /etc/init.d/deneb-printsvc restart
}
find_stock_init() {
    /home/deneb/backups/deneb-ui/init/printserver.orig
    /rom/etc/init.d/printserver
}
wait_for_stock_api_ready() {
    :
}
wait_for_stock_api_ready "stock-api-ready"
EOF
    cat > "$root/deneb-runtime-inventory" <<'EOF'
#!/bin/sh
grep -q DENEB_COORDINATOR_DISABLED "$0" 2>/dev/null || true
# DENEB_COORDINATOR_DISABLED
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
        "$root/docs" \
        "$root/ui/src/screens" "$root/ui/installer" "$root/ui/init" \
        "$root/web/src" "$root/web/init" \
        "$root/printsvc/src" "$root/printsvc/init" \
        "$root/dfsvc/src" "$root/dfsvc/init" \
        "$root/tools"

    cat > "$root/common/print/print_backend_route.c" <<'EOF'
#include "print_backend_route.h"
deneb_print_backend_route_t deneb_print_backend_route_default(void) {
    return deneb_print_backend_route(DENEB_PRINT_BACKEND_NATIVE);
}
EOF
    cat > "$root/UM2C_MODDING_CHECKLIST.md" <<'EOF'
- [x] Accepted bounded hardware evidence covers native route ownership,
  observe-only firmware/temperature telemetry, low-temperature heat/preheat
  abort, active abort cleanup, bounded pause/resume, generated cluster API
  upload/start/abort, completion flow drain, native driver RSS reduction,
  diagnostics-log mitigation, short repeated-job stability, and strict
  stock/native resource comparison for the current bounded evidence set.
  /tmp/deneb-stock-d82245c.summary, /tmp/deneb-native-d82245c-observe.summary,
  and /tmp/deneb-84376b4-stability-complete5.summary are accepted summaries.
EOF
    cat > "$root/docs/PRINTSVC_EVIDENCE_LEDGER.md" <<'EOF'
| Gate | Status | Authoritative evidence | Notes |
| --- | --- | --- | --- |
| Observe-only firmware/temperature parity | Proven for paired stock/native capture | /tmp/deneb-stock-d82245c.summary, /tmp/deneb-native-d82245c-observe.summary | Observe-only telemetry proof. |
| Short repeated-job stability | Proven for five bounded Z-only completion jobs | /tmp/deneb-84376b4-stability-complete5.summary | Short repeated native completion proof. |
| Active physical soak memory behavior | Investigated, not promotion-complete | /tmp/deneb-a6fe410-long-active-soak.summary | Longer soak still required. |
EOF
    cat > "$root/common/print/print_backend_route.h" <<'EOF'
#define DENEB_PRINT_BACKEND_NATIVE 0
#define DENEB_PRINTSVC_STATUS_URL "tcp://127.0.0.1:5555"
#define DENEB_PRINTSVC_COMMAND_URL "tcp://127.0.0.1:5556"
typedef struct { int backend; } deneb_print_backend_route_t;
deneb_print_backend_route_t deneb_print_backend_route(int backend);
EOF
    cat > "$root/web/src/df_bridge.c" <<'EOF'
#include "df_bridge.h"
#define DF_PAIR_REQUEST_FILE "/tmp/deneb-df-pair-request"
static void clear_pair_request_file(void) {
    remove(DF_PAIR_REQUEST_FILE);
}
static int ensure_native_connector_started_for_connect(void) {
    int enable_rc = system("/etc/init.d/digitalfactory enable >/dev/null 2>&1");
    int start_rc = system("/etc/init.d/digitalfactory start >/dev/null 2>&1");
    if (enable_rc == 0 && start_rc == 0)
        return 0;
    clear_pair_request_file();
    return -1;
}
int deneb_df_bridge_run(const char *action, int timeout_seconds, char *out, unsigned long out_size) {
    if (ensure_native_connector_started_for_connect() != 0) {
        (void)out;
        (void)out_size;
        return -1; /* digitalfactory-start-failed */
    }
    return 0;
}
EOF
    cat > "$root/web/src/df_bridge.h" <<'EOF'
int deneb_df_bridge_run(const char *action, int timeout_seconds, char *out, unsigned long out_size);
EOF
    cat > "$root/dfsvc/src/main.c" <<'EOF'
int main(void) {
    return 0;
}
EOF
cat > "$root/dfsvc/init/digitalfactory.init" <<'EOF'
#!/bin/sh /etc/rc.common
PROG=/usr/bin/deneb-dfsvc
boot() {
    if uci -q get ultimaker.option.cluster_id >/dev/null 2>&1; then
        start
    else
        stop >/dev/null 2>&1 || true
        disable >/dev/null 2>&1 || true
    fi
}
start_service() {
    procd_open_instance
    procd_set_param command "$PROG"
    procd_close_instance
}
EOF
    cat > "$root/ui/CMakeLists.txt" <<'EOF'
add_executable(deneb-ui src/main.c)
EOF
    cat > "$root/ui/src/main.c" <<'EOF'
int main(void) {
    return 0;
}
EOF
    cat > "$root/ui/build-package.sh" <<'EOF'
#!/bin/sh
DENEB_RELEASE_CHANNEL="${DENEB_RELEASE_CHANNEL:-experimental}"
VERSION="${DENEB_PACKAGE_VERSION_OVERRIDE:-test}"
PRINTSVC_STOCK_SUMMARY="${DENEB_PRINTSVC_STOCK_SUMMARY:-}"
PRINTSVC_NATIVE_SUMMARY="${DENEB_PRINTSVC_NATIVE_SUMMARY:-}"
if [ "$DENEB_RELEASE_CHANNEL" != "experimental" ]; then
deneb-printsvc-smoke-verify" --stock --resources "$PRINTSVC_STOCK_SUMMARY"
    deneb-printsvc-smoke-verify" --native --idle --restart --boot-sync --client-proof --firmware-proof --complete-job --resources "$PRINTSVC_NATIVE_SUMMARY"
    deneb-printsvc-smoke-verify" --full "$PRINTSVC_NATIVE_SUMMARY"
    deneb-printsvc-smoke-compare" --require-reduction "$PRINTSVC_STOCK_SUMMARY" "$PRINTSVC_NATIVE_SUMMARY" "$@"
fi
deneb-printsvc-stock-baseline
deneb-printsvc-native-audit --source .
deneb-printsvc-native-audit-selftest
deneb-printsvc-integration-audit --source .
deneb-printsvc-integration-audit-selftest.sh
deneb-printsvc-release-gate-selftest.sh
deneb-printsvc-stability
deneb-active-physical-soak-runner
deneb-api digital-factory
deneb-printsvc-stability" --selftest
find "$STAGING_DIR" \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' \)
find "$STAGING_DIR" \( -name 'deneb-df-bridge.py' \)
! grep -Eq '(^|/)deneb-df-bridge$' "$STAGING_DIR/package-files.txt"
EOF
    cat > "$root/tools/build-update-release.sh" <<'EOF'
#!/bin/sh
DENEB_REPO_ROOT=/tmp/deneb-release-smoke-selftest DENEB_INSTALLER=/tmp/update.sh sh /tmp/deneb-printsvc-init-selftest
bash "$repo_root/tools/write-package-checksum.sh" "$package"
EOF
    cat > "$root/tools/deneb-python-runtime-inventory.sh" <<'EOF'
#!/bin/sh
# DENEB_COORDINATOR_DISABLED
EOF
cat > "$root/tools/build-update-release.ps1" <<'EOF'
[ValidateSet("experimental", "nightly", "stable")]
$ReleaseChannel = "experimental"
if ($ReleaseChannel -ne "experimental") {
    throw "Non-experimental release builds require -PrintsvcStockSummary and -PrintsvcNativeSummary."
}
$buildPackageEnv += " DENEB_PRINTSVC_STOCK_SUMMARY='$printsvcStockSummaryWsl'"
$buildPackageEnv += " DENEB_PRINTSVC_NATIVE_SUMMARY='$printsvcNativeSummaryWsl'"
$buildUi = "$buildPackageEnv bash ui/build-package.sh ui/build-musl/deneb-ui${buildPackageEvidenceArgs}"
deneb-printsvc-release-gate-selftest.sh
deneb-printsvc-integration-audit
deneb-printsvc-integration-audit-selftest.sh
& wsl -d $Distro -u root -- bash -lc $verifyPackage
$writeChecksum = "bash '$repoWsl/tools/write-package-checksum.sh' '$packageWsl'"
& wsl -d $Distro -u root -- bash -lc $writeChecksum
Write-Output "Verified native-only print service package: $package"
EOF
    cat > "$root/tools/deneb-printsvc-release-gate-selftest.sh" <<'EOF'
#!/bin/sh
PACKAGE_VERSION="release-gate-selftest.$$"
PACKAGE_STAGING="/tmp/Deneb_Update_${PACKAGE_VERSION}"
cleanup() {
    rm -rf "$PACKAGE_STAGING"
}
DENEB_PACKAGE_VERSION_OVERRIDE="$PACKAGE_VERSION"
nightly_invalid_stock_summary
nightly_invalid_native_summary
EOF
    cat > "$root/tools/deneb-printsvc-smoke.sh" <<'EOF'
#!/bin/sh
PHYSICAL_OK="${DENEB_PRINTSVC_SMOKE_PHYSICAL_OK:-0}"
PHYSICAL_BUNDLE_OK="${DENEB_PRINTSVC_SMOKE_PHYSICAL_BUNDLE_OK:-0}"
ABORT_SETTLE_TIMEOUT="${DENEB_PRINTSVC_SMOKE_ABORT_SETTLE_TIMEOUT:-60}"
guarded_prehome() {
    summary "phase=$1 action=z_home rc=0 reason=pre_physical_home"
}
physical_safety_plan() {
    summary "phase=$1-safety kind=physical axes=$axes required_home=$required_home travel=$travel stop_conditions=$stop_conditions rc=0"
}
write_active_fixture() {
    :
}
write_preheat_abort_fixture() {
    :
}
write_representative_fixture() {
    :
}
wait_for_abort_idle() {
    :
}
summary "phase=physical-safety-gate rc=2 reason=missing_physical_ok"
summary "phase=physical-bundle-safety-gate rc=2 reason=missing_physical_bundle_ok count=2"
summary "phase=representative-fixture-safety-gate rc=2 reason=requires_all_axis_home"
EOF
    cat > "$root/tools/deneb-printsvc-smoke-selftest.sh" <<'EOF'
#!/bin/sh
smoke_requires_physical_ack
smoke_rejects_physical_bundle
verify_rejects_missing_physical_safety
generated bounded Z active fixture
generated low-temperature preheat-abort fixture
generated bounded representative XYZ fixture
smoke_rejects_representative_without_all_axis_home
EOF
    cat > "$root/tools/deneb-printsvc-stability.sh" <<'EOF'
#!/bin/sh
deneb-printsvc-smoke --native-no-restart
echo "--complete-job requires --physical-ok"
EOF
    cat > "$root/tools/deneb-active-physical-soak-runner.sh" <<'EOF'
#!/bin/sh
deneb-printsvc-smoke --native-no-restart --heat --motion --macro home \
    --complete-job "$FIXTURE"
EOF
    cat > "$root/tools/deneb-printsvc-stock-baseline.sh" <<'EOF'
#!/bin/sh
ALLOW_STOCK_SWITCH="${DENEB_PRINTSVC_STOCK_BASELINE_OK:-0}"
restore_native() {
    /etc/init.d/deneb-printsvc restart
}
find_stock_init() {
    /home/deneb/backups/deneb-ui/init/printserver.orig
    /rom/etc/init.d/printserver
}
wait_for_stock_api_ready() {
    :
}
wait_for_stock_api_ready "stock-api-ready"
EOF
cat > "$root/ui/installer/update.sh" <<'EOF'
#!/bin/sh
DENEB_COORDINATOR_DISABLED=1
deneb-printsvc-native-audit --package-dir /tmp/update
/tmp/update/deneb-printsvc-native-audit-selftest >audit.log
/tmp/update/deneb-printsvc-integration-audit --package-dir /tmp/update
/tmp/update/deneb-printsvc-stability --selftest
/etc/init.d/deneb-api restart
/etc/init.d/deneb-web restart
configure_digitalfactory_boot() {
    /etc/init.d/digitalfactory disable
}
rm -f /usr/bin/deneb-df-bridge
rm -f /tmp/Deneb_Update_*.deneb
cp /tmp/update/deneb-printsvc-stability /usr/bin/deneb-printsvc-stability
cp /tmp/update/deneb-active-physical-soak-runner /usr/bin/deneb-active-physical-soak-runner
cp /tmp/update/deneb-printsvc-stock-baseline /usr/bin/deneb-printsvc-stock-baseline
cp /tmp/update/deneb-dfsvc /usr/bin/deneb-dfsvc
cp /tmp/update/digitalfactory.init /etc/init.d/digitalfactory
install_web_runtime
configure_digitalfactory_boot
native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction
EOF
    cat > "$root/ui/src/screens/screen_digital_factory.c" <<'EOF'
void connect_cb(void) {
    /etc/init.d/digitalfactory enable
    /usr/bin/deneb-api digital-factory connect
    /usr/bin/deneb-api digital-factory disconnect
}
EOF
    cat > "$root/web/CMakeLists.txt" <<'EOF'
add_executable(deneb-api src/main.c src/df_bridge.c)
EOF
    cat > "$root/web/src/main.c" <<'EOF'
int deneb_df_bridge_run(const char *action, int timeout_seconds, char *out, unsigned long out_size);
int main(int argc, char **argv) {
    if (argc > 1 && argv[1][0] && "digital-factory"[0]) {
        return deneb_df_bridge_run("status", 3, 0, 0);
    }
    return 0;
}
EOF
    cat > "$root/web/src/api_http.c" <<'EOF'
void api_http(void) {}
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
    /etc/init.d/deneb-api start
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
sh "$AUDIT" --package-dir "$VALID" >"$TMP_DIR/valid.log"
echo "PASS: valid package fixture accepted"

VALID_SOURCE="$TMP_DIR/source-valid"
write_valid_source "$VALID_SOURCE"
sh "$AUDIT" --source "$VALID_SOURCE" >"$TMP_DIR/source-valid.log"
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

SOURCE_DF_PYTHON_BRIDGE="$TMP_DIR/source-df-python-bridge"
write_valid_source "$SOURCE_DF_PYTHON_BRIDGE"
mkdir -p "$SOURCE_DF_PYTHON_BRIDGE/ui/scripts"
touch "$SOURCE_DF_PYTHON_BRIDGE/ui/scripts/deneb-df-bridge.py"
expect_failure rejects_source_df_python_bridge "$AUDIT" --source "$SOURCE_DF_PYTHON_BRIDGE"

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
grep -v '/etc/init.d/deneb-api start' "$SOURCE_MISSING_WEB_API_INSTANCE/web/init/deneb-web.init" > "$SOURCE_MISSING_WEB_API_INSTANCE/web/init/deneb-web.tmp"
mv "$SOURCE_MISSING_WEB_API_INSTANCE/web/init/deneb-web.tmp" "$SOURCE_MISSING_WEB_API_INSTANCE/web/init/deneb-web.init"
expect_failure rejects_source_missing_web_api_instance "$AUDIT" --source "$SOURCE_MISSING_WEB_API_INSTANCE"

SOURCE_DUPLICATE_WEB_API_INSTANCE="$TMP_DIR/source-duplicate-web-api-instance"
write_valid_source "$SOURCE_DUPLICATE_WEB_API_INSTANCE"
printf '\nprocd_open_instance api\n' >> "$SOURCE_DUPLICATE_WEB_API_INSTANCE/web/init/deneb-web.init"
expect_failure rejects_source_duplicate_web_api_instance "$AUDIT" --source "$SOURCE_DUPLICATE_WEB_API_INSTANCE"

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

SOURCE_MISSING_NATIVE_CHECKSUM="$TMP_DIR/source-missing-native-checksum"
write_valid_source "$SOURCE_MISSING_NATIVE_CHECKSUM"
grep -v 'write-package-checksum' \
    "$SOURCE_MISSING_NATIVE_CHECKSUM/tools/build-update-release.sh" > \
    "$SOURCE_MISSING_NATIVE_CHECKSUM/tools/build-update-release.tmp"
mv "$SOURCE_MISSING_NATIVE_CHECKSUM/tools/build-update-release.tmp" \
    "$SOURCE_MISSING_NATIVE_CHECKSUM/tools/build-update-release.sh"
expect_failure rejects_source_missing_native_checksum "$AUDIT" --source "$SOURCE_MISSING_NATIVE_CHECKSUM"

SOURCE_EARLY_NATIVE_CHECKSUM="$TMP_DIR/source-early-native-checksum"
write_valid_source "$SOURCE_EARLY_NATIVE_CHECKSUM"
cat > "$SOURCE_EARLY_NATIVE_CHECKSUM/tools/build-update-release.sh" <<'EOF'
#!/bin/sh
bash "$repo_root/tools/write-package-checksum.sh" "$package"
DENEB_REPO_ROOT=/tmp/deneb-release-smoke-selftest DENEB_INSTALLER=/tmp/update.sh sh /tmp/deneb-printsvc-init-selftest
EOF
expect_failure rejects_source_early_native_checksum "$AUDIT" --source "$SOURCE_EARLY_NATIVE_CHECKSUM"

SOURCE_MISSING_WINDOWS_CHECKSUM="$TMP_DIR/source-missing-windows-checksum"
write_valid_source "$SOURCE_MISSING_WINDOWS_CHECKSUM"
grep -v 'writeChecksum' \
    "$SOURCE_MISSING_WINDOWS_CHECKSUM/tools/build-update-release.ps1" > \
    "$SOURCE_MISSING_WINDOWS_CHECKSUM/tools/build-update-release.tmp"
mv "$SOURCE_MISSING_WINDOWS_CHECKSUM/tools/build-update-release.tmp" \
    "$SOURCE_MISSING_WINDOWS_CHECKSUM/tools/build-update-release.ps1"
expect_failure rejects_source_missing_windows_checksum "$AUDIT" --source "$SOURCE_MISSING_WINDOWS_CHECKSUM"

SOURCE_EARLY_WINDOWS_VERIFIED="$TMP_DIR/source-early-windows-verified"
write_valid_source "$SOURCE_EARLY_WINDOWS_VERIFIED"
cat > "$SOURCE_EARLY_WINDOWS_VERIFIED/tools/build-update-release.ps1" <<'EOF'
[ValidateSet("experimental", "nightly", "stable")]
$ReleaseChannel = "experimental"
if ($ReleaseChannel -ne "experimental") {
    throw "Non-experimental release builds require -PrintsvcStockSummary and -PrintsvcNativeSummary."
}
$buildPackageEnv += " DENEB_PRINTSVC_STOCK_SUMMARY='$printsvcStockSummaryWsl'"
$buildPackageEnv += " DENEB_PRINTSVC_NATIVE_SUMMARY='$printsvcNativeSummaryWsl'"
$buildUi = "$buildPackageEnv bash ui/build-package.sh ui/build-musl/deneb-ui${buildPackageEvidenceArgs}"
deneb-printsvc-release-gate-selftest.sh
deneb-printsvc-integration-audit
deneb-printsvc-integration-audit-selftest.sh
$verifyPackage = "printf 'Verified native-only print service package: %s\n' '$packageWsl'"
& wsl -d $Distro -u root -- bash -lc $verifyPackage
$writeChecksum = "bash '$repoWsl/tools/write-package-checksum.sh' '$packageWsl'"
& wsl -d $Distro -u root -- bash -lc $writeChecksum
Write-Output "Verified native-only print service package: $package"
EOF
expect_failure rejects_source_early_windows_verified "$AUDIT" --source "$SOURCE_EARLY_WINDOWS_VERIFIED"

SOURCE_MISSING_STOCK_SUMMARY_VERIFY="$TMP_DIR/source-missing-stock-summary-verify"
write_valid_source "$SOURCE_MISSING_STOCK_SUMMARY_VERIFY"
grep -v 'deneb-printsvc-smoke-verify" --stock --resources' \
    "$SOURCE_MISSING_STOCK_SUMMARY_VERIFY/ui/build-package.sh" > \
    "$SOURCE_MISSING_STOCK_SUMMARY_VERIFY/ui/build-package.tmp"
mv "$SOURCE_MISSING_STOCK_SUMMARY_VERIFY/ui/build-package.tmp" \
    "$SOURCE_MISSING_STOCK_SUMMARY_VERIFY/ui/build-package.sh"
expect_failure rejects_source_missing_stock_summary_verify "$AUDIT" --source "$SOURCE_MISSING_STOCK_SUMMARY_VERIFY"

SOURCE_MISSING_BASELINE_HELPER="$TMP_DIR/source-missing-baseline-helper"
write_valid_source "$SOURCE_MISSING_BASELINE_HELPER"
rm -f "$SOURCE_MISSING_BASELINE_HELPER/tools/deneb-printsvc-stock-baseline.sh"
expect_failure rejects_source_missing_baseline_helper "$AUDIT" --source "$SOURCE_MISSING_BASELINE_HELPER"

SOURCE_STALE_FIRMWARE_DOCS="$TMP_DIR/source-stale-firmware-docs"
write_valid_source "$SOURCE_STALE_FIRMWARE_DOCS"
grep -v '/tmp/deneb-stock-d82245c[.]summary' \
    "$SOURCE_STALE_FIRMWARE_DOCS/docs/PRINTSVC_EVIDENCE_LEDGER.md" > \
    "$SOURCE_STALE_FIRMWARE_DOCS/docs/PRINTSVC_EVIDENCE_LEDGER.tmp"
mv "$SOURCE_STALE_FIRMWARE_DOCS/docs/PRINTSVC_EVIDENCE_LEDGER.tmp" \
    "$SOURCE_STALE_FIRMWARE_DOCS/docs/PRINTSVC_EVIDENCE_LEDGER.md"
expect_failure rejects_source_stale_firmware_docs "$AUDIT" --source "$SOURCE_STALE_FIRMWARE_DOCS"

SOURCE_STALE_STABILITY_DOCS="$TMP_DIR/source-stale-stability-docs"
write_valid_source "$SOURCE_STALE_STABILITY_DOCS"
grep -v '/tmp/deneb-84376b4-stability-complete5[.]summary' \
    "$SOURCE_STALE_STABILITY_DOCS/docs/PRINTSVC_EVIDENCE_LEDGER.md" > \
    "$SOURCE_STALE_STABILITY_DOCS/docs/PRINTSVC_EVIDENCE_LEDGER.tmp"
mv "$SOURCE_STALE_STABILITY_DOCS/docs/PRINTSVC_EVIDENCE_LEDGER.tmp" \
    "$SOURCE_STALE_STABILITY_DOCS/docs/PRINTSVC_EVIDENCE_LEDGER.md"
expect_failure rejects_source_stale_stability_docs "$AUDIT" --source "$SOURCE_STALE_STABILITY_DOCS"

SOURCE_BASELINE_WITHOUT_ACK="$TMP_DIR/source-baseline-without-ack"
write_valid_source "$SOURCE_BASELINE_WITHOUT_ACK"
grep -v 'DENEB_PRINTSVC_STOCK_BASELINE_OK' \
    "$SOURCE_BASELINE_WITHOUT_ACK/tools/deneb-printsvc-stock-baseline.sh" > \
    "$SOURCE_BASELINE_WITHOUT_ACK/tools/deneb-printsvc-stock-baseline.tmp"
mv "$SOURCE_BASELINE_WITHOUT_ACK/tools/deneb-printsvc-stock-baseline.tmp" \
    "$SOURCE_BASELINE_WITHOUT_ACK/tools/deneb-printsvc-stock-baseline.sh"
expect_failure rejects_source_baseline_without_ack "$AUDIT" --source "$SOURCE_BASELINE_WITHOUT_ACK"

SOURCE_BASELINE_WITHOUT_RESTORE="$TMP_DIR/source-baseline-without-restore"
write_valid_source "$SOURCE_BASELINE_WITHOUT_RESTORE"
grep -v 'restore_native' \
    "$SOURCE_BASELINE_WITHOUT_RESTORE/tools/deneb-printsvc-stock-baseline.sh" > \
    "$SOURCE_BASELINE_WITHOUT_RESTORE/tools/deneb-printsvc-stock-baseline.tmp"
mv "$SOURCE_BASELINE_WITHOUT_RESTORE/tools/deneb-printsvc-stock-baseline.tmp" \
    "$SOURCE_BASELINE_WITHOUT_RESTORE/tools/deneb-printsvc-stock-baseline.sh"
expect_failure rejects_source_baseline_without_restore "$AUDIT" --source "$SOURCE_BASELINE_WITHOUT_RESTORE"

SOURCE_MISSING_INSTALLER_SELFTEST="$TMP_DIR/source-missing-installer-selftest"
write_valid_source "$SOURCE_MISSING_INSTALLER_SELFTEST"
grep -v 'deneb-printsvc-native-audit-selftest' "$SOURCE_MISSING_INSTALLER_SELFTEST/ui/installer/update.sh" > "$SOURCE_MISSING_INSTALLER_SELFTEST/ui/installer/update.tmp"
mv "$SOURCE_MISSING_INSTALLER_SELFTEST/ui/installer/update.tmp" "$SOURCE_MISSING_INSTALLER_SELFTEST/ui/installer/update.sh"
expect_failure rejects_source_missing_installer_selftest "$AUDIT" --source "$SOURCE_MISSING_INSTALLER_SELFTEST"

SOURCE_MISSING_INSTALLER_WEB_RESTART="$TMP_DIR/source-missing-installer-web-restart"
write_valid_source "$SOURCE_MISSING_INSTALLER_WEB_RESTART"
grep -v '/etc/init.d/deneb-api restart' "$SOURCE_MISSING_INSTALLER_WEB_RESTART/ui/installer/update.sh" |
    grep -v '/etc/init.d/deneb-web restart' > "$SOURCE_MISSING_INSTALLER_WEB_RESTART/ui/installer/update.tmp"
mv "$SOURCE_MISSING_INSTALLER_WEB_RESTART/ui/installer/update.tmp" "$SOURCE_MISSING_INSTALLER_WEB_RESTART/ui/installer/update.sh"
expect_failure rejects_source_missing_installer_web_restart "$AUDIT" --source "$SOURCE_MISSING_INSTALLER_WEB_RESTART"
SOURCE_MISSING_RUNTIME_INVENTORY="$TMP_DIR/source-missing-runtime-inventory"
write_valid_source "$SOURCE_MISSING_RUNTIME_INVENTORY"
rm -f "$SOURCE_MISSING_RUNTIME_INVENTORY/tools/deneb-python-runtime-inventory.sh"
expect_failure rejects_source_missing_runtime_inventory "$AUDIT" --source "$SOURCE_MISSING_RUNTIME_INVENTORY"

SOURCE_RUNTIME_INVENTORY_NO_SHIM="$TMP_DIR/source-runtime-inventory-no-shim"
write_valid_source "$SOURCE_RUNTIME_INVENTORY_NO_SHIM"
grep -v 'DENEB_COORDINATOR_DISABLED' \
    "$SOURCE_RUNTIME_INVENTORY_NO_SHIM/tools/deneb-python-runtime-inventory.sh" > \
    "$SOURCE_RUNTIME_INVENTORY_NO_SHIM/tools/deneb-python-runtime-inventory.tmp"
mv "$SOURCE_RUNTIME_INVENTORY_NO_SHIM/tools/deneb-python-runtime-inventory.tmp" \
    "$SOURCE_RUNTIME_INVENTORY_NO_SHIM/tools/deneb-python-runtime-inventory.sh"
expect_failure rejects_source_runtime_inventory_no_shim "$AUDIT" --source "$SOURCE_RUNTIME_INVENTORY_NO_SHIM"

MISSING_AUDIT="$TMP_DIR/missing-audit"
write_valid_package "$MISSING_AUDIT"
rm -f "$MISSING_AUDIT/deneb-printsvc-native-audit"
expect_failure rejects_missing_audit "$AUDIT" --package-dir "$MISSING_AUDIT"

MISSING_SMOKE_SELFTEST="$TMP_DIR/missing-smoke-selftest"
write_valid_package "$MISSING_SMOKE_SELFTEST"
rm -f "$MISSING_SMOKE_SELFTEST/deneb-printsvc-smoke-selftest"
expect_failure rejects_missing_smoke_selftest "$AUDIT" --package-dir "$MISSING_SMOKE_SELFTEST"

MISSING_STABILITY="$TMP_DIR/missing-stability"
write_valid_package "$MISSING_STABILITY"
rm -f "$MISSING_STABILITY/deneb-printsvc-stability"
expect_failure rejects_missing_stability "$AUDIT" --package-dir "$MISSING_STABILITY"

MISSING_ACTIVE_SOAK_RUNNER="$TMP_DIR/missing-active-soak-runner"
write_valid_package "$MISSING_ACTIVE_SOAK_RUNNER"
rm -f "$MISSING_ACTIVE_SOAK_RUNNER/deneb-active-physical-soak-runner"
expect_failure rejects_missing_active_soak_runner "$AUDIT" --package-dir "$MISSING_ACTIVE_SOAK_RUNNER"

MISSING_BASELINE_HELPER="$TMP_DIR/missing-baseline-helper"
write_valid_package "$MISSING_BASELINE_HELPER"
rm -f "$MISSING_BASELINE_HELPER/deneb-printsvc-stock-baseline"
expect_failure rejects_missing_baseline_helper "$AUDIT" --package-dir "$MISSING_BASELINE_HELPER"

BASELINE_WITHOUT_ACK="$TMP_DIR/baseline-without-ack"
write_valid_package "$BASELINE_WITHOUT_ACK"
grep -v 'DENEB_PRINTSVC_STOCK_BASELINE_OK' \
    "$BASELINE_WITHOUT_ACK/deneb-printsvc-stock-baseline" > \
    "$BASELINE_WITHOUT_ACK/deneb-printsvc-stock-baseline.tmp"
mv "$BASELINE_WITHOUT_ACK/deneb-printsvc-stock-baseline.tmp" \
    "$BASELINE_WITHOUT_ACK/deneb-printsvc-stock-baseline"
expect_failure rejects_baseline_without_ack "$AUDIT" --package-dir "$BASELINE_WITHOUT_ACK"

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

MISSING_SAFE_FIXTURES="$TMP_DIR/missing-safe-fixtures"
write_valid_package "$MISSING_SAFE_FIXTURES"
grep -v 'write_active_fixture' "$MISSING_SAFE_FIXTURES/deneb-printsvc-smoke" |
    grep -v 'write_preheat_abort_fixture' |
    grep -v 'write_representative_fixture' > "$MISSING_SAFE_FIXTURES/deneb-printsvc-smoke.tmp"
mv "$MISSING_SAFE_FIXTURES/deneb-printsvc-smoke.tmp" "$MISSING_SAFE_FIXTURES/deneb-printsvc-smoke"
expect_failure rejects_missing_safe_fixtures "$AUDIT" --package-dir "$MISSING_SAFE_FIXTURES"

MISSING_ABORT_WAIT="$TMP_DIR/missing-abort-wait"
write_valid_package "$MISSING_ABORT_WAIT"
grep -v 'wait_for_abort_idle' "$MISSING_ABORT_WAIT/deneb-printsvc-smoke" |
    grep -v 'DENEB_PRINTSVC_SMOKE_ABORT_SETTLE_TIMEOUT' > "$MISSING_ABORT_WAIT/deneb-printsvc-smoke.tmp"
mv "$MISSING_ABORT_WAIT/deneb-printsvc-smoke.tmp" "$MISSING_ABORT_WAIT/deneb-printsvc-smoke"
expect_failure rejects_missing_abort_wait "$AUDIT" --package-dir "$MISSING_ABORT_WAIT"

MISSING_CLI_SELFTEST="$TMP_DIR/missing-cli-selftest"
write_valid_package "$MISSING_CLI_SELFTEST"
rm -f "$MISSING_CLI_SELFTEST/deneb-printsvc-cli-selftest"
expect_failure rejects_missing_cli_selftest "$AUDIT" --package-dir "$MISSING_CLI_SELFTEST"

MISSING_INIT_SELFTEST="$TMP_DIR/missing-init-selftest"
write_valid_package "$MISSING_INIT_SELFTEST"
rm -f "$MISSING_INIT_SELFTEST/deneb-printsvc-init-selftest"
expect_failure rejects_missing_init_selftest "$AUDIT" --package-dir "$MISSING_INIT_SELFTEST"

HOST_VALIDATION_SCRIPT="$TMP_DIR/host-validation-script"
write_valid_package "$HOST_VALIDATION_SCRIPT"
touch "$HOST_VALIDATION_SCRIPT/deneb-stock-menu-prune-selftest"
expect_failure rejects_host_validation_script "$AUDIT" --package-dir "$HOST_VALIDATION_SCRIPT"

MISSING_RUNTIME_INVENTORY="$TMP_DIR/missing-runtime-inventory"
write_valid_package "$MISSING_RUNTIME_INVENTORY"
rm -f "$MISSING_RUNTIME_INVENTORY/deneb-runtime-inventory"
expect_failure rejects_missing_runtime_inventory "$AUDIT" --package-dir "$MISSING_RUNTIME_INVENTORY"

RUNTIME_INVENTORY_NO_SHIM="$TMP_DIR/runtime-inventory-no-shim"
write_valid_package "$RUNTIME_INVENTORY_NO_SHIM"
grep -v 'DENEB_COORDINATOR_DISABLED' \
    "$RUNTIME_INVENTORY_NO_SHIM/deneb-runtime-inventory" > \
    "$RUNTIME_INVENTORY_NO_SHIM/deneb-runtime-inventory.tmp"
mv "$RUNTIME_INVENTORY_NO_SHIM/deneb-runtime-inventory.tmp" \
    "$RUNTIME_INVENTORY_NO_SHIM/deneb-runtime-inventory"
expect_failure rejects_runtime_inventory_no_shim "$AUDIT" --package-dir "$RUNTIME_INVENTORY_NO_SHIM"

MISSING_TLSF_NOTICE="$TMP_DIR/missing-tlsf-notice"
write_valid_package "$MISSING_TLSF_NOTICE"
rm -f "$MISSING_TLSF_NOTICE/LVGL_LICENSE_TLSF.txt"
expect_failure rejects_missing_tlsf_notice "$AUDIT" --package-dir "$MISSING_TLSF_NOTICE"

MISSING_LODEPNG_NOTICE="$TMP_DIR/missing-lodepng-notice"
write_valid_package "$MISSING_LODEPNG_NOTICE"
rm -f "$MISSING_LODEPNG_NOTICE/LODEPNG_LICENSE.txt"
expect_failure rejects_missing_lodepng_notice "$AUDIT" --package-dir "$MISSING_LODEPNG_NOTICE"

DRIVER_ARTIFACT="$TMP_DIR/driver-artifact"
write_valid_package "$DRIVER_ARTIFACT"
touch "$DRIVER_ARTIFACT/print_service.py"
expect_failure rejects_driver_artifact "$AUDIT" --package-dir "$DRIVER_ARTIFACT"

DF_BRIDGE_ARTIFACT="$TMP_DIR/df-bridge-artifact"
write_valid_package "$DF_BRIDGE_ARTIFACT"
touch "$DF_BRIDGE_ARTIFACT/deneb-df-bridge.py"
expect_failure rejects_df_bridge_artifact "$AUDIT" --package-dir "$DF_BRIDGE_ARTIFACT"

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

# Coordinator dependency negative fixtures
SOURCE_COORD_PORT="$TMP_DIR/source-coord-port"
write_valid_source "$SOURCE_COORD_PORT"
cat >> "$SOURCE_COORD_PORT/web/src/native_client.c" <<'EOF'
const char *bad_port = "tcp://127.0.0.1:5565";
EOF
expect_failure rejects_source_coord_port "$AUDIT" --source "$SOURCE_COORD_PORT"

SOURCE_COORD_SERVICE="$TMP_DIR/source-coord-service"
write_valid_source "$SOURCE_COORD_SERVICE"
cat >> "$SOURCE_COORD_SERVICE/common/print/print_backend_route.c" <<'EOF'
const char *bad_service = "coordinator::print::handling";
EOF
expect_failure rejects_source_coord_service "$AUDIT" --source "$SOURCE_COORD_SERVICE"

SOURCE_COORD_PRINTER_PUBSUB="$TMP_DIR/source-coord-printer-pubsub"
write_valid_source "$SOURCE_COORD_PRINTER_PUBSUB"
mkdir -p "$SOURCE_COORD_PRINTER_PUBSUB/ui/src"
cat > "$SOURCE_COORD_PRINTER_PUBSUB/ui/src/bad_coord.c" <<'EOF'
const char *bad = "PRINTER_PUBSUB";
EOF
expect_failure rejects_source_coord_printer_pubsub "$AUDIT" --source "$SOURCE_COORD_PRINTER_PUBSUB"

SOURCE_PRINT_ON_BUILDPLATE="$TMP_DIR/source-print-on-buildplate"
write_valid_source "$SOURCE_PRINT_ON_BUILDPLATE"
cat >> "$SOURCE_PRINT_ON_BUILDPLATE/common/print/print_state_rules.c" <<'EOF'
const char *bad = "print_on_buildplate";
EOF
expect_failure rejects_source_print_on_buildplate "$AUDIT" --source "$SOURCE_PRINT_ON_BUILDPLATE"

RUNTIME_SELFTEST_INIT="$TMP_DIR/runtime-inventory-init"
RUNTIME_SELFTEST_PROC="$TMP_DIR/runtime-inventory-proc"
RUNTIME_SELFTEST_BIN="$TMP_DIR/runtime-inventory-bin"
mkdir -p "$RUNTIME_SELFTEST_INIT" "$RUNTIME_SELFTEST_PROC" "$RUNTIME_SELFTEST_BIN"
cat > "$RUNTIME_SELFTEST_INIT/coordinator" <<'EOF'
#!/bin/sh
# DENEB_COORDINATOR_DISABLED
exit 0
EOF
chmod +x "$RUNTIME_SELFTEST_INIT/coordinator"
cat > "$RUNTIME_SELFTEST_BIN/wget" <<'EOF'
#!/bin/sh
exit 1
EOF
chmod +x "$RUNTIME_SELFTEST_BIN/wget"

write_runtime_process() {
    local pid="$1" exe="$2" state="$3" flags="$4" proc="$RUNTIME_SELFTEST_PROC/$1"
    shift 4
    mkdir -p "$proc/fd"
    [ -z "$exe" ] || ln -s "$exe" "$proc/exe"
    printf '%s (fixture ) process) %s 1 1 1 0 -1 %s 0 0 0 0 0 0 0 0 20 0 1 0 12345 0\n' \
        "$pid" "$state" "$flags" > "$proc/stat"
    printf '%s\000' "$@" > "$proc/cmdline"
    printf 'VmSize: 2048 kB\nVmRSS: 1024 kB\nVmData: 256 kB\nThreads: 2\n' > "$proc/status"
    printf 'Private_Dirty: 20 kB\nPrivate_Clean: 4 kB\n' > "$proc/smaps"
}

capture_runtime_inventory() {
    PATH="$RUNTIME_SELFTEST_BIN:$PATH" \
        DENEB_RUNTIME_INVENTORY_INIT_DIR="$RUNTIME_SELFTEST_INIT" \
        DENEB_RUNTIME_INVENTORY_PROC_DIR="$RUNTIME_SELFTEST_PROC" \
        DENEB_PYTHON_RUNTIME_SUMMARY="$TMP_DIR/runtime-inventory.md" \
        sh "$RUNTIME_INVENTORY" > "$TMP_DIR/runtime-inventory.out"
}

write_runtime_process 100 /usr/bin/dash S 0 sh unrelated-python /tmp/connector.py compileall
write_runtime_process 101 /usr/bin/python3.13 S 0 /tmp/interpreter-alias -u /home/cygnus/coordinator/coordinator.py
write_runtime_process 102 '/usr/bin/python3.13t (deleted)' S 0 python3 -m compileall '/tmp/folder with spaces'
write_runtime_process 103 /usr/bin/python3.13 S 0 /usr/bin/python3 -W 'ignore' -- /home/cygnus/menu/executor.py
write_runtime_process 104 /usr/bin/python3.13 S 0 python3 '/unrelated path/example.py' /tmp/print_service.py compileall
write_runtime_process 105 /usr/bin/python3.13 S 0 python3 /home/cygnus/connector.py
write_runtime_process 106 /usr/bin/python3.13 S 0 python3 /home/cygnus/marlindriver/print_service.py
write_runtime_process 107 /usr/bin/python3-helper S 0 python3-helper /tmp/connector.py
write_runtime_process 108 "" S 0 unknown
capture_runtime_inventory
grep -F '| `coordinator` | disabled shim | disabled shim |' "$TMP_DIR/runtime-inventory.out" >/dev/null
if grep -F '| `coordinator` | rc 0 | rc 0 |' "$TMP_DIR/runtime-inventory.out" >/dev/null; then
    cat "$TMP_DIR/runtime-inventory.out"
    echo "FAIL: disabled coordinator shim reported as running rc 0" >&2
    exit 1
fi
echo "PASS: runtime inventory reports disabled coordinator shim"

grep -E '^\| `101` \| .* stock coordinator fallback/.* \| 2048 \| 1024 \| 256 \| 2 \| 0 \| 24 \|$' "$TMP_DIR/runtime-inventory.out" >/dev/null
grep -E '^\| `102` \| .* stock startup work' "$TMP_DIR/runtime-inventory.out" >/dev/null
grep -E '^\| `103` \| .* stock UI fallback' "$TMP_DIR/runtime-inventory.out" >/dev/null
grep -E '^\| `104` \| .* unclassified Python runtime' "$TMP_DIR/runtime-inventory.out" >/dev/null
grep -E '^\| `105` \| .* unexpected regression in Deneb-native DF path' "$TMP_DIR/runtime-inventory.out" >/dev/null
grep -E '^\| `106` \| .* unexpected regression in Deneb-native print-service path' "$TMP_DIR/runtime-inventory.out" >/dev/null
grep -F 'Inventory incomplete. Could not inspect all live processes (1 unresolved).' "$TMP_DIR/runtime-inventory.out" >/dev/null
if grep -Eq '^\| `(100|107|108)` \|' "$TMP_DIR/runtime-inventory.out"; then
    echo "FAIL: runtime inventory listed a non-Python or uninspectable executable" >&2
    exit 1
fi
echo "PASS: runtime inventory identifies interpreters and classifies their structured arguments"

RUNTIME_SELFTEST_PROC="$TMP_DIR/runtime-inventory-no-python"
write_runtime_process 201 "" Z 0 dead
write_runtime_process 202 "" I 2097152 kworker
capture_runtime_inventory
grep -F '| none | none | no live Python process found |' "$TMP_DIR/runtime-inventory.out" >/dev/null
write_runtime_process 203 "" S 0 unknown
capture_runtime_inventory
grep -F 'Inventory incomplete. Could not inspect all live processes (1 unresolved).' "$TMP_DIR/runtime-inventory.out" >/dev/null
if grep -Fq 'no live Python process found' "$TMP_DIR/runtime-inventory.out"; then
    echo "FAIL: runtime inventory reported absence when an executable could not be inspected" >&2
    exit 1
fi
echo "PASS: runtime inventory distinguishes complete scans from unreadable processes"
echo "deneb-printsvc native audit selftest passed"
