#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Host-fixture selftest for stock compile_all init service disable.
#
# Creates a synthetic /etc/init.d/compile_all plus /etc/rc.d/S90compile_all
# enablement link, simulates the installer prune_stock_compile_all logic, and
# verifies that:
#   - An enabled compile_all is stopped and disabled.
#   - Already-disabled compile_all is handled as a no-op.
#   - A missing compile_all init script (no rom, no overlay) is handled
#     gracefully.
#   - Rollback re-enable (restoring backup with cleared pycache) would
#     trigger a fresh compile_all on next boot.
#
# Usage:
#   ./deneb-compile-all-selftest

set -eu

TMP_DIR="${TMPDIR:-/tmp}/deneb-compile-all-selftest.$$"
FIXTURE_INIT="${TMP_DIR}/etc/init.d/compile_all"
FIXTURE_HOME="${TMP_DIR}/home/cygnus"
FIXTURE_BACKUP="${TMP_DIR}/home/deneb/backups/deneb-ui/compile_all.init.orig"
FIXTURE_ROM="${TMP_DIR}/rom/etc/init.d/compile_all"
FIXTURE_RC_DIR="${TMP_DIR}/etc/rc.d"

pass() { printf 'PASS: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT HUP INT TERM

write_stock_compile_all() {
    _path=$1
    mkdir -p "$(dirname "$_path")"
    cat > "$_path" <<'EOF'
#!/bin/sh /etc/rc.common
# Optimize the python code used by Cygnus
START=90

export PYTHON_CODE_PATH="/home/cygnus"

start() {
    if [ ! -d "${PYTHON_CODE_PATH}/__pycache__" ]; then
        /usr/bin/python3 -m compileall "${PYTHON_CODE_PATH}"
    fi
}
EOF
}

enable_fixture_compile_all() {
    _init=$1
    _rc_dir=$2
    mkdir -p "$_rc_dir"
    ln -sf "../init.d/compile_all" "${_rc_dir}/S90compile_all"
    [ -f "$_init" ] || fail "cannot enable fixture without init script"
}

disable_fixture_compile_all() {
    _rc_dir=$1
    rm -f "${_rc_dir}/S90compile_all"
}

fixture_compile_all_enabled() {
    _rc_dir=$1
    [ -e "${_rc_dir}/S90compile_all" ]
}

# ---------------------------------------------------------------------------
# Helper - mocked subset of the installer's prune_stock_compile_all logic
# ---------------------------------------------------------------------------

simulate_disable() {
    _dst=$1       # /etc/init.d/compile_all
    _src=$2       # /rom/etc/init.d/compile_all
    _backup=$3    # backup path
    _pycache=$4   # /home/cygnus/__pycache__  (may not exist)
    _rc_dir=$5    # /etc/rc.d

    if [ ! -f "$_dst" ] && [ ! -f "$_src" ]; then
        echo "stock compile_all init script not found; nothing to disable"
        return 0
    fi

    if fixture_compile_all_enabled "$_rc_dir"; then
        echo "stock compile_all is enabled - disabling and backing up"
        mkdir -p "$(dirname "$_backup")"
        if [ -f "$_dst" ]; then
            cp -f "$_dst" "$_backup"
        elif [ -f "$_src" ]; then
            cp -f "$_src" "$_backup"
        fi
        disable_fixture_compile_all "$_rc_dir"
        echo "compile_all disabled: stock /home/cygnus compileall will not run at boot"
        if [ -d "$_pycache" ]; then
            rm -rf "$_pycache"
            echo "cleared /home/cygnus/__pycache__ so re-enabled compile_all re-runs on next stock boot"
        fi
    else
        echo "stock compile_all is already disabled or not present"
    fi
}

# ---------------------------------------------------------------------------
# Phase 1 - Enabled fixture: active compile_all inside writable overlay
# ---------------------------------------------------------------------------

phase1_setup() {
    mkdir -p "$(dirname "$FIXTURE_BACKUP")" "$FIXTURE_HOME"
    mkdir -p "${FIXTURE_HOME}/__pycache__"

    write_stock_compile_all "$FIXTURE_INIT"
    enable_fixture_compile_all "$FIXTURE_INIT" "$FIXTURE_RC_DIR"
}

phase1_test() {
    phase1_setup

    log=$(simulate_disable "$FIXTURE_INIT" "$FIXTURE_INIT" "$FIXTURE_BACKUP" "${FIXTURE_HOME}/__pycache__" "$FIXTURE_RC_DIR")

    echo "$log" | grep -q 'compile_all is enabled - disabling and backing up' \
        || fail "phase1: should report enabled state"
    pass "phase1: reported enabled state"

    cmp -s "$FIXTURE_INIT" "$FIXTURE_BACKUP" \
        || fail "phase1: backup should preserve stock init script"
    pass "phase1: backed up original init script"

    fixture_compile_all_enabled "$FIXTURE_RC_DIR" \
        && fail "phase1: rc.d enablement link should be removed" || true
    pass "phase1: rc.d enablement link removed"

    [ -d "${FIXTURE_HOME}/__pycache__" ] && fail "phase1: pycache should be removed" || true
    pass "phase1: pycache directory removed"

    echo "$log" | grep -q 'compile_all disabled' \
        || fail "phase1: should log disabled state"
    pass "phase1: logged disabled state"
}

# ---------------------------------------------------------------------------
# Phase 2 - Already-disabled in writable overlay (rom exists but rc.d
#           enablement link was previously removed, e.g. reinstall).
# ---------------------------------------------------------------------------

phase2_setup() {
    rm -rf "$TMP_DIR"
    mkdir -p "$(dirname "$FIXTURE_BACKUP")" "$FIXTURE_HOME" "$FIXTURE_RC_DIR"

    write_stock_compile_all "$FIXTURE_ROM"
    write_stock_compile_all "$FIXTURE_INIT"
}

phase2_test() {
    phase2_setup

    log=$(simulate_disable "$FIXTURE_INIT" "$FIXTURE_ROM" "$FIXTURE_BACKUP" "/nonexistent/pycache" "$FIXTURE_RC_DIR")

    echo "$log" | grep -q 'already disabled or not present' \
        || fail "phase2: should report already-disabled state"
    pass "phase2: already-disabled init script handled correctly"
}

# ---------------------------------------------------------------------------
# Phase 3 - Missing entirely (no rom, no overlay). Simulates a non-stock
#           firmware or clean-slate target.
# ---------------------------------------------------------------------------

phase3_setup() {
    rm -rf "$TMP_DIR"
    mkdir -p "$TMP_DIR"
}

phase3_test() {
    phase3_setup

    log=$(simulate_disable "/nonexistent/compile_all" "/nonexistent/rom_compile_all" "$FIXTURE_BACKUP" "/nonexistent/pycache" "$FIXTURE_RC_DIR")

    echo "$log" | grep -q 'not found; nothing to disable' \
        || fail "phase3: should report not-found state"
    pass "phase3: missing init script handled gracefully"
}

# ---------------------------------------------------------------------------
# Phase 4 - Rollback safety: re-enable after disabled compile_all + cleared
#           pycache means stock compile_all will re-run on next boot.
# ---------------------------------------------------------------------------

phase4_setup() {
    rm -rf "$TMP_DIR"
    mkdir -p "$(dirname "$FIXTURE_BACKUP")" "$FIXTURE_HOME" "$FIXTURE_RC_DIR"
    # Do NOT create pycache. This simulates post-Deneb-install state where
    # prune_stock_compile_all has already cleared /home/cygnus/__pycache__.

    write_stock_compile_all "$FIXTURE_BACKUP"
    write_stock_compile_all "$FIXTURE_INIT"
}

simulate_compile_all_would_run() {
    _init=$1
    _pycache=$2
    _rc_dir=$3
    if [ ! -d "$_pycache" ] && [ -f "$_init" ] && fixture_compile_all_enabled "$_rc_dir"; then
        echo "compile_all would run on next boot (pycache absent, init enabled)"
    else
        echo "compile_all would NOT run"
    fi
}

phase4_test() {
    phase4_setup

    # Rollback: restore the backup init script and re-enable it.
    cp -f "$FIXTURE_BACKUP" "$FIXTURE_INIT"
    enable_fixture_compile_all "$FIXTURE_INIT" "$FIXTURE_RC_DIR"

    fixture_compile_all_enabled "$FIXTURE_RC_DIR" \
        || fail "phase4: restored init should be enabled via rc.d"
    pass "phase4: backup restore re-enables compile_all"

    [ -d "${FIXTURE_HOME}/__pycache__" ] && fail "phase4: pycache should still be absent after Deneb install" || true
    pass "phase4: pycache absent after Deneb install"

    log=$(simulate_compile_all_would_run "$FIXTURE_INIT" "${FIXTURE_HOME}/__pycache__" "$FIXTURE_RC_DIR")
    echo "$log" | grep -q 'would run' || fail "phase4: compile_all should run when pycache absent"
    pass "phase4: stock compile_all triggers correctly on rollback boot"
}

# ---------------------------------------------------------------------------
# Run all phases
# ---------------------------------------------------------------------------

echo "=== Phase 1: Enabled compile_all is disabled ==="
phase1_test

echo ""
echo "=== Phase 2: Already-disabled overlay ==="
phase2_test

echo ""
echo "=== Phase 3: Missing init script (no rom, no overlay) ==="
phase3_test

echo ""
echo "=== Phase 4: Rollback re-enable behavior ==="
phase4_test

echo ""
echo "deneb compile-all selftest passed"
