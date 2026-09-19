---
title: Touchscreen
weight: 40
---

Deneb replaces the stock tap-only menus with a native LVGL UI at 320×240.
Unlike stock firmware, Deneb supports **vertical swipe scrolling** and
**horizontal drag sliders** on Temperature, material workflows, and Frame
Lighting. Dragging moves the visible content or control; it does not change
screens. Use the on-screen buttons and Back control to navigate.

These captures are layout references with placeholder data, not live printer
photos. Click a screen to view it at full size.

## Screen catalog

<div class="deneb-catalog">
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/home.png">
    <img src="/images/screens/home.png" width="320" height="240" alt="Home screen">
  </a>
  <div>
    <h3>Home</h3>
    <p>Scrollable root menu. Open Status, Print from USB, Material, Maintenance, Manual Control, Temperature, and Settings.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/status.png">
    <img src="/images/screens/status.png" width="320" height="240" alt="Status screen">
  </a>
  <div>
    <h3>Status</h3>
    <p>Live print overview: state, nozzle and bed readings, progress, remaining time, active file, position, Pause/Resume, and Stop.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/print-from-usb.png">
    <img src="/images/screens/print-from-usb.png" width="320" height="240" alt="Print from USB screen">
  </a>
  <div>
    <h3>Print from USB</h3>
    <p>Choose a G-code file from a USB stick, then start, open Material, pause, resume, or stop the job.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/print-conflict.png">
    <img src="/images/screens/print-conflict.png" width="320" height="240" alt="Print conflict screen">
  </a>
  <div>
    <h3>Print conflict</h3>
    <p>A pending job needs a choice before it can continue. Continue the job or cancel it.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/material.png">
    <img src="/images/screens/material.png" width="320" height="240" alt="Material screen">
  </a>
  <div>
    <h3>Material</h3>
    <p>Load, unload, choose a material, move filament, finish movement, import profiles, and set the workflow temperature.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/set-material.png">
    <img src="/images/screens/set-material.png" width="320" height="240" alt="Set Material screen">
  </a>
  <div>
    <h3>Set Material</h3>
    <p>Pick the installed material profile from the catalog.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/maintenance.png">
    <img src="/images/screens/maintenance.png" width="320" height="240" alt="Maintenance screen">
  </a>
  <div>
    <h3>Maintenance</h3>
    <p>Temperature, Update Firmware, Move Build Plate, Level Build Plate, and Diagnostics.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/temperature.png">
    <img src="/images/screens/temperature.png" width="320" height="240" alt="Temperature screen">
  </a>
  <div>
    <h3>Temperature</h3>
    <p>Drag the nozzle and bed targets, apply them, or cool down both heaters.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/update-firmware.png">
    <img src="/images/screens/update-firmware.png" width="320" height="240" alt="Update Firmware screen">
  </a>
  <div>
    <h3>Update Firmware</h3>
    <p>Lists USB <code>.deneb</code> packages. Select a package, then tap again to confirm. This screen does not install official UltiMaker <code>.img</code> files.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/manual-control.png">
    <img src="/images/screens/manual-control.png" width="320" height="240" alt="Manual Control screen">
  </a>
  <div>
    <h3>Manual Control</h3>
    <p>Jog X/Y, home XY, choose step size, home Z, and move the build plate up or down.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/level-build-plate.png">
    <img src="/images/screens/level-build-plate.png" width="320" height="240" alt="Level Build Plate screen">
  </a>
  <div>
    <h3>Level Build Plate</h3>
    <p>Guided leveling. Advance each step with the single action button.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/diagnostics.png">
    <img src="/images/screens/diagnostics.png" width="320" height="240" alt="Diagnostics screen">
  </a>
  <div>
    <h3>Diagnostics</h3>
    <p>Hardware summary plus log export to USB, including Air Manager, build-volume, and fan information when present.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/settings.png">
    <img src="/images/screens/settings.png" width="320" height="240" alt="Settings screen">
  </a>
  <div>
    <h3>Settings</h3>
    <p>Language, Nozzle Size, Network, Digital Factory, Frame Lighting, Factory Reset, and About Deneb.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/language.png">
    <img src="/images/screens/language.png" width="320" height="240" alt="Language screen">
  </a>
  <div>
    <h3>Language</h3>
    <p>English, Dutch, German, French, Simplified Chinese, Pirate English, or L33T English.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/nozzle-size.png">
    <img src="/images/screens/nozzle-size.png" width="320" height="240" alt="Nozzle Size screen">
  </a>
  <div>
    <h3>Nozzle Size</h3>
    <p>Select the installed nozzle diameter.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/network.png">
    <img src="/images/screens/network.png" width="320" height="240" alt="Network screen">
  </a>
  <div>
    <h3>Network</h3>
    <p>Hostname, Wi-Fi, and Ethernet status. Toggle Wi-Fi, import settings from a USB stick, or reset Ethernet to DHCP. There is no stock captive portal.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/digital-factory.png">
    <img src="/images/screens/digital-factory.png" width="320" height="240" alt="Digital Factory screen">
  </a>
  <div>
    <h3>Digital Factory</h3>
    <p>Connect to start pairing and show a PIN. Disconnect is available only while a cloud session can be ended.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/frame-lighting.png">
    <img src="/images/screens/frame-lighting.png" width="320" height="240" alt="Frame Lighting screen">
  </a>
  <div>
    <h3>Frame Lighting</h3>
    <p>Turn the frame light on or off and drag brightness.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/factory-reset.png">
    <img src="/images/screens/factory-reset.png" width="320" height="240" alt="Factory Reset screen">
  </a>
  <div>
    <h3>Factory Reset</h3>
    <p>Tap Reset, then tap again to clear local settings and reboot.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/about.png">
    <img src="/images/screens/about.png" width="320" height="240" alt="About Deneb screen">
  </a>
  <div>
    <h3>About Deneb</h3>
    <p>Deneb version, stock base, repository, printer ID, and certifications.</p>
  </div>
</article>
<article class="deneb-screen">
  <a class="deneb-zoom" href="/images/screens/error.png">
    <img src="/images/screens/error.png" width="320" height="240" alt="Error screen">
  </a>
  <div>
    <h3>Error</h3>
    <p>Blocking recovery prompt with an ER code, description, recommended action, and OK to dismiss.</p>
  </div>
</article>
</div>

## Compared with stock

Stock menus were tap-only. Deneb adds swipe scrolling and drag sliders, puts
Status, Manual Control, and Temperature on the home menu, and uses USB files
for network setup instead of a captive portal. Firmware updates on this UI are
Deneb `.deneb` packages only.

## Network from the panel

Wi-Fi and Ethernet are configured from USB files:

- [Wi-Fi setup](/docs/wifi-setup/)
- [Ethernet setup](/docs/ethernet-setup/)
