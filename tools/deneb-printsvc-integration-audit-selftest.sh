#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Negative fixtures for deneb-printsvc-integration-audit.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TMP_DIR="${TMPDIR:-/tmp}/deneb-integration-audit-selftest.$$"
AUDIT="${SCRIPT_DIR}/deneb-printsvc-integration-audit.sh"

if [ ! -f "$AUDIT" ]; then
    AUDIT="${SCRIPT_DIR}/deneb-printsvc-integration-audit"
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
        echo "FAIL: expected integration audit failure: $label" >&2
        exit 1
    fi
    echo "PASS: expected integration audit failure: $label"
}

write_valid_source() {
    root="$1"
    mkdir -p "$root/docs" "$root/ui/src" "$root/web/src" "$root/printsvc/src" "$root/common/print" "$root/tools" "$root/ui/installer"
    cat > "$root/docs/PRINTSVC_INTEGRATION_AUDIT.md" <<'EOF'
| Integration | Placement decision | Native owner | Compatibility boundary | Removal condition | Evidence | Remaining proof |
| --- | --- | --- | --- | --- | --- | --- |
| LCD `backend_comm` | Client via shared helpers | owner | boundary | removal | deneb_status_state_has_abort_context | proof |
| web `backend_zmq` | Client via shared helpers | owner | boundary | removal | evidence | proof |
| REST `api_print_job` | Client via shared helpers | owner | boundary | removal | evidence | proof |
| Cura `api_cluster` | Client via shared helpers | owner | boundary | removal | evidence | proof |
| REST `api_printer` | Client via shared helpers | owner | boundary | removal | evidence | proof |
| conflict and preheat bridges | Client via shared helpers | owner | boundary | removal | evidence | proof |
| pending-job metadata files | Shared library/API boundary | owner | boundary | removal | evidence | proof |
| direct macro calls | Native service-owned | owner | boundary | removal | evidence | proof |
| direct raw G-code calls | Native service-owned | owner | boundary | removal | evidence | proof |
| duplicated status classification | Shared library/API boundary | owner | boundary | removal | evidence | proof |
| diagnostics and error mapping | Native service-owned | owner | boundary | removal | evidence | proof |
| native `deneb-printsvc` callers | Native service-owned | owner | boundary | removal | evidence | proof |
EOF
    cat > "$root/ui/src/backend_comm.c" <<'EOF'
#include "print_backend_route.h"
#include "status_state.h"
#include "pending_job_dispatch.h"
#include "pending_job_file.h"
#include "command_format.h"
#include "print_state_rules.h"
int uses_flags(void) { return deneb_status_state_context_flags(0, 0).has_active_context; }
int uses_abort_context(void) { return deneb_status_state_has_abort_context(0, 0); }
int uses_transition(void) { deneb_status_transition_t t; return deneb_status_state_transition_from_pair(&t, 0, 0); }
int uses_preheat(void) { return deneb_status_state_preheat_events(0, 0); }
EOF
    cat > "$root/web/src/backend_zmq.c" <<'EOF'
#include "print_backend_route.h"
#include "status_state.h"
#include "pending_job_dispatch.h"
#include "print_history.h"
#include "print_state_rules.h"
const char *uses_label(void) { return deneb_print_status_label_with_req(1, 0, 0, 0, "", 0); }
int uses_transition(void) { deneb_status_transition_t t; return deneb_status_state_transition_from_pair(&t, 0, 0); }
int uses_preheat(void) { return deneb_status_state_preheat_events(0, 0); }
EOF
    cat > "$root/web/src/api_print_job.c" <<'EOF'
#include "pending_job_registration.h"
#include "print_action_dispatch.h"
#include "print_job_summary.h"
#include "print_state_rules.h"
EOF
    cat > "$root/web/src/api_cluster.c" <<'EOF'
#include "pending_job_file.h"
#include "print_job_summary.h"
#include "print_profile.h"
#include "print_state_rules.h"
EOF
    cat > "$root/web/src/api_printer.c" <<'EOF'
#include "gcode_command.h"
#include "manual_motion.h"
#include "printer_status_response.h"
EOF
    cat > "$root/ui/build-package.sh" <<'EOF'
deneb-printsvc-integration-audit
EOF
    cat > "$root/ui/installer/update.sh" <<'EOF'
deneb-printsvc-integration-audit
EOF
    cat > "$root/printsvc/CMakeLists.txt" <<'EOF'
deneb-printsvc-integration-audit
EOF
    touch "$root/common/print/placeholder.c" "$root/printsvc/src/placeholder.c" "$root/tools/placeholder.sh"
}

write_valid_package() {
    root="$1"
    mkdir -p "$root"
    touch "$root/deneb-printsvc-integration-audit"
}

VALID_SOURCE="$TMP_DIR/source-valid"
write_valid_source "$VALID_SOURCE"
"$AUDIT" --source "$VALID_SOURCE" >/tmp/deneb-integration-audit-selftest-source-valid.log
echo "PASS: valid source fixture accepted"

MISSING_ROW="$TMP_DIR/source-missing-row"
write_valid_source "$MISSING_ROW"
grep -v 'REST `api_printer`' "$MISSING_ROW/docs/PRINTSVC_INTEGRATION_AUDIT.md" > "$MISSING_ROW/docs/audit.tmp"
mv "$MISSING_ROW/docs/audit.tmp" "$MISSING_ROW/docs/PRINTSVC_INTEGRATION_AUDIT.md"
expect_failure rejects_missing_doc_row "$AUDIT" --source "$MISSING_ROW"

PLACEHOLDER="$TMP_DIR/source-placeholder"
write_valid_source "$PLACEHOLDER"
cat >> "$PLACEHOLDER/docs/PRINTSVC_INTEGRATION_AUDIT.md" <<'EOF'
TBD
EOF
expect_failure rejects_placeholder_doc "$AUDIT" --source "$PLACEHOLDER"

MISSING_DECISION="$TMP_DIR/source-missing-decision"
write_valid_source "$MISSING_DECISION"
sed 's/Client via shared helpers/missing decision/' "$MISSING_DECISION/docs/PRINTSVC_INTEGRATION_AUDIT.md" > "$MISSING_DECISION/docs/audit.tmp"
mv "$MISSING_DECISION/docs/audit.tmp" "$MISSING_DECISION/docs/PRINTSVC_INTEGRATION_AUDIT.md"
expect_failure rejects_missing_placement_decision "$AUDIT" --source "$MISSING_DECISION"

MISSING_HELPER="$TMP_DIR/source-missing-helper"
write_valid_source "$MISSING_HELPER"
grep -v 'print_action_dispatch.h' "$MISSING_HELPER/web/src/api_print_job.c" > "$MISSING_HELPER/web/src/api_print_job.tmp"
mv "$MISSING_HELPER/web/src/api_print_job.tmp" "$MISSING_HELPER/web/src/api_print_job.c"
expect_failure rejects_missing_shared_helper "$AUDIT" --source "$MISSING_HELPER"

MISSING_LCD_ABORT_HELPER="$TMP_DIR/source-missing-lcd-abort-helper"
write_valid_source "$MISSING_LCD_ABORT_HELPER"
grep -v 'deneb_status_state_has_abort_context' "$MISSING_LCD_ABORT_HELPER/ui/src/backend_comm.c" > "$MISSING_LCD_ABORT_HELPER/ui/src/backend_comm.tmp"
mv "$MISSING_LCD_ABORT_HELPER/ui/src/backend_comm.tmp" "$MISSING_LCD_ABORT_HELPER/ui/src/backend_comm.c"
expect_failure rejects_missing_lcd_abort_helper "$AUDIT" --source "$MISSING_LCD_ABORT_HELPER"

MISSING_LCD_TRANSITION_HELPER="$TMP_DIR/source-missing-lcd-transition-helper"
write_valid_source "$MISSING_LCD_TRANSITION_HELPER"
grep -v 'deneb_status_state_transition_from_pair' "$MISSING_LCD_TRANSITION_HELPER/ui/src/backend_comm.c" > "$MISSING_LCD_TRANSITION_HELPER/ui/src/backend_comm.tmp"
mv "$MISSING_LCD_TRANSITION_HELPER/ui/src/backend_comm.tmp" "$MISSING_LCD_TRANSITION_HELPER/ui/src/backend_comm.c"
expect_failure rejects_missing_lcd_transition_helper "$AUDIT" --source "$MISSING_LCD_TRANSITION_HELPER"

MISSING_WEB_PREHEAT_HELPER="$TMP_DIR/source-missing-web-preheat-helper"
write_valid_source "$MISSING_WEB_PREHEAT_HELPER"
grep -v 'deneb_status_state_preheat_events' "$MISSING_WEB_PREHEAT_HELPER/web/src/backend_zmq.c" > "$MISSING_WEB_PREHEAT_HELPER/web/src/backend_zmq.tmp"
mv "$MISSING_WEB_PREHEAT_HELPER/web/src/backend_zmq.tmp" "$MISSING_WEB_PREHEAT_HELPER/web/src/backend_zmq.c"
expect_failure rejects_missing_web_preheat_helper "$AUDIT" --source "$MISSING_WEB_PREHEAT_HELPER"

PYTHON_LAUNCH="$TMP_DIR/source-python-launch"
write_valid_source "$PYTHON_LAUNCH"
cat >> "$PYTHON_LAUNCH/web/src/backend_zmq.c" <<'EOF'
const char *bad_driver = "/usr/bin/python3 /home/cygnus/marlindriver/print_service.py";
EOF
expect_failure rejects_python_driver_launch "$AUDIT" --source "$PYTHON_LAUNCH"

VALID_PACKAGE="$TMP_DIR/package-valid"
write_valid_package "$VALID_PACKAGE"
"$AUDIT" --package-dir "$VALID_PACKAGE" >/tmp/deneb-integration-audit-selftest-package-valid.log
echo "PASS: valid package fixture accepted"

MISSING_PACKAGE_TOOL="$TMP_DIR/package-missing-tool"
mkdir -p "$MISSING_PACKAGE_TOOL"
expect_failure rejects_missing_package_tool "$AUDIT" --package-dir "$MISSING_PACKAGE_TOOL"

ARCHIVE_DIR="$TMP_DIR/archive-valid"
write_valid_package "$ARCHIVE_DIR"
(cd "$ARCHIVE_DIR" && tar cf "$TMP_DIR/archive-valid.deneb" .)
"$AUDIT" --archive "$TMP_DIR/archive-valid.deneb" >/tmp/deneb-integration-audit-selftest-archive-valid.log
echo "PASS: valid archive fixture accepted"

echo "deneb-printsvc integration audit selftest passed"
