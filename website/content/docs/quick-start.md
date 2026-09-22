---
title: Getting started (easy guide)
weight: 10
---

Start here if your **UltiMaker 2+ Connect still has its original software**.
You will download a ready-made file, put it on a USB drive, and let the printer
install it. You do not need to build the file yourself.

Want to build your own files or follow the detailed commands? Use
[Technical installation](/docs/getting-started/).
Already running the full Deneb software? Use [Updating Deneb](/docs/updating/).

{{< callout type="info" >}}
**This is the first part of installing Deneb.** The file here prepares your
printer to accept Deneb updates. It does **not** install the new touchscreen,
Web UI, or printing software. Those need a second file ending in `.deneb`.
GitHub currently provides only the first file.
{{< /callout >}}

## Before you start

You need:

- An **UltiMaker 2+ Connect** with a working firmware-update menu.
- A computer with internet access.
- A USB drive formatted as **FAT32**. If you need to format it, save your files
  somewhere else first: formatting erases the drive.

Deneb is experimental community software, not an official UltiMaker update.
Only start when the printer is idle and no print is running. Keep the power
on during installation.

Keep the printer on a trusted home or workshop network. Do not expose it to
the internet or a public network.

## 1. Download the image

Open the [Deneb get-started download page](https://github.com/jatmn/Deneb/releases/tag/get-started).
Under **Assets** (expand it if needed), download **Deneb_get_started.img**.

Keep its name exactly as shown. Skip the **Source code** downloads and the
**nightly** pre-release.

You do **not** need an image-writing app. Do not unpack the file.
You will copy it just like a photo or document.

## 2. Put it on the USB drive

1. Plug the FAT32 USB drive into your computer.
2. Open the drive and copy **Deneb_get_started.img** directly onto it.
   Do not put it inside a folder.
3. Keep only this one firmware file on the drive. Move other firmware files
   off it so the printer cannot pick the wrong one.
4. Wait for the copy to finish. Use your computer's **Eject** or
   **Safely remove** option, then unplug the drive.

## 3. Install it on the printer

1. Make sure no print is running, then plug the USB drive into the printer.
2. Open **Maintenance > Update Firmware** on the touchscreen, then choose
   the USB install option. The exact words may differ with your stock
   firmware version.
   If it only offers an internet update, disconnect the printer from Ethernet
   and Wi-Fi, then leave and reopen the update screen to show the USB option.
3. Select **Deneb_get_started.img** and follow the update prompts.
4. Wait for installation to finish and the printer to restart. Leave the
   power on and the USB drive in place while it is updating.

## 4. Check what changed

You should see the **Deneb splash screen** as the printer starts, followed by
the familiar stock menu. That is expected: you have installed the preparation
file, not the full Deneb software.

## 5. What comes next

The new touchscreen, Web UI, and printing software come in a separate
`Deneb_Update_<version>.deneb` file.

**There is no prebuilt full-stack `.deneb` download from GitHub Actions yet.**
To finish, you or someone helping you must build that file on a supported
Debian/Linux or Windows/WSL computer. The technical guide explains
[how to build it](/docs/getting-started/#step-4-build-the-full-deneb-update-package)
and [how to install it](/docs/getting-started/#step-5-install-the-first-full-deneb-package).

If you are not ready for that, stop here. The new Deneb features will not be
available until you install the second file.

## If something does not look right

- **The printer cannot see the file:** check that the drive is FAT32, the
  name is exactly `Deneb_get_started.img`, and the file is not inside a folder.
- **You still see the old menu:** that is normal after this first file.
  The full `.deneb` package comes next.
- **You need to go back:** while the stock menu is still running, its USB
  updater can install official UltiMaker firmware. After the full Deneb
  install, recovery works differently. Read
  [Returning toward official firmware](/docs/getting-started/#returning-toward-official-firmware)
  before proceeding; a factory reset is not a firmware rollback.

For installation errors or an update that appears stuck, use the
[technical troubleshooting guide](/docs/getting-started/#troubleshooting).
