---
title: Touchscreen
weight: 40
---

Deneb replaces the stock tap-only menus with a native LVGL UI at 320x240.
Unlike stock firmware, Deneb supports **vertical swipe scrolling** and
**horizontal drag sliders** (Temperature, material workflows, Frame Lighting).
Use the on-screen buttons and Back control to change screens.

These captures are host-rendered layout references with stub data, not live
printer photos. They were regenerated from commit `ab2741a` on 2026-07-22.

Home is the root menu:

![Home](/images/screens/home.png)

## Screen catalog

<div class="deneb-screen-grid">
<figure>
<img src="/images/screens/home.png" alt="Home">
<figcaption>Home</figcaption>
</figure>
<figure>
<img src="/images/screens/status.png" alt="Status">
<figcaption>Status</figcaption>
</figure>
<figure>
<img src="/images/screens/print-from-usb.png" alt="Print from USB">
<figcaption>Print from USB</figcaption>
</figure>
<figure>
<img src="/images/screens/material.png" alt="Material">
<figcaption>Material</figcaption>
</figure>
<figure>
<img src="/images/screens/maintenance.png" alt="Maintenance">
<figcaption>Maintenance</figcaption>
</figure>
<figure>
<img src="/images/screens/temperature.png" alt="Temperature">
<figcaption>Temperature</figcaption>
</figure>
<figure>
<img src="/images/screens/manual-control.png" alt="Manual Control">
<figcaption>Manual Control</figcaption>
</figure>
<figure>
<img src="/images/screens/network.png" alt="Network">
<figcaption>Network</figcaption>
</figure>
<figure>
<img src="/images/screens/digital-factory.png" alt="Digital Factory">
<figcaption>Digital Factory</figcaption>
</figure>
<figure>
<img src="/images/screens/settings.png" alt="Settings">
<figcaption>Settings</figcaption>
</figure>
<figure>
<img src="/images/screens/update-firmware.png" alt="Update Firmware">
<figcaption>Update Firmware</figcaption>
</figure>
<figure>
<img src="/images/screens/about.png" alt="About Deneb">
<figcaption>About Deneb</figcaption>
</figure>
</div>

Pause safety, some material and leveling cancel paths, update UX, and
diagnostics still have open work. See the
[repository screen catalog](../../../docs/TOUCHSCREEN_SCREEN_CATALOG.md)
for the full per-screen capture notes.

## Network from the panel

Wi-Fi and Ethernet are configured from USB files, not a captive portal:

- [Wi-Fi setup](wifi-setup.md)
- [Ethernet setup](ethernet-setup.md)
