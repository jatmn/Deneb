---
title: Cura plugin setup
weight: 60
---

Use the Deneb plugin so UltiMaker Cura can find your printer on your home or
workshop network and send prints to it.

You download a ready-made file and drop it into Cura. You do not build
anything, and you do not copy this file onto the printer.

Want the build commands, or an explanation of how the connection works? Use
the [Cura technical guide](/docs/cura-technical/).

## Before you start

You need:

- **UltiMaker Cura** on your computer. Deneb's documented tests used
  **Cura 5.13**. Other versions may work, but they have not been fully tested.
- A printer that already has the **full Deneb software**. The get-started
  image by itself is not enough. Finish
  [Technical installation](/docs/getting-started/) first if that is all you
  have installed.
- Your computer and the printer on the **same trusted network**. The printer
  can use [Wi-Fi](/docs/wifi-setup/) or [Ethernet](/docs/ethernet-setup/).

Deneb is experimental. Keep the printer on a network you trust. Do not expose
it to the public internet.

## 1. Get the plugin file

Open the [Deneb Cura plugin download page](https://github.com/jatmn/Deneb/releases/tag/cura-plugin).

Under **Assets** (expand it if needed), download **DenebUM2CNetworkPrinting.curapackage**.

Keep the name exactly as shown. Do not unzip it or rename it. Save it
somewhere easy to find, such as your Downloads folder.

This file is installed on the computer that runs Cura. Do not put it on the
printer's USB drive. Skip the **Source code** downloads. This is not the
get-started printer image.

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
