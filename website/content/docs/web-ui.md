---
title: Browser Web UI
weight: 50
---

Deneb adds a local-network browser UI that stock UltiMaker 2+ Connect firmware
did not ship. After a full `.deneb` install it is available at
`http://<printer-ip>/`.

Keep the printer on a **trusted LAN**. Bootstrap SSH still uses the known
password `deneb` until you change it. The Cura cluster upload/control path is
intentionally unauthenticated so stock Cura can print; do not expose the
printer to the public internet.

## Open it

1. Finish [Getting started](getting-started.md) or [Updating](updating.md).
2. Put the printer on Ethernet or Wi-Fi ([Wi-Fi](wifi-setup.md) /
   [Ethernet](ethernet-setup.md)).
3. Browse to `http://<printer-ip>/`.
4. On first visit, set a Web UI password or choose Open Access.

The UI is a small vanilla HTML/CSS/JS front end served by lighttpd. It talks
to `deneb-api` for status, uploads, and print control.

## What it can do today

- Show printer and job status
- Upload and start local jobs
- Pause, resume, and cancel through the Web UI
- Work alongside Cura local discovery (see [Cura](cura.md))

It is still an MVP. Connection cleanup, storage UX, security hardening, and
failure recovery remain open on the
[project status board](../../../docs/PROJECT_STATUS.md).

## Related

- [Cura integration](cura.md)
- [Slicer compatibility](slicer-compatibility.md)
- [Getting started](getting-started.md)
