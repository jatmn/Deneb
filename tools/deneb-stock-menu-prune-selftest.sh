#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Host-fixture selftest for stock menu UI prune.
#
# Creates a synthetic /home/cygnus/menu tree matching the stock structure,
# simulates the installer prune logic, and verifies that retained files
# survive while dormant UI files are removed.
#
# Usage:
#   ./deneb-stock-menu-prune-selftest

set -eu

SCRIPT_NAME=$(basename "$0")
TMP_DIR="${TMPDIR:-/tmp}/deneb-stock-menu-prune-selftest.$$"
FIXTURE_MENU="${TMP_DIR}/home/cygnus/menu"

pass() { printf 'PASS: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT HUP INT TERM

# ---------------------------------------------------------------------------
# Phase 1 — Build fixture mimicking the stock menu tree
# ---------------------------------------------------------------------------

build_fixture() {
    # Retained top-level files
    mkdir -p "$FIXTURE_MENU"
    cat > "$FIXTURE_MENU/menu_settings.py" <<'PYEOF'
import os
from pathlib import Path
ROOT = Path(os.getenv("S1_SIM_ROOT_PATH", "/"))
GCODE_DIR = ROOT / 'home/3D'
GCODE_FILE = GCODE_DIR / 'model.gcode'
FIRMWARE_WDIR = ROOT / 'tmp/firmware_files'
FW_IMG_COPY_TARGET = ROOT / "home/3D/update.img"
FW_IMG_EXTRACT_DIR = ROOT / "tmp/update"
UM_FIRMWARE_URL = "https://software.ultimaker.com/releases/firmware/214588"
UPDATE_CHECK_PERIOD = 3600
LAN_INTERFACE = 'eth0'
WLAN_INTERFACE = 'apcli0'
MACHINE_CONFIG = './machine_config.json'
PRINTER_PUBSUB = "tcp://127.0.0.1:5565"
PRINTER_RPC = "tcp://127.0.0.1:5566"
MAINLOOP_CYCLE_MS = 100
MAX_NUMBER_LVGL_OBJECT = 200
PYEOF

    cat > "$FIXTURE_MENU/machine_config.json" <<'EOF'
{"type":"Ultimaker S1","material":"PLA","min_x":0.0,"max_x":220.0,"min_y":0.0,"max_y":208.0,"min_z":0.0,"max_z":207.0}
EOF

    # Pruned top-level files
    for f in executor.py controldialog.py machine.py pylvgl.py screen.py style.py; do
        echo "# $f — stock UI module, pruned by Deneb" > "$FIXTURE_MENU/$f"
    done

    # Pruned directories with representative file trees
    mkdir -p "$FIXTURE_MENU/gui_companion"
    touch "$FIXTURE_MENU/gui_companion/__init__.py"
    touch "$FIXTURE_MENU/gui_companion/path_companion.py"
    touch "$FIXTURE_MENU/gui_companion/progress_bar_companion.py"

    mkdir -p "$FIXTURE_MENU/helpers"
    touch "$FIXTURE_MENU/helpers/backlight.py"
    touch "$FIXTURE_MENU/helpers/communication.py"

    mkdir -p "$FIXTURE_MENU/img"
    touch "$FIXTURE_MENU/img/images.py"
    touch "$FIXTURE_MENU/img/settings.png"
    touch "$FIXTURE_MENU/img/material.png"
    mkdir -p "$FIXTURE_MENU/img/unused"
    touch "$FIXTURE_MENU/img/unused/lan.png"

    mkdir -p "$FIXTURE_MENU/navigator/print"
    touch "$FIXTURE_MENU/navigator/print/print_navigator.py"
    touch "$FIXTURE_MENU/navigator/print/abort_print_navigator.py"
    mkdir -p "$FIXTURE_MENU/navigator/materials"
    touch "$FIXTURE_MENU/navigator/materials/load_material_navigator.py"
    mkdir -p "$FIXTURE_MENU/navigator/settings"
    touch "$FIXTURE_MENU/navigator/settings/about_navigator.py"
    mkdir -p "$FIXTURE_MENU/navigator/maintenance"
    touch "$FIXTURE_MENU/navigator/maintenance/diagnostics_navigator.py"
    touch "$FIXTURE_MENU/navigator/root_navigator.py"
    touch "$FIXTURE_MENU/navigator/menu_navigator.py"
    touch "$FIXTURE_MENU/navigator/navigator.py"
    touch "$FIXTURE_MENU/navigator/mixin.py"

    mkdir -p "$FIXTURE_MENU/screens"
    touch "$FIXTURE_MENU/screens/main_menu_page.py"
    touch "$FIXTURE_MENU/screens/print_browse_page.py"
    touch "$FIXTURE_MENU/screens/network_info_page.py"

    mkdir -p "$FIXTURE_MENU/templates"
    touch "$FIXTURE_MENU/templates/choice_screen.py"
    touch "$FIXTURE_MENU/templates/message_screen.py"

    mkdir -p "$FIXTURE_MENU/ui_elements"
    touch "$FIXTURE_MENU/ui_elements/button.py"
    touch "$FIXTURE_MENU/ui_elements/label.py"
    touch "$FIXTURE_MENU/ui_elements/bar.py"

    # __pycache__ in various locations
    mkdir -p "$FIXTURE_MENU/__pycache__"
    touch "$FIXTURE_MENU/__pycache__/menu_settings.cpython-36.pyc"
    mkdir -p "$FIXTURE_MENU/screens/__pycache__"
    touch "$FIXTURE_MENU/screens/__pycache__/main_menu_page.cpython-36.pyc"
    mkdir -p "$FIXTURE_MENU/navigator/__pycache__"
    touch "$FIXTURE_MENU/navigator/__pycache__/root_navigator.cpython-36.pyc"
}

# ---------------------------------------------------------------------------
# Phase 2 — Simulate installer prune logic
# ---------------------------------------------------------------------------

prune_fixture() {
    local menu_dir="$FIXTURE_MENU"

    # Keep menu_settings.py and machine_config.json.
    rm -rf \
        "${menu_dir}/executor.py" \
        "${menu_dir}/controldialog.py" \
        "${menu_dir}/machine.py" \
        "${menu_dir}/pylvgl.py" \
        "${menu_dir}/screen.py" \
        "${menu_dir}/style.py" \
        "${menu_dir}/gui_companion" \
        "${menu_dir}/helpers" \
        "${menu_dir}/img" \
        "${menu_dir}/navigator" \
        "${menu_dir}/screens" \
        "${menu_dir}/templates" \
        "${menu_dir}/ui_elements"

    find "${menu_dir}" -type d -name __pycache__ -prune -exec rm -rf {} + 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# Phase 3 — Assertions
# ---------------------------------------------------------------------------

verify_retained() {
    # Retained top-level files must exist
    for f in menu_settings.py machine_config.json; do
        if [ ! -f "${FIXTURE_MENU}/${f}" ]; then
            fail "retained file missing after prune: ${f}"
        fi
        pass "retained file present: ${f}"
    done
}

verify_pruned() {
    # Pruned top-level files must be gone
    for f in executor.py controldialog.py machine.py pylvgl.py screen.py style.py; do
        if [ -f "${FIXTURE_MENU}/${f}" ]; then
            fail "pruned file still present: ${f}"
        fi
        pass "pruned file absent: ${f}"
    done

    # Pruned directories must be gone (surface and deep)
    for d in gui_companion helpers img navigator screens templates ui_elements; do
        if [ -d "${FIXTURE_MENU}/${d}" ]; then
            fail "pruned directory still present: ${d}/"
        fi
        pass "pruned directory absent: ${d}/"
    done

    # Deep paths inside pruned directories must also be gone
    if [ -f "${FIXTURE_MENU}/navigator/print/print_navigator.py" ]; then
        fail "deep pruned file still present: navigator/print/print_navigator.py"
    fi
    pass "deep pruned file absent (navigator/print/)"

    if [ -f "${FIXTURE_MENU}/screens/main_menu_page.py" ]; then
        fail "deep pruned file still present: screens/main_menu_page.py"
    fi
    pass "deep pruned file absent (screens/)"

    if [ -f "${FIXTURE_MENU}/img/unused/lan.png" ]; then
        fail "deep pruned file still present: img/unused/lan.png"
    fi
    pass "deep pruned file absent (img/unused/)"

    # __pycache__ directories must be gone
    if [ -d "${FIXTURE_MENU}/__pycache__" ]; then
        fail "pruned __pycache__ still present at top level"
    fi
    pass "top-level __pycache__ absent"

    if [ -d "${FIXTURE_MENU}/screens/__pycache__" ]; then
        fail "pruned __pycache__ still present in screens/"
    fi
    pass "nested __pycache__ absent"
}

verify_retained_content() {
    # Quick content integrity check on retained files
    if ! grep -q 'GCODE_DIR' "${FIXTURE_MENU}/menu_settings.py"; then
        fail "retained menu_settings.py missing expected content"
    fi
    pass "retained menu_settings.py content verified"

    if ! grep -q 'Ultimaker S1' "${FIXTURE_MENU}/machine_config.json"; then
        fail "retained machine_config.json missing expected content"
    fi
    pass "retained machine_config.json content verified"
}

verify_prune_idempotent() {
    # Running prune again should not change the retained set
    prune_fixture
    for f in menu_settings.py machine_config.json; do
        if [ ! -f "${FIXTURE_MENU}/${f}" ]; then
            fail "idempotent prune removed retained file: ${f}"
        fi
    done
    pass "prune is idempotent (retained files survive re-run)"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

echo "=== Stock Menu Prune Selftest ==="
echo "Fixture: ${FIXTURE_MENU}"

build_fixture
echo "--- Fixture built ---"

prune_fixture
echo "--- Prune applied ---"

verify_retained
verify_pruned
verify_retained_content
verify_prune_idempotent

echo "=== All tests passed ==="