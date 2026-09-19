# SPDX-License-Identifier: MPL-2.0

# Public site (deneb3d.dev)

Hugo + Hextra sources for the public landing page and operator guides.
This tree is not a printer runtime.

## Build

Requires Hugo extended 0.166.0 (see `vendor-pins/hugo.version`) and Go for
Hugo modules. Python is not used.

Run from this directory (`website/`):

```sh
export HUGO_BIN=/path/to/hugo
bash scripts/build.sh
```

From the repository root, use `bash website/scripts/build.sh` instead.

`scripts/prepare-assets.sh` downloads pinned FlexSearch into
`assets/js/vendor/` (gitignored) after checking the SHA-256 pin.

`scripts/check-site-links.sh` fails if a site-root `/docs/` or
`/images/` target in `content/` does not map onto a content page or
hugo.yaml mount. `scripts/build.sh` runs that check again against
`public/` so missing built routes and heading fragments fail the
Website job. `tools/check-publication-boundary.ps1` runs the same
source check (and `scripts/check-site-links-selftest.sh`) so broken
wiki navigation fails the required policy gate even when Website
`build` is not a branch-protection context.

## Deploy

GitHub Actions workflow `.github/workflows/website.yml` builds on changes to
this tree, `docs/GETTING_STARTED.md` (bootstrap host-package contract), and
related assets, then on `main` uploads `website/public/` to a Bunny Storage
Zone and purges the Pull Zone.

Repository secrets:

- `BUNNY_STORAGE_ZONE`
- `BUNNY_STORAGE_PASSWORD`
- `BUNNY_STORAGE_ENDPOINT` (example: `https://storage.bunnycdn.com`)
- `BUNNY_API_KEY`
- `BUNNY_PULL_ZONE_ID`

Disable Bunny Pull Zone JavaScript minification/rewriting so FlexSearch
subresource integrity still matches.

The first production deploy overlays files into Bunny Storage. Removed pages
are not deleted automatically; purge the zone or delete stale objects if a
path is retired.

Operator guides under `website/content/docs/` are the public copies. In-repo
`docs/GETTING_STARTED.md` keeps the CI-guarded bootstrap host-package list;
`website/content/docs/getting-started.md` Step 2 must stay aligned with it.
`tools/ssh-bootstrap-patch-selftest.sh` greps both. Do not recopy operator
how-to into `docs/`.
