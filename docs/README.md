# Deneb Documentation

This directory is organized by purpose so old investigations do not look like
current project status.

## Start here

Public operator guides live at [deneb3d.dev](https://deneb3d.dev/).

1. [Getting started](https://deneb3d.dev/docs/getting-started/) — first install
   from stock UltiMaker firmware through bootstrap and the initial `.deneb`
   package.
2. [Updating Deneb](https://deneb3d.dev/docs/updating/) — day-2 `.deneb`
   package updates and when to rebuild bootstrap.
3. [Project status](PROJECT_STATUS.md) — the only human-maintained source for
   what is done, in progress, planned, blocked, or known broken.
4. [Platform modernization roadmap](PLATFORM_MODERNIZATION_ROADMAP.md) — the
   ordered plan for de-Pythonization, Web/API productization, a current OpenWrt
   base, an independent image, and modern Marlin work.
5. [Debian/Linux build environment](WSL_BUILD_ENVIRONMENT.md) — required
   workstation and cross-build setup.

[GETTING_STARTED.md](GETTING_STARTED.md) is the in-repo bootstrap host-package
and locked-Pillow contract that CI still greps. Keep Step 2 in
`website/content/docs/getting-started.md` aligned with that file;
`tools/ssh-bootstrap-patch-selftest.sh` checks both, and edits to
`docs/GETTING_STARTED.md` trigger the Website workflow so deneb3d.dev
rebuilds. Site-root `/docs/` and `/images/` links under
`website/content` are checked by `website/scripts/check-site-links.sh`
from `tools/check-publication-boundary.ps1` and after Hugo against
`website/public/`.

## Active technical documentation

These files describe current interfaces or supported workflows. They should
explain how the project works now, not preserve a running diary.

| Area | Document |
| --- | --- |
| Bootstrap host Python / Pillow | [GETTING_STARTED.md](GETTING_STARTED.md) |
| Third-party slicer profiles and command rules | [SLICER_COMPATIBILITY.md](SLICER_COMPATIBILITY.md) |
| Web/API architecture and first-class product requirements | [WEB_UI.md](WEB_UI.md) |
| Cura discovery and local printing internals | [CURA_INTEGRATION.md](CURA_INTEGRATION.md) |
| Touchscreen parity | [STOCK_UI_COVERAGE.md](STOCK_UI_COVERAGE.md) |
| Touchscreen host-capture procedure | [TOUCHSCREEN_SCREEN_CATALOG.md](TOUCHSCREEN_SCREEN_CATALOG.md) |
| Native UI/backend protocol | [BACKEND_IPC_PROTOCOL.md](BACKEND_IPC_PROTOCOL.md) |
| Fault handling policy | [FAULT_POLICY.md](FAULT_POLICY.md) |
| Source, license, and generated-data provenance | [SOURCE_PROVENANCE.md](SOURCE_PROVENANCE.md) |

Operator Wi-Fi, Ethernet, touchscreen, Web UI, Cura, and slicer guides are on
the public site, not duplicated here.

Component-specific build and implementation documentation also lives beside
the code, including `ui/README.md` and `printsvc/README.md`.

## Machine-audited acceptance specifications

The following documents remain at fixed paths because release/audit scripts
validate their content. They are detailed acceptance inventories, not the
human-facing project dashboard:

- [`../UM2C_MODDING_CHECKLIST.md`](../UM2C_MODDING_CHECKLIST.md)
- [PRINTSVC_EVIDENCE_LEDGER.md](PRINTSVC_EVIDENCE_LEDGER.md)
- [PRINTSVC_INTEGRATION_AUDIT.md](PRINTSVC_INTEGRATION_AUDIT.md)

When one of these disagrees with [PROJECT_STATUS.md](PROJECT_STATUS.md), treat
the disagreement as a documentation defect and reconcile it. Do not silently
promote an implementation or old test into current completion.

## Evidence and archive

- [`evidence/`](evidence/README.md) contains dated measurements, hardware runs,
  and technical investigations. Evidence proves only the named package,
  workflow, and date.
- [`archive/`](archive/README.md) contains completed or superseded plans and
  checklists. Archived documents must not be used as current work queues.

## Maintenance rules

1. Keep current work state only in `PROJECT_STATUS.md`.
2. Keep future sequencing and acceptance gates only in
   `PLATFORM_MODERNIZATION_ROADMAP.md`.
3. Keep component docs focused on current behavior, configuration, and known
   limitations. Remove trial-by-trial narratives once their conclusion is
   represented in status or evidence.
4. Put new dated hardware captures and investigations in `evidence/`, then link
   only the resulting status change from the dashboard.
5. Move completed or abandoned execution plans to `archive/`; do not keep
   appending completion logs to active plans.
6. Use the status vocabulary consistently: `SOURCE`, `HOST`, `TARGET`,
   `FAILED`, `BLOCKED`, and `PLANNED`.
7. A checkbox means only that its exact item is satisfied. It never means the
   containing feature or release is complete.
8. Operator how-to lives under `website/` / deneb3d.dev. Do not recopy those
   guides into this directory.
