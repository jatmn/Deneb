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

This file also enables **SSH**, a way to control the printer from a computer,
with the known password `deneb`. Keep the printer on a trusted home or workshop
network; do not expose it to the internet or a public network. It will not ask
you to change that password automatically.

## 1. Download the two files

Open the [Deneb get-started download page](https://github.com/jatmn/Deneb/releases/tag/get-started).
Under **Assets** (expand it if needed), download:

- **Deneb_get_started.img** — the file the printer will install.
- **Deneb_get_started.img.sha256** — a small text file used to check the download.

Save both in the same folder. Keep their names exactly as shown. Skip the
**Source code** downloads and the **nightly** pre-release.

You do **not** need an image-writing app. Do not unpack the `.img` file.
You will copy it just like a photo or document.

## 2. Check the download

This check is like matching two fingerprints: it helps catch a damaged or
incomplete download before you put it on the printer.

Open `Deneb_get_started.img.sha256` in a text editor. The long string of
letters and numbers at the start is the fingerprint to match; the filename
after it is not part of the fingerprint.

Choose your computer below. This is the only computer command you need for
this guide.

{{< details title="Windows" >}}
Open **PowerShell** from the Start menu. Type the following, with a space at
the end:

```powershell
Get-FileHash -Algorithm SHA256
```

Drag `Deneb_get_started.img` from File Explorer into that window to add its
path, then press **Enter**. If the path contains spaces, put double quotes
around it. Look at the value under **Hash**.
{{< /details >}}

{{< details title="macOS" >}}
Open **Terminal**. Type the following, then a space:

```sh
shasum -a 256
```

Drag `Deneb_get_started.img` from Finder into the window, then press
**Return**. The long string at the start is the fingerprint.
{{< /details >}}

{{< details title="Linux" >}}
Open a terminal. Type the following, then a space:

```sh
sha256sum
```

Drag `Deneb_get_started.img` from your file manager into the window to add
its path. If dragging is not supported, type the full file path in quotes.
Press **Enter**. The long string at the start is the fingerprint.
{{< /details >}}

**All the letters and numbers must match** the text file. Uppercase and
lowercase letters count as the same. If they do not match, stop: download both
files again from the same release and repeat the check.

## 3. Put the image on the USB drive

1. Plug the FAT32 USB drive into your computer.
2. Open the drive and copy **Deneb_get_started.img** directly onto it.
   Do not put it inside a folder. This top level is sometimes called the
   drive's “root.”
3. Keep only this one firmware file on the drive. Move other `.img` or
   `.deneb` files off it so the printer cannot pick the wrong one.
4. Repeat the fingerprint check from Step 2, this time dragging the
   **file on the USB drive** into the command window. It must match the same
   `.sha256` text file. If it does not, copy the checked download again and
   recheck it before continuing.
5. Use your computer's **Eject** or **Safely remove** option, then unplug
   the drive.

The `.sha256` file can stay on your computer. The printer only needs the
`.img` file.

## 4. Install it on the printer

1. Make sure no print is running, then plug the USB drive into the printer.
2. Open **Maintenance > Update Firmware** on the touchscreen, then choose
   the USB install option. The exact words may differ with your stock
   firmware version.
3. Select **Deneb_get_started.img** and follow the update prompts.
4. Wait for installation to finish and the printer to restart. Leave the
   power on and the USB drive in place while it is updating.

## 5. Check what changed

You should see the **Deneb splash screen** as the printer starts, followed by
the familiar stock menu. That is expected: you have installed the preparation
file, not the full Deneb software.

For a detailed check using SSH, see
[Verify bootstrap success](/docs/getting-started/#verify-bootstrap-success).
You do not need to use SSH to copy the next update onto USB.

## 6. Continue to the full Deneb install

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
- **The fingerprint does not match:** do not install it. Download or copy
  it again, then check again.
- **You still see the old menu:** that is normal after this first file.
  The full `.deneb` package comes next.
- **You need to go back:** while the stock menu is still running, its USB
  updater can install official UltiMaker firmware. After the full Deneb
  install, recovery works differently. Read
  [Returning toward official firmware](/docs/getting-started/#returning-toward-official-firmware)
  before proceeding; a factory reset is not a firmware rollback.

For installation errors or an update that appears stuck, use the
[technical troubleshooting guide](/docs/getting-started/#troubleshooting).
