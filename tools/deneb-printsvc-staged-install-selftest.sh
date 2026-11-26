#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

# Exercise the install checks and copies without starting printer services.
set -eu

repo="$(cd "$(dirname "$0")/.." && pwd)"
binary="$(realpath "$1")"
fixture="$(mktemp -d "${TMPDIR:-/tmp}/deneb-staged-install.XXXXXX")"
trap 'rm -rf "$fixture"' EXIT
trap 'exit 1' HUP INT TERM
cd "$fixture"
stage=tmp/update
mkdir -p "$stage/www" "$stage/deneb-printsvc-macros" etc/init.d etc/config usr/bin usr/sbin

for tool in deneb-printsvc-smoke deneb-printsvc-smoke-verify deneb-printsvc-smoke-compare \
    deneb-printsvc-smoke-selftest deneb-printsvc-stability deneb-active-physical-soak-runner \
    deneb-printsvc-stock-baseline deneb-printsvc-cli-selftest deneb-printsvc-init-selftest \
    deneb-printsvc-native-audit deneb-printsvc-native-audit-selftest deneb-printsvc-integration-audit; do
    cp "$repo/tools/$tool.sh" "$stage/$tool"
    chmod 0755 "$stage/$tool"
done
cp "$repo/tools/deneb-python-runtime-inventory.sh" "$stage/deneb-runtime-inventory"
cp "$repo/ui/installer/update.sh" "$stage/update.sh"
cp "$binary" "$stage/deneb-printsvc"
touch "$stage/deneb-ui" "$stage/deneb-ui.init" "$stage/deneb-api" "$stage/deneb-dfsvc" \
    "$stage/deneb-mdns" "$stage/lighttpd" "$stage/lighttpd.conf" "$stage/en.json" \
    "$stage/LVGL_LICENSE_TLSF.txt" "$stage/LODEPNG_LICENSE.txt" \
    "$stage/deneb-printsvc-macros/default.gcode" "$stage/www/index.html"
for service in deneb-api deneb-web deneb-mdns deneb-printsvc digitalfactory; do
    printf '#!/bin/sh\nexit 0\n' > "$stage/$service.init"
done
# Leave the real init content readable by the ownership audit, with service
# invocations stopped by the fixture script's immediate exit above.
tr -d '\r' < "$repo/printsvc/init/deneb-printsvc.init" >> "$stage/deneb-printsvc.init"
awk '
    /cat > "\$\{printserver_init\}" <<'\''EOF'\''/ { shim = 1; next }
    shim && /^EOF$/ { exit }
    shim { print }
' "$stage/update.sh" > etc/init.d/printserver
cat > "$stage/manifest.txt" <<'MANIFEST'
channel: experimental
native_printsvc: experimental
native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction
no Python driver files are packaged; native deneb-printsvc owns marlindriver
MANIFEST

# Load only the exercised installer functions and redirect their paths.
# The original update.sh remains in staging for the init ownership audit.
awk '
    /^(validate_package|install_web_runtime|smoke_test_printsvc_tools|smoke_test_printsvc_init_handoff|remove_legacy_install_tool|remove_legacy_validation_tools)\(\) \{$/ { copy = 1 }
    copy { print }
    /^\}$/ { copy = 0 }
' "$stage/update.sh" | sed \
    -e 's|/tmp/|./tmp/|g' -e 's|/usr/bin/|./usr/bin/|g' \
    -e 's|/usr/sbin/|./usr/sbin/|g' -e 's|/etc/|./etc/|g' \
    -e 's| /www/| ./www/|g' -e "s|0:0:755|$(id -u):$(id -g):755|g" > installer-functions.sh
# shellcheck disable=SC1091
. ./installer-functions.sh
log() { printf '%s\n' "$*"; }
uci() { return 1; }
export DENEB_BACKUP_DIR=./backup

validate_package
install_web_runtime
smoke_test_printsvc_tools
smoke_test_printsvc_init_handoff

# All permanent diagnostics still exist. Fresh installs write only the
# runtime files, so staged checks cannot accidentally use installed copies.
for installed in deneb-printsvc-smoke deneb-printsvc-smoke-verify deneb-printsvc-smoke-compare \
    deneb-printsvc-stability deneb-active-physical-soak-runner deneb-printsvc-stock-baseline deneb-runtime-inventory; do
    cmp "$stage/$installed" "usr/bin/$installed"
done
for staged in deneb-printsvc-native-audit deneb-printsvc-integration-audit \
    deneb-printsvc-smoke-selftest deneb-printsvc-cli-selftest \
    deneb-printsvc-init-selftest deneb-printsvc-native-audit-selftest; do
    test ! -e "usr/bin/$staged"
done

# A missing sibling fails preflight and the staged smoke selftest.
mv "$stage/deneb-printsvc-smoke-verify" verify.saved
if (validate_package) > preflight.log 2>&1 || (smoke_test_printsvc_tools) > smoke.log 2>&1; then
    echo "FAIL: installer accepted a missing staged smoke verifier" >&2
    exit 1
fi
grep -Fq 'ERROR: missing required file: deneb-printsvc-smoke-verify' preflight.log
grep -Fq 'ERROR: deneb-printsvc smoke tool selftest failed' smoke.log
mv verify.saved "$stage/deneb-printsvc-smoke-verify"

# A clean old copy can go. Keep an edited copy, a permission override, and
# an operator-owned link even when the name belongs to Deneb.
legacy='usr/bin/deneb-printsvc-cli-selftest'
# Exact packaged bytes from the previous installer at baea878df665.
legacy_fixture="$repo/tools/fixtures/deneb-printsvc-cli-selftest.legacy"
cp "$legacy_fixture" "$legacy"
chmod 0755 "$legacy"
remove_legacy_validation_tools
if [ -e "$legacy" ]; then
    echo "FAIL: installer kept the known previous CLI selftest" >&2
    exit 1
fi
cp "$legacy_fixture" "$legacy"
chmod 0700 "$legacy"
remove_legacy_validation_tools
test -f "$legacy"
chmod 0755 "$legacy"
printf '\n# operator change\n' >> "$legacy"
remove_legacy_validation_tools
grep -Fq '# operator change' "$legacy"
rm "$legacy"
ln -s "$legacy_fixture" "$legacy"
remove_legacy_validation_tools
test -L "$legacy"

echo "deneb-printsvc staged install selftest passed"
