# Contributing to Deneb

Deneb is a maintainer-directed firmware project controlling physical motion,
heaters, updates, and network access. Contributions are welcome when they
advance the project's documented goals, preserve its technical direction, and
include evidence appropriate to the risk.

Submitting a pull request does not guarantee review or acceptance. Maintainers
may close contributions that fall outside the project's intended scope,
roadmap, architecture, or current priorities.

## Start With Scope

For anything beyond a small, obvious fix, open an issue and obtain agreement on
the problem and proposed direction before writing code. Coding agents must also
follow [`AGENTS.md`](AGENTS.md). Review
[`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md) for current work and
[`docs/PLATFORM_MODERNIZATION_ROADMAP.md`](docs/PLATFORM_MODERNIZATION_ROADMAP.md)
for accepted future direction.

Keep each pull request limited to one clearly defined problem. Do not bundle
cleanup, formatting, dependency changes, refactoring, or unrelated fixes into
the requested work.

## Technical Direction

- Do not introduce a new implementation language, runtime, build system, or
  framework.
- New Python source, dependencies, tools, generated Python, or target-side
  Python runtime use are explicitly forbidden.
- Existing Python files do not make Python an accepted Deneb implementation
  language. Changes to them must be limited to removing Python or narrowly
  maintaining an existing host-tool or Cura compatibility boundary; do not
  expand the Python footprint.
- Dependency changes require prior agreement and a concrete benefit such as a
  verified defect fix, security correction, compatibility requirement, or
  measurable resource reduction. Personal preference is not sufficient.
- Preserve the lightweight C, shell, and existing Web implementation unless a
  different direction has been explicitly approved.

## What May Be Closed Without Review

Maintainers may close a pull request without notice, review, or further
explanation when it:

- performs mass refactoring, broad rewriting, mechanical cleanup, or formatting
  churn without a specific and demonstrated functional benefit;
- changes code merely to express a different style, abstraction preference, or
  tool-generated opinion;
- introduces a new language, expands Python, replaces core dependencies, or
  shifts the architecture without prior approval;
- falls outside Deneb's documented goals or current project scope;
- duplicates existing work or disregards an existing implementation decision;
- bundles unrelated changes or drifts beyond an approved issue;
- provides no clear value, evidence, validation, or maintainable reason for the
  change; or
- weakens safety controls, publication boundaries, de-Pythonization gates, or
  resource constraints.

Not every technically valid pull request belongs in Deneb. Rejection or closure
may reflect project direction and maintainer capacity rather than whether the
code can compile.

## Before Submitting

1. Keep proprietary firmware, extracted filesystems, credentials, logs with
   device identifiers, and generated release images out of Git.
2. Add provenance and license data for every approved dependency or derived
   dataset.
3. Explain what changed, why it belongs in Deneb, the user or engineering
   benefit, and the exact validation performed.
4. Run the relevant host tests and static checks. Do not mark a hardware
   workflow complete without dated evidence from the exact tested package.
5. For physical testing, home before axis motion, treat every endstop or limit
   alert as an absolute stop condition, remain inside the documented motion
   envelope, and obey the current heater caps.
6. Distinguish source implementation, host validation, and target proof using
   the vocabulary in
   [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md).
7. Update the current dashboard only when evidence changes status. Put dated
   captures in `docs/evidence/` and superseded plans in
   `docs/archive/`.

The Debian/Linux release environment (including the optional WSL workflow) is documented in
[`docs/WSL_BUILD_ENVIRONMENT.md`](docs/WSL_BUILD_ENVIRONMENT.md). A generated
package is not valid unless the release wrapper completes all package audits.
