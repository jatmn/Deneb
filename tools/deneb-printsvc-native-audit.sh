#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Static de-Python audit for the native print-service migration.

set -eu

SCRIPT_NAME=$(basename "$0")

usage() {
    cat >&2 <<EOF
Usage:
  ${SCRIPT_NAME} --source <repo-root>
  ${SCRIPT_NAME} --package-dir <unpacked-update-dir>
  ${SCRIPT_NAME} --archive <Deneb_Update_*.deneb>
EOF
    exit 2
}

pass() {
    printf 'PASS: %s\n' "$1"
}

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

require_file() {
    if [ ! -f "$1" ]; then
        fail "$2 missing: $1"
    fi
    pass "$2"
}

require_dir() {
    if [ ! -d "$1" ]; then
        fail "$2 missing: $1"
    fi
    pass "$2"
}

require_pattern() {
    file=$1
    pattern=$2
    label=$3
    if ! grep -Eq "$pattern" "$file"; then
        fail "$label"
    fi
    pass "$label"
}

reject_pattern() {
    file=$1
    pattern=$2
    label=$3
    if grep -Eq "$pattern" "$file"; then
        fail "$label"
    fi
    pass "$label"
}

require_order() {
    file=$1
    before_pattern=$2
    after_pattern=$3
    label=$4
    before_line=$(grep -nE "$before_pattern" "$file" | head -n 1 | cut -d: -f1 || true)
    after_line=$(grep -nE "$after_pattern" "$file" | head -n 1 | cut -d: -f1 || true)
    if [ -z "$before_line" ] || [ -z "$after_line" ] || [ "$before_line" -ge "$after_line" ]; then
        fail "$label"
    fi
    pass "$label"
}

reject_name_artifacts() {
    root=$1
    label=$2
    if find "$root" \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' \) \
        -print | grep . >/dev/null 2>&1; then
        find "$root" \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' \) \
            -print >&2
        fail "$label"
    fi
    pass "$label"
}

reject_df_python_bridge_source() {
    repo=$1
    if [ -e "${repo}/ui/scripts/deneb-df-bridge.py" ]; then
        fail "Deneb-owned Python Digital Factory bridge is not a runtime or source fallback"
    fi
    pass "Deneb-owned Python Digital Factory bridge source is absent"
}

reject_df_python_bridge_artifacts() {
    root=$1
    label=$2
    if find "$root" \( -name 'deneb-df-bridge.py' -o -name '*df-bridge.py' \) \
        -print | grep . >/dev/null 2>&1; then
        find "$root" \( -name 'deneb-df-bridge.py' -o -name '*df-bridge.py' \) \
            -print >&2
        fail "$label"
    fi
    pass "$label"
}

reject_df_standalone_bridge_artifact() {
    root=$1
    label=$2
    if find "$root" -name 'deneb-df-bridge' -print | grep . >/dev/null 2>&1; then
        find "$root" -name 'deneb-df-bridge' -print >&2
        fail "$label"
    fi
    pass "$label"
}

audit_source() {
    repo=$1

    require_file "${repo}/common/print/print_backend_route.c" "native route source exists"
    require_file "${repo}/common/print/print_backend_route.h" "native route header exists"
    require_file "${repo}/ui/build-package.sh" "package builder exists"
    require_file "${repo}/ui/installer/update.sh" "installer exists"
    require_file "${repo}/tools/build-update-release.ps1" "release wrapper exists"
    require_file "${repo}/printsvc/init/deneb-printsvc.init" "native printsvc init exists"
    require_file "${repo}/web/init/deneb-web.init" "web init exists"
    require_file "${repo}/web/src/df_bridge.c" "deneb-api Digital Factory command source exists"
    require_file "${repo}/web/src/df_bridge.h" "deneb-api Digital Factory command header exists"
    require_file "${repo}/dfsvc/src/main.c" "native Digital Factory connector source exists"
    require_file "${repo}/dfsvc/init/digitalfactory.init" "native Digital Factory init exists"
    reject_df_python_bridge_source "$repo"
    reject_pattern "${repo}/ui/CMakeLists.txt" \
        'deneb-df-bridge[.]c|df-bridge/deneb-df-bridge' \
        "deneb-ui build does not embed Digital Factory bridge source"
    reject_pattern "${repo}/ui/src/main.c" \
        'deneb_df_bridge_main|program_basename\(argv\[0\]\).*deneb-df-bridge' \
        "deneb-ui main does not dispatch external bridge mode"
    require_pattern "${repo}/web/CMakeLists.txt" \
        'src/df_bridge[.]c' \
        "deneb-api build includes Digital Factory command source"
    require_pattern "${repo}/web/src/main.c" \
        'digital-factory' \
        "deneb-api exposes local Digital Factory command mode"
    require_pattern "${repo}/web/src/main.c" \
        'deneb_df_bridge_run' \
        "deneb-api command mode invokes Digital Factory bridge"
    df_http_matches=$(grep -nE '/api/v1/deneb/digital_factory|digital_factory' \
        "${repo}/web/src/api_http.c" | \
        grep -Ev '/api/v1/deneb/digital_factory/(auth|unlink)|api_deneb_digital_factory_(auth_get|auth_post|unlink_post)' || true)
    if [ -n "$df_http_matches" ]; then
        fail "Digital Factory HTTP surface is limited to authenticated cloud auth/unlink endpoints"
    fi
    pass "Digital Factory HTTP surface is limited to authenticated cloud auth/unlink endpoints"

    require_pattern "${repo}/common/print/print_backend_route.c" \
        'deneb_print_backend_route\(DENEB_PRINT_BACKEND_NATIVE\)' \
        "shared route defaults to native printsvc"
    require_pattern "${repo}/common/print/print_backend_route.h" \
        '#define DENEB_PRINTSVC_STATUS_URL "tcp://127\.0\.0\.1:5555"' \
        "shared route exposes native status endpoint"
    require_pattern "${repo}/common/print/print_backend_route.h" \
        '#define DENEB_PRINTSVC_COMMAND_URL "tcp://127\.0\.0\.1:5556"' \
        "shared route exposes native command endpoint"

    if grep -rE "DENEB_PRINT_BACKEND_STOCK|STOCK_PRINT_BACKEND|print_service_route" \
        "${repo}/common/print" "${repo}/ui/src" "${repo}/web/src" "${repo}/printsvc/src" \
        >/dev/null 2>&1; then
        grep -rE "DENEB_PRINT_BACKEND_STOCK|STOCK_PRINT_BACKEND|print_service_route" \
            "${repo}/common/print" "${repo}/ui/src" "${repo}/web/src" "${repo}/printsvc/src" >&2 || true
        fail "no stock print backend selector remains in native clients"
    fi
    pass "no stock print backend selector remains in native clients"

    if grep -rE '/usr/bin/python3[[:space:]]+/home/cygnus/marlindriver/print_service\.py' \
        "${repo}/ui/src" "${repo}/ui/installer" "${repo}/ui/init" \
        "${repo}/web/src" "${repo}/web/init" \
        "${repo}/printsvc/src" "${repo}/printsvc/init" "${repo}/common/print" \
        >/dev/null 2>&1; then
        grep -rE '/usr/bin/python3[[:space:]]+/home/cygnus/marlindriver/print_service\.py' \
            "${repo}/ui/src" "${repo}/ui/installer" "${repo}/ui/init" \
            "${repo}/web/src" "${repo}/web/init" \
            "${repo}/printsvc/src" "${repo}/printsvc/init" "${repo}/common/print" >&2 || true
        fail "no Deneb runtime launches stock Python print_service.py"
    fi
    pass "no Deneb runtime launches stock Python print_service.py"

    require_pattern "${repo}/ui/build-package.sh" \
        "deneb-printsvc-native-audit" \
        "package builder runs de-Python audit"
    reject_pattern "${repo}/tools/build-update-release.ps1" \
        'zig.*deneb_df_bridge|build-exe.*deneb-df-bridge|ui/src/df-bridge' \
        "release wrapper does not build a standalone Digital Factory bridge"
    require_pattern "${repo}/ui/build-package.sh" \
        'deneb-df-bridge[.]py' \
        "package builder rejects Python Digital Factory bridge artifacts"
    require_pattern "${repo}/ui/build-package.sh" \
        'deneb-api digital-factory' \
        "package manifest documents deneb-api Digital Factory command mode"
    require_pattern "${repo}/ui/build-package.sh" \
        '![[:space:]]+grep -Eq.*deneb-df-bridge' \
        "package builder rejects standalone Digital Factory bridge"
    require_pattern "${repo}/ui/build-package.sh" \
        "deneb-printsvc-native-audit-selftest" \
        "package builder runs de-Python audit selftest"
    require_pattern "${repo}/ui/build-package.sh" \
        "deneb-printsvc-integration-audit" \
        "package builder runs integration audit"
    require_pattern "${repo}/ui/build-package.sh" \
        "deneb-printsvc-integration-audit-selftest" \
        "package builder runs integration audit selftest"
    require_pattern "${repo}/ui/build-package.sh" \
        "deneb-printsvc-release-gate-selftest" \
        "package builder runs release gate selftest"
    require_pattern "${repo}/ui/build-package.sh" \
        "deneb-printsvc-stability" \
        "package builder includes stability harness"
    require_pattern "${repo}/ui/build-package.sh" \
        "deneb-active-physical-soak-runner" \
        "package builder includes active physical soak runner"
    require_pattern "${repo}/ui/build-package.sh" \
        'deneb-printsvc-stability" --selftest' \
        "package builder runs stability selftest"
    require_pattern "${repo}/ui/build-package.sh" \
        'DENEB_PACKAGE_VERSION_OVERRIDE' \
        "package builder supports isolated package version override"
    require_pattern "${repo}/ui/build-package.sh" \
        "find \"\\\$STAGING_DIR\" .*print_service\\.py" \
        "package builder rejects Python driver artifacts"
    require_pattern "${repo}/ui/build-package.sh" \
        'DENEB_RELEASE_CHANNEL="\$\{DENEB_RELEASE_CHANNEL:-experimental\}"' \
        "package builder defaults native printsvc builds to experimental"
    require_pattern "${repo}/ui/build-package.sh" \
        'if \[ "\$DENEB_RELEASE_CHANNEL" != "experimental" \]' \
        "package builder gates non-experimental native printsvc builds"
    require_pattern "${repo}/ui/build-package.sh" \
        'DENEB_PRINTSVC_STOCK_SUMMARY' \
        "package builder requires stock smoke summary input"
    require_pattern "${repo}/ui/build-package.sh" \
        'DENEB_PRINTSVC_NATIVE_SUMMARY' \
        "package builder requires native smoke summary input"
    require_pattern "${repo}/ui/build-package.sh" \
        'DENEB_PRINTSVC_NATIVE_EVIDENCE_SUMMARIES' \
        "package builder accepts split native evidence summaries"
    require_pattern "${repo}/ui/build-package.sh" \
        'deneb-printsvc-smoke-verify" --native --idle --restart --boot-sync --client-proof --firmware-proof --complete-job --resources' \
        "package builder verifies primary native resource summary"
    require_pattern "${repo}/ui/build-package.sh" \
        'deneb-printsvc-smoke-verify" --full' \
        "package builder still verifies single full native smoke summary"
    require_pattern "${repo}/ui/build-package.sh" \
        'deneb-printsvc-smoke-verify" --stock --resources' \
        "package builder verifies stock smoke summary ownership/resources"
    require_pattern "${repo}/ui/build-package.sh" \
        'deneb-printsvc-smoke-compare" --require-reduction' \
        "package builder requires strict stock/native reduction comparison"
    require_pattern "${repo}/tools/build-update-release.ps1" \
        '\[ValidateSet\("experimental", "nightly", "stable"\)\]' \
        "release wrapper limits native printsvc release channels"
    require_pattern "${repo}/tools/build-update-release.ps1" \
        'Non-experimental release builds require -PrintsvcStockSummary and -PrintsvcNativeSummary' \
        "release wrapper rejects non-experimental builds without live summaries"
    require_pattern "${repo}/tools/build-update-release.ps1" \
        'DENEB_PRINTSVC_STOCK_SUMMARY=' \
        "release wrapper forwards stock smoke summary"
    require_pattern "${repo}/tools/build-update-release.ps1" \
        'DENEB_PRINTSVC_NATIVE_SUMMARY=' \
        "release wrapper forwards native smoke summary"
    require_pattern "${repo}/tools/build-update-release.ps1" \
        'DENEB_PRINTSVC_NATIVE_EVIDENCE_SUMMARIES=' \
        "release wrapper forwards split native evidence summaries"
    require_pattern "${repo}/tools/build-update-release.ps1" \
        'deneb-printsvc-release-gate-selftest' \
        "release wrapper runs release gate selftest"
    require_pattern "${repo}/tools/build-update-release.ps1" \
        'deneb-printsvc-integration-audit' \
        "release wrapper preserves integration audit"
    require_pattern "${repo}/tools/build-update-release.ps1" \
        'deneb-printsvc-integration-audit-selftest' \
        "release wrapper runs integration audit selftest"
    require_pattern "${repo}/tools/deneb-printsvc-release-gate-selftest.sh" \
        'DENEB_PACKAGE_VERSION_OVERRIDE="\$PACKAGE_VERSION"' \
        "release gate selftest isolates package staging"
    require_pattern "${repo}/tools/deneb-printsvc-release-gate-selftest.sh" \
        'rm -rf "\$PACKAGE_STAGING"' \
        "release gate selftest cleans package staging"
    require_pattern "${repo}/tools/deneb-printsvc-release-gate-selftest.sh" \
        'nightly_invalid_native_summary' \
        "release gate selftest rejects invalid native summary"
    require_pattern "${repo}/tools/deneb-printsvc-release-gate-selftest.sh" \
        'nightly_invalid_stock_summary' \
        "release gate selftest rejects invalid stock summary"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'physical-safety-gate' \
        "live smoke harness gates physical phases"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'DENEB_PRINTSVC_SMOKE_PHYSICAL_OK' \
        "live smoke harness requires physical acknowledgement"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'physical-bundle-safety-gate' \
        "live smoke harness gates bundled physical phases"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'DENEB_PRINTSVC_SMOKE_PHYSICAL_BUNDLE_OK' \
        "live smoke harness requires bundled physical acknowledgement"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'guarded_prehome' \
        "live smoke harness pre-homes before moving physical phases"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'reason=pre_physical_home' \
        "live smoke harness records pre-home evidence"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'physical_safety_plan' \
        "live smoke harness records physical safety plans"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'axes=\$axes required_home=\$required_home travel=\$travel stop_conditions=\$stop_conditions' \
        "live smoke harness records axes, homing, travel, and stop conditions"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'write_active_fixture' \
        "live smoke harness generates bounded active-job fixtures"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'write_preheat_abort_fixture' \
        "live smoke harness generates low-temperature preheat-abort fixtures"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'write_representative_fixture' \
        "live smoke harness generates bounded representative XYZ fixtures"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'representative-fixture-safety-gate' \
        "live smoke harness requires all-axis home for representative XYZ fixtures"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'wait_for_abort_idle' \
        "live smoke harness waits for aborts to settle idle"
    require_pattern "${repo}/tools/deneb-printsvc-smoke.sh" \
        'DENEB_PRINTSVC_SMOKE_ABORT_SETTLE_TIMEOUT' \
        "live smoke harness makes abort settle wait bounded"
    require_pattern "${repo}/tools/deneb-printsvc-smoke-selftest.sh" \
        'smoke_requires_physical_ack' \
        "smoke selftest covers physical safety gate"
    require_pattern "${repo}/tools/deneb-printsvc-smoke-selftest.sh" \
        'smoke_rejects_physical_bundle' \
        "smoke selftest covers physical bundle safety gate"
    require_pattern "${repo}/tools/deneb-printsvc-smoke-selftest.sh" \
        'verify_rejects_missing_physical_safety' \
        "smoke selftest covers missing physical safety plan"
    require_pattern "${repo}/tools/deneb-printsvc-smoke-selftest.sh" \
        'generated bounded Z active fixture' \
        "smoke selftest covers bounded active-job fixture generation"
    require_pattern "${repo}/tools/deneb-printsvc-smoke-selftest.sh" \
        'generated low-temperature preheat-abort fixture' \
        "smoke selftest covers preheat-abort fixture generation"
    require_pattern "${repo}/tools/deneb-printsvc-smoke-selftest.sh" \
        'generated bounded representative XYZ fixture' \
        "smoke selftest covers representative XYZ fixture generation"
    require_pattern "${repo}/tools/deneb-printsvc-smoke-selftest.sh" \
        'smoke_rejects_representative_without_all_axis_home' \
        "smoke selftest covers representative fixture all-axis-home gate"
    require_file "${repo}/tools/deneb-printsvc-stability.sh" \
        "stability harness exists"
    require_pattern "${repo}/tools/deneb-printsvc-stability.sh" \
        'native-no-restart' \
        "stability harness preserves native process lifetime"
    require_pattern "${repo}/tools/deneb-printsvc-stability.sh" \
        'complete-job requires --physical-ok' \
        "stability harness gates repeated jobs behind physical acknowledgement"
    require_file "${repo}/tools/deneb-active-physical-soak-runner.sh" \
        "active physical soak runner exists"
    require_pattern "${repo}/tools/deneb-active-physical-soak-runner.sh" \
        '.*--native-no-restart' \
        "active physical soak runner preserves native process lifetime"
    require_pattern "${repo}/tools/deneb-active-physical-soak-runner.sh" \
        '.*--heat --motion --macro home' \
        "active physical soak runner exercises heat, motion, and home macro"
    require_pattern "${repo}/tools/deneb-active-physical-soak-runner.sh" \
        '.*--complete-job "\$FIXTURE"' \
        "active physical soak runner repeats completed print jobs"
    require_file "${repo}/tools/deneb-printsvc-stock-baseline.sh" \
        "stock baseline helper exists"
    require_pattern "${repo}/UM2C_MODDING_CHECKLIST.md" \
        'Accepted bounded hardware evidence covers native route ownership,' \
        "checklist records accepted bounded hardware evidence"
    require_pattern "${repo}/docs/PRINTSVC_EVIDENCE_LEDGER.md" \
        '^\| Observe-only firmware/temperature parity \| Proven for paired stock/native capture \|' \
        "evidence ledger records paired firmware/ambient proof"
    require_pattern "${repo}/docs/PRINTSVC_EVIDENCE_LEDGER.md" \
        '/tmp/deneb-stock-d82245c[.]summary' \
        "evidence ledger cites accepted stock firmware/ambient summary"
    require_pattern "${repo}/docs/PRINTSVC_EVIDENCE_LEDGER.md" \
        '/tmp/deneb-native-d82245c-observe[.]summary' \
        "evidence ledger cites accepted native firmware/ambient summary"
    require_pattern "${repo}/docs/PRINTSVC_EVIDENCE_LEDGER.md" \
        '/tmp/deneb-84376b4-stability-complete5[.]summary' \
        "evidence ledger cites accepted repeated-job stability summary"
    require_pattern "${repo}/docs/PRINTSVC_EVIDENCE_LEDGER.md" \
        '^\| Short repeated-job stability \| Proven for five bounded Z-only completion jobs \|' \
        "evidence ledger records repeated-job stability proof"
    require_pattern "${repo}/docs/PRINTSVC_EVIDENCE_LEDGER.md" \
        '^\| Active physical soak memory behavior \| Investigated, not promotion-complete \|' \
        "evidence ledger keeps multi-hour stability gate open"
    require_pattern "${repo}/tools/deneb-printsvc-stock-baseline.sh" \
        'DENEB_PRINTSVC_STOCK_BASELINE_OK' \
        "stock baseline helper requires explicit stock-switch acknowledgement"
    require_pattern "${repo}/tools/deneb-printsvc-stock-baseline.sh" \
        'restore_native\(\)' \
        "stock baseline helper restores native printsvc"
    require_pattern "${repo}/tools/deneb-printsvc-stock-baseline.sh" \
        '/home/deneb/backups/deneb-ui/init/printserver\.orig' \
        "stock baseline helper uses backed-up stock printserver when available"
    require_pattern "${repo}/tools/deneb-printsvc-stock-baseline.sh" \
        '/rom/etc/init\.d/printserver' \
        "stock baseline helper falls back to read-only stock printserver"
    require_pattern "${repo}/tools/deneb-printsvc-stock-baseline.sh" \
        'wait_for_stock_api_ready "stock-api-ready"' \
        "stock baseline helper waits for stock API telemetry readiness"
    require_pattern "${repo}/ui/build-package.sh" \
        'deneb-printsvc-stock-baseline' \
        "package builder includes stock baseline helper"
    require_pattern "${repo}/ui/installer/update.sh" \
        'cp /tmp/update/deneb-printsvc-stock-baseline /usr/bin/deneb-printsvc-stock-baseline' \
        "installer preserves stock baseline helper"
    require_pattern "${repo}/ui/installer/update.sh" \
        'cp /tmp/update/deneb-printsvc-stability /usr/bin/deneb-printsvc-stability' \
        "installer preserves stability harness"
    require_pattern "${repo}/ui/installer/update.sh" \
        'cp /tmp/update/deneb-active-physical-soak-runner /usr/bin/deneb-active-physical-soak-runner' \
        "installer preserves active physical soak runner"
    require_pattern "${repo}/ui/installer/update.sh" \
        '/usr/bin/deneb-printsvc-stability --selftest' \
        "installer runs stability selftest"
    require_pattern "${repo}/web/init/deneb-web.init" \
        'procd_open_instance api' \
        "web init supervises API dependency before lighttpd"
    require_pattern "${repo}/web/init/deneb-web.init" \
        'procd_open_instance web' \
        "web init keeps lighttpd in a named procd instance"
    require_pattern "${repo}/web/init/deneb-web.init" \
        '/var/run/deneb-api\.sock' \
        "web init waits for API socket dependency"
    reject_pattern "${repo}/web/init/deneb-api.init" \
        'deneb[.]web[.]enabled' \
        "API init does not depend on unsupported web enable setting"
    reject_pattern "${repo}/web/init/deneb-web.init" \
        'deneb[.]web[.]enabled' \
        "web init does not depend on unsupported web enable setting"
    reject_pattern "${repo}/web/init/deneb-mdns.init" \
        'deneb[.]web[.]enabled' \
        "mDNS init does not depend on unsupported web enable setting"
    reject_pattern "${repo}/ui/installer/update.sh" \
        'deneb[.]web[.]enabled' \
        "installer does not write unsupported web enable setting"
    require_pattern "${repo}/ui/installer/update.sh" \
        "deneb-printsvc-native-audit" \
        "installer runs de-Python audit"
    require_pattern "${repo}/ui/installer/update.sh" \
        'rm -f /tmp/Deneb_Update_[*][.]deneb' \
        "installer removes stale temporary update archives"
    require_pattern "${repo}/ui/installer/update.sh" \
        'rm -f /usr/bin/deneb-df-bridge' \
        "installer removes stale standalone Digital Factory bridge"
    reject_pattern "${repo}/ui/installer/update.sh" \
        'ln -sf /usr/bin/deneb-ui /usr/bin/deneb-df-bridge' \
        "installer does not symlink Digital Factory bridge to deneb-ui"
    require_pattern "${repo}/ui/installer/update.sh" \
        'configure_digitalfactory_boot' \
        "installer gates Digital Factory service at boot"
    require_order "${repo}/ui/installer/update.sh" \
        '^[[:space:]]*install_web_runtime$' \
        '^[[:space:]]*configure_digitalfactory_boot$' \
        "installer configures Digital Factory boot after native init replacement"
    require_pattern "${repo}/ui/installer/update.sh" \
        'cp /tmp/update/deneb-dfsvc /usr/bin/deneb-dfsvc' \
        "installer installs native Digital Factory connector"
    require_pattern "${repo}/ui/installer/update.sh" \
        'cp /tmp/update/digitalfactory.init /etc/init.d/digitalfactory' \
        "installer replaces Digital Factory init with native service"
    require_pattern "${repo}/dfsvc/init/digitalfactory.init" \
        'uci -q get ultimaker[.]option[.]cluster_id' \
        "Digital Factory init preserves paired printers across boot"
    reject_pattern "${repo}/ui/installer/update.sh" \
        '/usr/bin/python3[[:space:]]+/home/cygnus/digitalfactory/connector[.]py' \
        "installer does not launch stock Python Digital Factory connector"
    require_pattern "${repo}/ui/installer/update.sh" \
        '/etc/init.d/digitalfactory disable' \
        "installer disables Digital Factory service when unconfigured"
    require_pattern "${repo}/ui/src/screens/screen_digital_factory.c" \
        '/etc/init.d/digitalfactory enable' \
        "Digital Factory screen enables service for explicit user setup"
    require_pattern "${repo}/ui/src/screens/screen_digital_factory.c" \
        '/usr/bin/deneb-api digital-factory connect' \
        "Digital Factory screen calls deneb-api connect command"
    require_pattern "${repo}/ui/src/screens/screen_digital_factory.c" \
        '/usr/bin/deneb-api digital-factory disconnect' \
        "Digital Factory screen calls deneb-api disconnect command"
    require_pattern "${repo}/ui/installer/update.sh" \
        "deneb-printsvc-native-audit-selftest" \
        "installer runs de-Python audit selftest"
    require_pattern "${repo}/ui/installer/update.sh" \
        "deneb-printsvc-integration-audit" \
        "installer runs integration audit"
    require_pattern "${repo}/ui/installer/update.sh" \
        "deneb-printsvc-integration-audit-selftest" \
        "installer requires integration audit selftest"
    require_pattern "${repo}/ui/installer/update.sh" \
        "deneb-printsvc-release-gate-selftest" \
        "installer requires release gate selftest"
    require_pattern "${repo}/ui/installer/update.sh" \
        '/etc/init.d/deneb-api restart' \
        "installer starts deneb-api after updating binaries"
    require_pattern "${repo}/ui/installer/update.sh" \
        '/etc/init.d/deneb-web restart' \
        "installer starts deneb-web after updating binaries"
    require_pattern "${repo}/ui/installer/update.sh" \
        'cp /tmp/update/deneb-printsvc-release-gate-selftest /usr/bin/deneb-printsvc-release-gate-selftest' \
        "installer preserves release gate selftest"
    require_pattern "${repo}/ui/installer/update.sh" \
        "native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction" \
        "installer checks native printsvc release gate"
}

audit_package_dir() {
    root=$1

    require_file "${root}/manifest.txt" "package manifest exists"
    require_file "${root}/deneb-printsvc" "package includes native printsvc"
    require_file "${root}/deneb-dfsvc" "package includes native Digital Factory connector"
    require_file "${root}/digitalfactory.init" "package includes native Digital Factory init"
    require_file "${root}/deneb-printsvc-smoke" "package includes live smoke harness"
    require_file "${root}/deneb-printsvc-smoke-verify" "package includes smoke verifier"
    require_file "${root}/deneb-printsvc-smoke-compare" "package includes smoke comparator"
    require_file "${root}/deneb-printsvc-smoke-selftest" "package includes smoke verifier selftest"
    require_file "${root}/deneb-printsvc-stability" "package includes stability harness"
    require_file "${root}/deneb-active-physical-soak-runner" "package includes active physical soak runner"
    require_file "${root}/deneb-printsvc-stock-baseline" "package includes stock baseline helper"
    require_file "${root}/deneb-printsvc-cli-selftest" "package includes native CLI selftest"
    require_file "${root}/deneb-printsvc-init-selftest" "package includes native init selftest"
    require_file "${root}/deneb-printsvc-release-gate-selftest" "package includes release gate selftest"
    require_file "${root}/deneb-printsvc-native-audit" "package includes de-Python audit"
    require_file "${root}/deneb-printsvc-native-audit-selftest" "package includes de-Python audit selftest"
    require_file "${root}/deneb-printsvc-integration-audit" "package includes integration audit"
    require_file "${root}/deneb-printsvc-integration-audit-selftest" "package includes integration audit selftest"
    require_dir "${root}/deneb-printsvc-macros" "package includes Deneb macro directory"
    require_file "${root}/LVGL_LICENSE_TLSF.txt" "package includes declared TLSF notice"
    reject_name_artifacts "$root" "package has no Python driver artifact names"
    reject_df_python_bridge_artifacts "$root" "package has no Python Digital Factory bridge artifact"
    reject_df_standalone_bridge_artifact "$root" "package has no standalone Digital Factory bridge artifact"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'physical-safety-gate' \
        "packaged smoke harness gates physical phases"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'DENEB_PRINTSVC_SMOKE_PHYSICAL_OK' \
        "packaged smoke harness requires physical acknowledgement"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'physical-bundle-safety-gate' \
        "packaged smoke harness gates bundled physical phases"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'DENEB_PRINTSVC_SMOKE_PHYSICAL_BUNDLE_OK' \
        "packaged smoke harness requires bundled physical acknowledgement"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'guarded_prehome' \
        "packaged smoke harness pre-homes before moving physical phases"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'reason=pre_physical_home' \
        "packaged smoke harness records pre-home evidence"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'physical_safety_plan' \
        "packaged smoke harness records physical safety plans"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'axes=\$axes required_home=\$required_home travel=\$travel stop_conditions=\$stop_conditions' \
        "packaged smoke harness records axes, homing, travel, and stop conditions"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'write_active_fixture' \
        "packaged smoke harness generates bounded active-job fixtures"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'write_preheat_abort_fixture' \
        "packaged smoke harness generates low-temperature preheat-abort fixtures"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'write_representative_fixture' \
        "packaged smoke harness generates bounded representative XYZ fixtures"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'representative-fixture-safety-gate' \
        "packaged smoke harness requires all-axis home for representative XYZ fixtures"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'wait_for_abort_idle' \
        "packaged smoke harness waits for aborts to settle idle"
    require_pattern "${root}/deneb-printsvc-smoke" \
        'DENEB_PRINTSVC_SMOKE_ABORT_SETTLE_TIMEOUT' \
        "packaged smoke harness makes abort settle wait bounded"
    require_pattern "${root}/deneb-printsvc-stability" \
        'native-no-restart' \
        "packaged stability harness preserves native process lifetime"
    require_pattern "${root}/deneb-printsvc-stability" \
        'complete-job requires --physical-ok' \
        "packaged stability harness gates repeated jobs behind physical acknowledgement"
    require_pattern "${root}/deneb-active-physical-soak-runner" \
        '.*--native-no-restart' \
        "packaged active soak runner preserves native process lifetime"
    require_pattern "${root}/deneb-active-physical-soak-runner" \
        '.*--heat --motion --macro home' \
        "packaged active soak runner exercises heat, motion, and home macro"
    require_pattern "${root}/deneb-active-physical-soak-runner" \
        '.*--complete-job "\$FIXTURE"' \
        "packaged active soak runner repeats completed print jobs"
    require_pattern "${root}/deneb-printsvc-stock-baseline" \
        'DENEB_PRINTSVC_STOCK_BASELINE_OK' \
        "packaged stock baseline helper requires explicit stock-switch acknowledgement"
    require_pattern "${root}/deneb-printsvc-stock-baseline" \
        'restore_native\(\)' \
        "packaged stock baseline helper restores native printsvc"
    require_pattern "${root}/deneb-printsvc-stock-baseline" \
        '/rom/etc/init\.d/printserver' \
        "packaged stock baseline helper can use read-only stock printserver"
    require_pattern "${root}/deneb-printsvc-stock-baseline" \
        'wait_for_stock_api_ready "stock-api-ready"' \
        "packaged stock baseline helper waits for stock API telemetry readiness"

    require_pattern "${root}/manifest.txt" '^native_printsvc: experimental$' \
        "package manifest marks native printsvc experimental"
    require_pattern "${root}/manifest.txt" \
        '^native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction$' \
        "package manifest carries non-experimental evidence gate"
    require_pattern "${root}/manifest.txt" \
        'no Python driver files are packaged; native deneb-printsvc owns marlindriver' \
        "package manifest records no-Python driver ownership"
}

audit_archive() {
    archive=$1
    tmp_dir="${TMPDIR:-/tmp}/deneb-native-audit.$$"
    trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM
    mkdir -p "$tmp_dir"
    tar -tf "$archive" > "${tmp_dir}/files.txt"

    if grep -Ei '(^|/).*\.py$|(^|/).*python.*|(^|/)print_service\.py$' "${tmp_dir}/files.txt"; then
        fail "archive has no Python driver artifact names"
    fi
    pass "archive has no Python driver artifact names"
    if grep -Ei '(^|/)deneb-df-bridge[.]py$|(^|/).*df-bridge[.]py$' "${tmp_dir}/files.txt"; then
        fail "archive has no Python Digital Factory bridge artifact"
    fi
    pass "archive has no Python Digital Factory bridge artifact"
    if grep -Ei '(^|/)deneb-df-bridge$' "${tmp_dir}/files.txt"; then
        fail "archive has no standalone Digital Factory bridge artifact"
    fi
    pass "archive has no standalone Digital Factory bridge artifact"

    require_pattern "${tmp_dir}/files.txt" '(^|/)deneb-printsvc$' \
        "archive includes native printsvc"
    require_pattern "${tmp_dir}/files.txt" '(^|/)deneb-dfsvc$' \
        "archive includes native Digital Factory connector"
    require_pattern "${tmp_dir}/files.txt" '(^|/)digitalfactory[.]init$' \
        "archive includes native Digital Factory init"
    require_pattern "${tmp_dir}/files.txt" '(^|/)deneb-printsvc-native-audit$' \
        "archive includes de-Python audit"
    require_pattern "${tmp_dir}/files.txt" '(^|/)deneb-printsvc-native-audit-selftest$' \
        "archive includes de-Python audit selftest"
    require_pattern "${tmp_dir}/files.txt" '(^|/)deneb-printsvc-stock-baseline$' \
        "archive includes stock baseline helper"
    require_pattern "${tmp_dir}/files.txt" '(^|/)deneb-printsvc-stability$' \
        "archive includes stability harness"
    require_pattern "${tmp_dir}/files.txt" '(^|/)deneb-printsvc-integration-audit$' \
        "archive includes integration audit"
    require_pattern "${tmp_dir}/files.txt" '(^|/)deneb-printsvc-integration-audit-selftest$' \
        "archive includes integration audit selftest"
    require_pattern "${tmp_dir}/files.txt" '(^|/)manifest.txt$' \
        "archive includes manifest"
    tar xf "$archive" -C "$tmp_dir"
    audit_package_dir "$tmp_dir"
}

if [ "$#" -ne 2 ]; then
    usage
fi

case "$1" in
    --source)
        audit_source "$2"
        ;;
    --package-dir)
        audit_package_dir "$2"
        ;;
    --archive)
        audit_archive "$2"
        ;;
    *)
        usage
        ;;
esac

printf 'deneb-printsvc de-Python audit passed\n'
