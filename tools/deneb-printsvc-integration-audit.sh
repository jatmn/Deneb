#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Static audit for native print-service client integration boundaries.

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

require_doc_row() {
    doc=$1
    label=$2
    pattern=$3
    require_pattern "$doc" "$pattern" "integration audit records ${label}"
}

audit_source() {
    repo=$1
    doc="${repo}/docs/PRINTSVC_INTEGRATION_AUDIT.md"

    require_file "$doc" "integration audit document exists"
    require_pattern "$doc" 'Placement decision' "integration audit records placement decision column"
    require_pattern "$doc" 'Native owner' "integration audit records native owner column"
    require_pattern "$doc" 'Compatibility boundary' "integration audit records compatibility boundary column"
    require_pattern "$doc" 'Removal condition' "integration audit records removal condition column"
    require_pattern "$doc" 'Evidence' "integration audit records evidence column"
    require_pattern "$doc" 'Remaining proof' "integration audit records remaining proof column"
    reject_pattern "$doc" 'TBD|TODO|unknown|Unknown' "integration audit has no placeholder rows"

    require_doc_row "$doc" 'LCD backend_comm' 'LCD `backend_comm`'
    require_doc_row "$doc" 'web backend_zmq' 'web `backend_zmq`'
    require_doc_row "$doc" 'REST api_print_job' 'REST `api_print_job`'
    require_doc_row "$doc" 'Cura api_cluster' 'Cura `api_cluster`'
    require_doc_row "$doc" 'REST api_printer' 'REST `api_printer`'
    require_doc_row "$doc" 'conflict and preheat bridges' 'conflict and preheat bridges'
    require_doc_row "$doc" 'pending-job metadata files' 'pending-job metadata files'
    require_doc_row "$doc" 'direct macro calls' 'direct macro calls'
    require_doc_row "$doc" 'direct raw G-code calls' 'direct raw G-code calls'
    require_doc_row "$doc" 'duplicated status classification' 'duplicated status classification'
    require_doc_row "$doc" 'diagnostics and error mapping' 'diagnostics and error mapping'
    require_doc_row "$doc" 'native deneb-printsvc callers' 'native `deneb-printsvc` callers'
    require_pattern "$doc" 'LCD `backend_comm`.*Client via shared helpers' \
        "integration audit decides LCD remains client via shared helpers"
    require_pattern "$doc" 'LCD `backend_comm`.*deneb_status_state_has_abort_context' \
        "integration audit records shared LCD abort-display helper"
    require_pattern "$doc" 'web `backend_zmq`.*Client via shared helpers' \
        "integration audit decides web backend remains client via shared helpers"
    require_pattern "$doc" 'REST `api_print_job`.*Client via shared helpers' \
        "integration audit decides print job API remains client via shared helpers"
    require_pattern "$doc" 'Cura `api_cluster`.*Client via shared helpers' \
        "integration audit decides Cura API remains client via shared helpers"
    require_pattern "$doc" 'REST `api_printer`.*Client via shared helpers' \
        "integration audit decides printer API remains client via shared helpers"
    require_pattern "$doc" 'conflict and preheat bridges.*Client via shared helpers' \
        "integration audit decides conflict/preheat bridges remain clients via shared helpers"
    require_pattern "$doc" 'pending-job metadata files.*Shared library/API boundary' \
        "integration audit decides pending-job files are shared boundary"
    require_pattern "$doc" 'duplicated status classification.*Shared library/API boundary' \
        "integration audit decides status classification is shared boundary"
    require_pattern "$doc" 'direct macro calls.*Native service-owned' \
        "integration audit decides macro resolution is native service-owned"
    require_pattern "$doc" 'direct raw G-code calls.*Native service-owned' \
        "integration audit decides raw G-code execution is native service-owned"
    require_pattern "$doc" 'diagnostics and error mapping.*Native service-owned' \
        "integration audit decides diagnostics ownership"
    require_pattern "$doc" 'native `deneb-printsvc` callers.*Native service-owned' \
        "integration audit decides native callers ownership"

    require_file "${repo}/ui/src/backend_comm.c" "LCD backend source exists"
    require_pattern "${repo}/ui/src/backend_comm.c" '#include "print_backend_route\.h"' "LCD backend uses shared route helper"
    require_pattern "${repo}/ui/src/backend_comm.c" '#include "status_state\.h"' "LCD backend uses shared status state"
    require_pattern "${repo}/ui/src/backend_comm.c" '#include "pending_job_dispatch\.h"' "LCD backend uses shared pending-job dispatch"
    require_pattern "${repo}/ui/src/backend_comm.c" '#include "pending_job_file\.h"' "LCD backend uses shared pending-job files"
    require_pattern "${repo}/ui/src/backend_comm.c" '#include "command_format\.h"' "LCD backend uses shared command formatting"
    require_pattern "${repo}/ui/src/backend_comm.c" '#include "print_state_rules\.h"' "LCD backend uses shared state rules"
    require_pattern "${repo}/ui/src/backend_comm.c" 'deneb_status_state_context_flags' "LCD backend gets context flags from shared status helper"
    require_pattern "${repo}/ui/src/backend_comm.c" 'deneb_status_state_has_abort_context' "LCD backend gets abort display context from shared status helper"

    require_file "${repo}/web/src/backend_zmq.c" "web backend source exists"
    require_pattern "${repo}/web/src/backend_zmq.c" '#include "print_backend_route\.h"' "web backend uses shared route helper"
    require_pattern "${repo}/web/src/backend_zmq.c" '#include "status_state\.h"' "web backend uses shared status state"
    require_pattern "${repo}/web/src/backend_zmq.c" '#include "pending_job_dispatch\.h"' "web backend uses shared pending-job dispatch"
    require_pattern "${repo}/web/src/backend_zmq.c" '#include "print_history\.h"' "web backend uses shared print history"
    require_pattern "${repo}/web/src/backend_zmq.c" '#include "print_state_rules\.h"' "web backend uses shared state rules"
    require_pattern "${repo}/web/src/backend_zmq.c" 'deneb_print_status_label_with_req' "web backend gets status labels from shared rules"

    require_file "${repo}/web/src/api_print_job.c" "print job API source exists"
    require_pattern "${repo}/web/src/api_print_job.c" '#include "pending_job_registration\.h"' "print job API uses shared pending-job registration"
    require_pattern "${repo}/web/src/api_print_job.c" '#include "print_action_dispatch\.h"' "print job API uses shared action dispatch"
    require_pattern "${repo}/web/src/api_print_job.c" '#include "print_job_summary\.h"' "print job API uses shared job summary"
    require_pattern "${repo}/web/src/api_print_job.c" '#include "print_state_rules\.h"' "print job API uses shared state rules"

    require_file "${repo}/web/src/api_cluster.c" "cluster API source exists"
    require_pattern "${repo}/web/src/api_cluster.c" '#include "pending_job_file\.h"' "cluster API uses shared pending-job files"
    require_pattern "${repo}/web/src/api_cluster.c" '#include "print_job_summary\.h"' "cluster API uses shared job summary"
    require_pattern "${repo}/web/src/api_cluster.c" '#include "print_profile\.h"' "cluster API uses shared print profile"
    require_pattern "${repo}/web/src/api_cluster.c" '#include "print_state_rules\.h"' "cluster API uses shared state rules"

    require_file "${repo}/web/src/api_printer.c" "printer API source exists"
    require_pattern "${repo}/web/src/api_printer.c" '#include "gcode_command\.h"' "printer API uses shared G-code command helper"
    require_pattern "${repo}/web/src/api_printer.c" '#include "manual_motion\.h"' "printer API uses shared motion helper"
    require_pattern "${repo}/web/src/api_printer.c" '#include "printer_status_response\.h"' "printer API uses shared status response helper"

    require_pattern "${repo}/ui/build-package.sh" \
        "deneb-printsvc-integration-audit" \
        "package builder carries integration audit"
    require_pattern "${repo}/ui/installer/update.sh" \
        "deneb-printsvc-integration-audit" \
        "installer preserves integration audit"
    require_pattern "${repo}/printsvc/CMakeLists.txt" \
        "deneb-printsvc-integration-audit" \
        "CTest registers integration audit"

    if grep -rE '/usr/bin/python3[[:space:]]+/home/cygnus/marlindriver/print_service\.py' \
        "${repo}/ui/src" "${repo}/web/src" "${repo}/printsvc/src" "${repo}/common/print" \
        >/dev/null 2>&1; then
        grep -rE '/usr/bin/python3[[:space:]]+/home/cygnus/marlindriver/print_service\.py' \
            "${repo}/ui/src" "${repo}/web/src" "${repo}/printsvc/src" "${repo}/common/print" >&2 || true
        fail "integration clients do not launch stock Python print_service.py"
    fi
    pass "integration clients do not launch stock Python print_service.py"
}

audit_package_dir() {
    root=$1
    require_file "${root}/deneb-printsvc-integration-audit" "package includes integration audit"
}

audit_archive() {
    archive=$1
    tmp_dir="${TMPDIR:-/tmp}/deneb-integration-audit.$$"
    trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM
    mkdir -p "$tmp_dir"
    tar -tf "$archive" > "${tmp_dir}/files.txt"
    require_pattern "${tmp_dir}/files.txt" '(^|/)deneb-printsvc-integration-audit$' \
        "archive includes integration audit"
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

printf 'deneb-printsvc integration audit passed\n'
