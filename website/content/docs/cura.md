---
title: Cura plugin setup
weight: 60
---

Use the Deneb plugin to find your UltiMaker 2+ Connect in Cura and connect to
it over your home or workshop network.

This guide covers installing the plugin on **your computer**. For package
building and how the connection works, see the
[Cura technical guide](/docs/cura-technical/).

## Before you start

You need:

- **UltiMaker Cura** on your computer. Deneb's documented tests used
  **Cura 5.13**; other versions have not been fully validated.
- A printer running the **full Deneb software**. The get-started `.img`
  alone is not enough. Finish [Technical installation](/docs/getting-started/)
  first if you have only installed that image.
- Your computer and printer connected to the **same trusted local network**.
  The printer can use [Wi-Fi](/docs/wifi-setup/) or
  [Ethernet](/docs/ethernet-setup/).
- The plugin file: **DenebUM2CNetworkPrinting.curapackage**.

Deneb is experimental. Keep printer access on a trusted network.

## 1. Get the plugin file

There is no ready-made Deneb plugin download in GitHub Releases yet.
You or someone helping you must create the file using
[Build the Cura plugin](/docs/cura-technical/#cura-plugin-build).

Once you have **DenebUM2CNetworkPrinting.curapackage**, save it somewhere easy
to find on the computer running Cura, such as your Downloads folder.

Keep the file as it is. Do not unzip it, rename it, or put it on the printer's
USB drive. Cura installs this file on your computer.

## 2. Install the plugin

1. Open Cura and wait for its main window.
   If this is your first time using Cura, finish the welcome setup first.
   You can add a non-networked **UltiMaker 2+ Connect** for now.
2. Open the folder containing **DenebUM2CNetworkPrinting.curapackage**.
3. Drag the file into Cura's main window, over the area showing the build plate.
4. Cura should show a message that the package will be installed after
   restarting.
5. Close Cura completely, then open it again.

The plugin needs that restart before it can help Cura find your printer.
You do not need to copy files into Cura's installation folders.

## 3. Add your printer

1. Turn on your Deneb printer and make sure it has joined your network.
2. In Cura, open **Settings > Printer > Add Printer**.
3. If asked, choose **UltiMaker printer**, then **Add local printer**.
   Use the list of printers found on your network.
4. Look for your printer's name followed by **(Deneb UM2C)**.
   Click **Refresh** if needed.
5. Select it and finish Cura's add-printer prompts.

The plugin lets Cura use its normal **UltiMaker 2+ Connect** profile for the
Deneb printer. You do not need to create a custom printer profile.

## 4. Check the connection

Select the new network printer in Cura. Confirm that its printer type is
**UltiMaker 2+ Connect**, then open **Monitor** to check that Cura can see it.

If you kept an older, non-networked printer entry, make sure the new network
entry is selected when you want to send a job to Deneb.

You do not need to start a print just to check that the plugin is installed
and the printer is connected.

## If something does not work

- **Cura does not accept the file:** check that its name ends in
  `.curapackage`, not `.zip`, and that you dropped it onto the main window.
  If Cura reports an incompatible package, check your Cura version against
  the tested version above.
- **The printer is missing:** restart Cura after installing the plugin,
  check that both devices are on the same network, then try **Refresh**.
  Guest networks or a VPN can prevent devices from finding each other.
- **Discovery still fails:** use **Add printer by IP** in the network-printer
  list and enter the printer's local IP address from its network settings.
- **Cura shows the printer as offline:** check the printer's network connection
  and confirm you selected the network entry rather than an older offline one.
- **Only the get-started image is installed:** install the full Deneb package
  before trying to connect from Cura.

For build commands, discovery details, and known limitations, use the
[Cura technical guide](/docs/cura-technical/).
