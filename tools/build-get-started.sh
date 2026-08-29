#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# Build the tar-backed Deneb get-started bootstrap package on Debian/Linux.
# Produces:
#   dist/Deneb_get_started.img
#   dist/Deneb_get_started.img.sha256

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
version=0.2.8
output_directory=dist

usage() {
    cat <<'USAGE'
usage: tools/build-get-started.sh [options]

Build Deneb_get_started.img for first-time stock-firmware bootstrap.

Options:
  --version VERSION          Package manifest version (default: 0.2.8)
  --output-directory DIR     Output directory relative to repo root or absolute
  -h, --help                 Show this help
USAGE
}

die() {
    echo "$*" >&2
    exit 1
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version)
            version=${2:?missing value}
            shift 2
            ;;
        --output-directory)
            output_directory=${2:?missing value}
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
done

# Keep this in lockstep with tools/build-ssh-bootstrap.ps1: first character
# must be alphanumeric so Linux and Windows builders accept the same tokens.
case "$version" in
    ''|[!A-Za-z0-9]*|*[!A-Za-z0-9._+-]*|*.)
        die "Invalid --version '$version'. Use a token of letters, digits, '.', '_', '+', or '-' (for example 0.2.8)."
        ;;
esac

package_dir=$repo_root/packages/ssh-bootstrap
branding_dir=$repo_root/assets/branding
rgb565_script=$repo_root/tools/png-to-rgb565.py
rgb565_digest_file=$branding_dir/deneb-splash.rgb565.sha256

if [ "${output_directory#/}" = "$output_directory" ]; then
    dist_dir=$repo_root/$output_directory
else
    dist_dir=$output_directory
fi

staging_root=$repo_root/build/ssh-bootstrap
staging_dir=$staging_root/Deneb_get_started_$version
artifact=$dist_dir/Deneb_get_started.img
checksum=$artifact.sha256

[ -d "$package_dir" ] || die "Package directory not found: $package_dir"
[ -d "$branding_dir" ] || die "Branding directory not found: $branding_dir"
[ -f "$rgb565_script" ] || die "Missing converter script: $rgb565_script"
[ -f "$rgb565_digest_file" ] || die "Missing expected RGB565 digest: $rgb565_digest_file"

if ! python3 -c 'import PIL' >/dev/null 2>&1; then
    die "Python 3 with locked Pillow is required. Install tools/bootstrap-requirements.txt with pip --require-hashes."
fi

rm -rf "$staging_root"
mkdir -p "$staging_dir" "$dist_dir"

cp "$package_dir/update.sh" "$staging_dir/update.sh"
cp "$package_dir/README.md" "$staging_dir/README.md"
cp "$package_dir/manifest.txt" "$staging_dir/manifest.txt"
cp "$branding_dir/deneb-boot-320x240.png" "$staging_dir/deneb-boot-320x240.png"
cp "$branding_dir/deneb-splash-128x102.jpg" "$staging_dir/deneb-splash-128x102.jpg"

python3 "$rgb565_script" \
    "$staging_dir/deneb-boot-320x240.png" \
    "$staging_dir/deneb-splash.rgb565"

rgb_size=$(wc -c < "$staging_dir/deneb-splash.rgb565")
[ "$rgb_size" -eq 153600 ] || die "RGB565 output size mismatch: expected 153600 bytes, got $rgb_size"
expected_rgb_hash=$(awk 'NR == 1 { print $1 }' "$rgb565_digest_file")
case "$expected_rgb_hash" in
    *[!0-9a-f]*|'') die "Invalid expected RGB565 SHA256 in $rgb565_digest_file" ;;
esac
[ "${#expected_rgb_hash}" -eq 64 ] || die "Invalid expected RGB565 SHA256 in $rgb565_digest_file"
actual_rgb_hash=$(sha256sum "$staging_dir/deneb-splash.rgb565" | awk '{ print $1 }')
[ "$actual_rgb_hash" = "$expected_rgb_hash" ] || \
    die "RGB565 digest mismatch: expected $expected_rgb_hash, got $actual_rgb_hash"

# Rewrite version without interpolating it into a sed replacement expression.
tmp_manifest=$(mktemp)
VERSION="$version" awk 'BEGIN { version = ENVIRON["VERSION"] } /^version=/ { $0 = "version=" version } { print }' \
    "$staging_dir/manifest.txt" > "$tmp_manifest"
mv "$tmp_manifest" "$staging_dir/manifest.txt"

# Normalize text payloads to LF for the target device.
for text_file in update.sh README.md manifest.txt; do
    tmp_text=$(mktemp)
    sed 's/\r$//' "$staging_dir/$text_file" > "$tmp_text"
    mv "$tmp_text" "$staging_dir/$text_file"
done
chmod 0755 "$staging_dir/update.sh"

temp_artifact=
temp_checksum=
previous_checksum=
cleanup_publish_outputs() {
    [ -z "$temp_artifact" ] || rm -f "$temp_artifact"
    [ -z "$temp_checksum" ] || rm -f "$temp_checksum"
    [ -z "$previous_checksum" ] || rm -f "$previous_checksum"
}
trap cleanup_publish_outputs EXIT

# Build and validate in the destination filesystem so an incomplete archive is
# never exposed at the final package path.
temp_artifact=$(mktemp "$dist_dir/.Deneb_get_started.img.XXXXXX")
temp_checksum=$(mktemp "$dist_dir/.Deneb_get_started.img.sha256.XXXXXX")

(
    cd "$staging_dir"
    tar -cf "$temp_artifact" \
        update.sh \
        README.md \
        manifest.txt \
        deneb-boot-320x240.png \
        deneb-splash-128x102.jpg \
        deneb-splash.rgb565
)

expected_members='update.sh
README.md
manifest.txt
deneb-boot-320x240.png
deneb-splash-128x102.jpg
deneb-splash.rgb565'
actual_members=$(tar -tf "$temp_artifact")
[ "$actual_members" = "$expected_members" ] || die "Bootstrap archive member validation failed"

hash=$(sha256sum "$temp_artifact" | awk '{print $1}')
printf '%s  %s\n' "$hash" "$temp_artifact" | sha256sum --check --status - || die "Bootstrap archive checksum validation failed"
printf '%s  %s\n' "$hash" "$(basename "$artifact")" > "$temp_checksum"
chmod 0644 "$temp_artifact" "$temp_checksum"

if [ -f "$checksum" ]; then
    previous_checksum=$(mktemp "$dist_dir/.Deneb_get_started.img.sha256.previous.XXXXXX")
    cp -p "$checksum" "$previous_checksum"
fi
mv -f "$temp_checksum" "$checksum"
temp_checksum=
if ! mv -f "$temp_artifact" "$artifact"; then
    if [ -n "$previous_checksum" ]; then
        mv -f "$previous_checksum" "$checksum"
        previous_checksum=
    else
        rm -f "$checksum"
    fi
    die "Failed to publish bootstrap archive"
fi
temp_artifact=
if [ -n "$previous_checksum" ]; then
    rm -f "$previous_checksum"
    previous_checksum=
fi

printf 'Built %s\n' "$artifact"
printf 'SHA256 %s\n' "$hash"
