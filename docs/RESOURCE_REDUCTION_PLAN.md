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

