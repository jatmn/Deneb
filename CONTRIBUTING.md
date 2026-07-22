# Contributing to Deneb

Deneb controls physical motion, heaters, firmware updates, and network access.
Contributions must distinguish source implementation, host validation, and
target proof using the vocabulary in
[`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md).

Before submitting a change:

1. Keep proprietary firmware, extracted filesystems, credentials, logs with
   device identifiers, and generated release images out of Git.
2. Add provenance and license data for every new dependency or derived dataset.
3. Run the relevant host tests and static checks. Do not mark a hardware
   workflow complete without dated evidence from the exact tested package.
4. For physical testing, home before axis motion, treat every endstop/limit
   alert as a stop condition, stay within the documented motion envelope, and
   obey the current heater caps.
5. Update the single current dashboard only when evidence changes status; put
   dated captures in `docs/evidence/` and superseded plans in `docs/archive/`.

The WSL release environment is documented in
[`docs/WSL_BUILD_ENVIRONMENT.md`](docs/WSL_BUILD_ENVIRONMENT.md). A generated
package is not valid unless the release wrapper completes all package audits.
