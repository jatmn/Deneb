# Resource Reduction Plan

Deneb assumes the stock firmware is already too constrained by RAM, CPU, boot time, and UI latency. Matching stock behavior is not the final goal; measurable reduction is.

## First Measurements After SSH Bootstrap

- Process list and resident memory.
- Idle CPU and wakeups.
- CPU while printing.
- Free RAM and memory pressure while idle, printing, uploading, updating, and exporting logs.
- Boot-to-ready timing.
- Service startup order.
- Open sockets and listeners.
- Flash and local storage usage.
- Log volume and rotation behavior.

## Release Gates

- New long-running services need measured RAM and CPU budgets.
- Hot paths should prefer event-driven behavior over polling.
- Large files, logs, thumbnails, and uploads should be streamed or parsed in bounded memory.
- Stable releases should block resource regressions unless explicitly scoped as temporary transition builds.

## Current Guardrails

- Stock menu baseline: 33.7 MB VSZ / about 21 MB RSS.
- Current Deneb UI idle snapshot: 2.7 MB VSZ / about 2 MB RSS for `deneb-ui --lang en`.
- Current settled idle system sample: about 90% idle after Deneb replaces the stock menu.
- Current `.deneb` package size: about 8.6 MiB with LVGL, static ZMQ, and generated i18n fonts.

Before treating a build as release-ready, repeat memory and CPU sampling while
idle, printing, installing a Deneb package, exporting diagnostics, switching
languages, and using Digital Factory pairing/disconnect.
