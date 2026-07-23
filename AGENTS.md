# AGENTS.md

These instructions apply to the entire repository. A more deeply nested
`AGENTS.md`, if one is added later, may provide stricter component-specific
rules but must not weaken this file.

## Mission

Deneb replaces the stock printer's target-side Python services with compact C
components, improves the touchscreen and local-network experience, and works
toward a reproducible Deneb-owned firmware image. The target has very limited
CPU, memory, and storage. Treat unnecessary dependencies, processes, memory,
copies, abstractions, and generated assets as defects.

This project controls a physical 3D printer. Correctness includes preventing
unsafe motion, heating, flashing, update, and recovery behavior.

## Read Before Changing Anything

- `CONTRIBUTING.md` defines contribution scope and rejection policy.
- `docs/PROJECT_STATUS.md` is the only human-maintained current-status
  dashboard.
- `docs/PLATFORM_MODERNIZATION_ROADMAP.md` owns future sequencing and
  acceptance gates.
- `docs/README.md` defines documentation placement and status vocabulary.
- `UM2C_MODDING_CHECKLIST.md` and the print-service ledgers are
  machine-audited acceptance inventories, not general status summaries.
- `docs/WSL_BUILD_ENVIRONMENT.md` defines the supported native Debian/Linux
  and Windows/WSL release build lanes. Do not mix their dependency paths or
  wrappers.
- `COMPLIANCE.md`, `SECURITY.md`, and
  `docs/SOURCE_PROVENANCE.md` define publication, credential, and source
  boundaries.

Do not infer completion from a checkbox, implementation, old evidence file, or
successful host test. Preserve the distinctions between `SOURCE`, `HOST`,
`TARGET`, `FAILED`, `BLOCKED`, and `PLANNED`.

## Technical Direction

- Do not introduce a new implementation language, runtime, framework, build
  system, or service.
- New Python source, dependencies, tools, generated Python, and target-side
  Python use are forbidden.
- Existing Python files are bounded legacy/host integration exceptions. Only
  remove them or make narrowly required maintenance to the existing Cura or
  host-tool boundary. Never expand the Python footprint.
- Prefer existing C components and shared helpers. Preserve shell for existing
  build, installer, audit, and device-integration scripts and plain JavaScript
  for the existing static Web UI.
- Keep lighttpd as the HTTP front end. HTTP concerns may move into lighttpd, but
  printer behavior belongs in Deneb services.
- Avoid dependency additions. Any approved dependency must have a concrete,
  measured benefit and updated provenance, licensing, notices, and package
  inventory.
- Marlin changes must remain surgical, GPL-compatible, separated from the Deneb
  application tree, and backed by protocol fixtures and a recovery plan.
- Do not perform broad refactors, mechanical rewrites, formatting churn, or
  unrelated cleanup. Make the smallest change that solves the evidenced
  problem.

## Repository Map

- `common/`: shared native contracts and helpers.
- `ui/`: native C/LVGL touchscreen and update-package builder.
- `web/`: native C Web/API backend, lighttpd configuration, and static UI.
- `printsvc/`: native C print service, G-code policies, and host tests.
- `dfsvc/`: native C Digital Factory connector.
- `cura/`: existing Cura compatibility plugin; Python exception, not a
  general implementation pattern.
- `tools/`: build, audit, fixture, and device scripts.
- `docs/evidence/`: dated proof for a named package and workflow.
- `docs/archive/`: superseded or completed plans, never current truth.
- `build/`, `dist/`, and generated packages: build output; do not treat as
  source or commit unless an existing tracked artifact explicitly requires it.

## Working Rules

1. Inspect the current worktree before editing. Preserve user changes and avoid
   unrelated files.
2. Establish the exact source, host, package, or target evidence for the claim
   being changed.
3. Keep one implementation owner for each behavior. Do not duplicate status
   classification, printer policy, G-code semantics, or lifecycle state across
   UI, Web, and services.
4. Add or update focused tests and negative fixtures with behavior changes.
5. Update active documentation only when current behavior or evidence changes.
   Put dated test detail in `docs/evidence/` and obsolete plans in
   `docs/archive/`.
6. Do not create changelog-style progress diaries or workstation-specific
   commentary in active documentation.
7. Do not commit, push, publish releases, change repository visibility, deploy,
   reboot, move, heat, or flash hardware unless the user explicitly authorizes
   that action. Treat one-time Git authorization as consumed after use.

## Physical Printer Safety

When hardware work is explicitly authorized:

- Never move any axis before homing the required axes.
- Treat every limit-switch, endstop, controller, or thermal alert as an absolute
  stop condition, never informational noise.
- Keep commanded X, Y, and Z positions within 0 through 190 mm for automated
  testing.
- Never command the bed above 50 C or the extruder above 195 C.
- Fail closed if position, homing state, temperature, controller state, or
  command ownership is uncertain.
- Use bounded timeouts and record the exact package, commands, limits, and
  results.
- Do not flash the mainboard/controller without explicit approval, a known-good
  recovery path, and a restorable image.
- A reboot does not erase an alert or convert an unsafe result into a pass.

These limits are maximum permissions, not default test targets. Prefer
non-motion and non-heating validation whenever it can answer the question.

## Validation

Run the narrowest relevant checks first, then the broader gates affected by the
change. The CI source of truth is `.github/workflows/ci.yml`.

Baseline repository checks:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-publication-boundary.ps1
pipx run --spec "reuse[charset-normalizer]==6.2.0" reuse lint
git diff --check
```

Shell and fixture checks run in the documented Debian/Linux environment:

```sh
find . -path ./.git -prune -o -path ./ui/lib/lvgl -prune -o \
  -type f -name '*.sh' -print0 | xargs -0 -r sh -n
shellcheck tools/setup-linux-build.sh tools/setup-wsl-build.sh tools/build-update-release.sh
bash tools/deneb-compile-all-selftest.sh
bash tools/deneb-stock-menu-prune-selftest.sh
bash tools/deneb-printsvc-smoke-selftest.sh
bash tools/deneb-printsvc-stability.sh --selftest
bash tools/deneb-printsvc-init-selftest.sh
bash tools/deneb-printsvc-release-gate-selftest.sh
bash tools/deneb-printsvc-native-audit-selftest.sh
bash tools/deneb-printsvc-integration-audit-selftest.sh
```

Native host builds:

```sh
cmake -S printsvc -B build/ci-printsvc -DBUILD_HOST_STUB=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/ci-printsvc --parallel
ctest --test-dir build/ci-printsvc --output-on-failure
cmake -S web -B build/ci-web -DBUILD_HOST_STUB=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/ci-web --parallel
cmake -S ui -B build/ci-ui -DBACKEND_COMM_STUB=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/ci-ui --parallel
cmake -S dfsvc -B build/ci-dfsvc -DCMAKE_BUILD_TYPE=Debug
cmake --build build/ci-dfsvc --parallel
```

A package build is not valid unless the release wrapper and package audits
complete. Host validation never authorizes or proves physical printer behavior.
If a required check cannot run, report exactly what was skipped and do not
promote the result.
