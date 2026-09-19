#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
checker="${script_dir}/check-site-links.sh"
work="$(mktemp -d "${TMPDIR:-/tmp}/deneb-site-links.XXXXXX")"
cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT

repo="${work}/repo"
site="${repo}/website"
mkdir -p \
    "${site}/content/docs" \
    "${site}/static" \
    "${repo}/docs/touchscreen-screens" \
    "${repo}/assets/branding"

printf 'png' > "${repo}/docs/touchscreen-screens/home.png"
printf 'jpg' > "${repo}/assets/branding/banner.jpg"

write_good_content() {
    printf '# Hub\n' > "${site}/content/_index.md"
    printf '# Docs\n[ok](/docs/ok/)\n' > "${site}/content/docs/_index.md"
    printf '# Ok\n' > "${site}/content/docs/ok.md"
}

write_good_public() {
    mkdir -p "${site}/public/docs/ok" "${site}/public/images/screens" "${site}/public/images/branding"
    printf '<html></html>\n' > "${site}/public/index.html"
    printf '<html></html>\n' > "${site}/public/docs/index.html"
    printf '<h2 id=real-heading>Real</h2>\n' > "${site}/public/docs/ok/index.html"
    cp "${repo}/docs/touchscreen-screens/home.png" "${site}/public/images/screens/home.png"
    cp "${repo}/assets/branding/banner.jpg" "${site}/public/images/branding/banner.jpg"
}

assert_fails() {
    local label="$1"
    shift
    if bash "$checker" "$@" >/dev/null; then
        printf '%s\n' "expected failure: ${label}" >&2
        exit 1
    fi
}

assert_passes() {
    local label="$1"
    shift
    if ! bash "$checker" "$@"; then
        printf '%s\n' "expected pass: ${label}" >&2
        exit 1
    fi
}

write_good_content
assert_passes "valid docs page" --root "$site"

write_good_content
printf '[missing](/docs/no-such-page/)\n' > "${site}/content/docs/_index.md"
assert_fails "missing docs page" --root "$site"

write_good_content
printf '<a href="/docs/ok/">ok</a>\n' > "${site}/content/_index.md"
assert_passes "html href" --root "$site"

write_good_content
printf '{{< card link="/docs/nope/" title="x" >}}\n' > "${site}/content/docs/_index.md"
assert_fails "shortcode card" --root "$site"

write_good_content
printf '![x](/images/screens/home.png)\n' > "${site}/content/docs/ok.md"
assert_passes "screen image" --root "$site"

write_good_content
printf '<img src="/images/screens/missing.png">\n' > "${site}/content/docs/ok.md"
assert_fails "missing screen image" --root "$site"

write_good_content
printf '<img src="/images/branding/banner.jpg">\n' > "${site}/content/docs/ok.md"
assert_passes "branding image" --root "$site"

write_good_content
printf '[ok](/docs/ok/#missing-heading)\n' > "${site}/content/docs/_index.md"
assert_passes "source mode ignores fragments" --root "$site"

write_good_content
write_good_public
assert_passes "built page" --root "$site" --public "${site}/public"

write_good_content
write_good_public
printf '[ok](/docs/ok/#real-heading)\n' > "${site}/content/docs/_index.md"
assert_passes "built fragment" --root "$site" --public "${site}/public"

write_good_content
write_good_public
printf '[ok](/docs/ok/#missing-heading)\n' > "${site}/content/docs/_index.md"
assert_fails "missing built fragment" --root "$site" --public "${site}/public"

write_good_content
write_good_public
rm -rf "${site}/public/docs/ok"
assert_fails "content without built page" --root "$site" --public "${site}/public"

printf 'Site-root link self-test: PASS\n'
