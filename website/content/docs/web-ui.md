---
title: Browser Web UI
weight: 50
---

Deneb adds a local-network browser UI that stock UltiMaker 2+ Connect firmware
did not ship. After a full `.deneb` install it is available at
`http://PRINTER_IP/`.

Keep the printer on a **trusted LAN**. Bootstrap SSH still uses the known
password `deneb` until you change it. The Cura cluster upload/control path is
intentionally unauthenticated so stock Cura can print; do not expose the
printer to the public internet.

## Open it

1. Finish [Getting started](/docs/getting-started/) or [Updating](/docs/updating/).
2. Put the printer on Ethernet or Wi-Fi ([Wi-Fi](/docs/wifi-setup/) /
   [Ethernet](/docs/ethernet-setup/)).
3. Browse to `http://PRINTER_IP/`.
4. On first visit, set a Web UI password or choose Open Access.

The UI is a small vanilla HTML/CSS/JS front end served by lighttpd. It talks
to `deneb-api` for status, uploads, and print control.

## What it can do today

- Show printer and job status
- Upload and start local jobs
- Pause, resume, and cancel through the Web UI
- Work alongside Cura local discovery (see [Cura](/docs/cura/))

It is still an MVP. Connection cleanup, storage UX, security hardening, and
failure recovery remain open on the
[project status board](https://github.com/jatmn/Deneb/blob/main/docs/PROJECT_STATUS.md).

## Related

- [Cura integration](/docs/cura/)
- [Slicer compatibility](/docs/slicer-compatibility/)
- [Getting started](/docs/getting-started/)
