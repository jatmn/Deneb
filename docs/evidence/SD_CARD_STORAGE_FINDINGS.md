# SD Card Storage Findings

Date: 2026-06-27
Device: live UltiMaker 2+ Connect test printer at `10.10.10.241`
Context: investigated after the touchscreen remained on the Deneb splash screen. The splash issue itself was caused by `deneb-ui` not being started at boot; this document records the separate SD-card findings discovered during follow-up health checks.

## Executive Summary

The printer contains a physical SD card reported by the kernel as `SD16G` with a usable size of 14.4 GiB. Stock firmware does not use the whole card. Its `/etc/init.d/sdcard` script creates exactly two 4 GiB ext4 partitions and leaves the remaining capacity unpartitioned.

The operating system does not boot from this SD card. The OS root lives on internal flash using squashfs plus a JFFS2 overlay. The SD card is used for print-file storage and persistent logs.

Both mounted SD partitions currently have ext4 filesystem errors. The filesystems are not full; the issue is metadata inconsistency. Stock fstab has `check_fs '0'`, so boot does not automatically repair them.

## Live Block Layout

Observed commands:

```sh
cat /proc/partitions
fdisk -l /dev/mmcblk0
df -h
mount
block info
```

| Device | Size | Label | Mounted At | Used | Purpose |
| --- | ---: | --- | --- | ---: | --- |
| `/dev/mmcblk0` | 14.4 GiB | `SD16G` card | n/a | n/a | Physical SD card |
| `/dev/mmcblk0p1` | 4.0 GiB | `3D` | `/home/3D` | 20.3 MiB | Print-file area and Deneb print metadata |
| `/dev/mmcblk0p2` | 4.0 GiB | `log` | `/tmp/log/ultimaker` via `/var -> /tmp` | 144.5 MiB | Persistent Ultimaker and Deneb logs |
| Unpartitioned | about 6.4 GiB | n/a | n/a | n/a | Unused by stock partitioning |

Kernel/sysfs card identity:

| Field | Value |
| --- | --- |
| Name | `SD16G` |
| Type | `SD` |
| Size sectors | `30244864` |
| CID | `275048534431364760dab56632013a77` |
| CSD | `400e00325b590000735f7f800a400025` |
| Date | `10/2019` |
| OEM ID | `0x5048` |
| Manufacturer ID | `0x000027` |

## What The SD Card Is Used For

`/home/3D` currently contains Deneb and stock print-side state:

- `/home/3D/deneb-uploads/`: uploaded G-code/UFP files and test fixtures.
- `/home/3D/deneb-materials/`: Deneb material catalog/cache.
- `/home/3D/deneb-print-history.json`: Deneb print history.
- `/home/3D/model.gcode`: stock/legacy print-file style location.
- `/home/3D/lost+found/`: ext4 recovery directory.

`/tmp/log/ultimaker` is the persistent log mount. Because `/var` is a symlink to `/tmp`, `/var/log/ultimaker` resolves to the same mounted SD partition. It currently contains:

- `system.log` and `system.log.old`.
- `deneb-printsvc.log`.
- Stock-era logs such as `print.log*`, `menu.log*`, `coordinator.log*`, `digitalfactory.log*`, and `wificonnect.log*`.

The OS root and Deneb binaries/config are not on this SD card:

| Storage | Role |
| --- | --- |
| `/dev/root` squashfs on internal flash | Read-only firmware root |
| `/dev/mtdblock6` JFFS2 overlay | Writable OS overlay, installed service scripts/config, Deneb binaries |
| `/dev/mmcblk0p1` | Print files and print metadata |
| `/dev/mmcblk0p2` | Persistent logs |
| `tmpfs` | Runtime `/tmp`, including `/var` symlink target except for the mounted log directory |

## Stock SD Initialization Logic

The live stock script is `/etc/init.d/sdcard`, started as `/etc/rc.d/S13sdcard` before `/etc/rc.d/S40fstab`.

Important stock constants from the script:

```sh
SD_DEVICE="/dev/mmcblk0"
GCODE_DEVICE="${SD_DEVICE}p1"
GCODE_LABEL="3D"
GCODE_MOUNT="/home/3D"
GCODE_SIZE="4G"
LOG_DEVICE="${SD_DEVICE}p2"
LOG_LABEL="log"
LOG_MOUNT="/var/log/ultimaker"
LOG_SIZE="4G"
```

If either expected partition is missing, mislabeled, or not mounted, stock firmware logs `Creating partitions on SD card`, writes a new DOS partition table, creates the two 4 GiB partitions, formats them as ext4 without journals, writes fstab entries, remounts block devices, and moves the system log to `${LOG_MOUNT}/system.log`.

If both expected partitions exist and are mounted, it logs `No need to create partitions on SD card`.

The script explicitly documents a limitation: it assumes factory preconditions with an empty card. If it rewrites the partition table on a non-empty card, Linux may continue using the old partition table until reboot, but the script does not implement that reboot.

## Stock Size Interpretation

We do not yet have independent evidence of the factory-installed physical SD card size for this exact unit. The live card is 16 GB, dated `10/2019`. It may be stock or may have been replaced by a previous owner/operator.

The unused 6.4 GiB does not imply damage. It matches stock behavior: the firmware formatter always allocates 4 GiB for `/home/3D` and 4 GiB for logs, regardless of larger card capacity. Larger cards therefore leave the remainder unused.

Minimum practical card size implied by the script is slightly more than 8 GiB, because it creates two 4 GiB partitions plus partition-table/alignment overhead. A smaller card would likely fail formatting or leave the second partition incomplete.

## Current Health Findings

Kernel and log evidence show both SD partitions need repair:

```text
EXT4-fs (mmcblk0p2): warning: mounting unchecked fs, running e2fsck is recommended
EXT4-fs (mmcblk0p1): warning: mounting unchecked fs, running e2fsck is recommended
EXT4-fs error (device mmcblk0p2): ext4_mb_generate_buddy:757: group 2, block bitmap and bg descriptor inconsistent
EXT4-fs (mmcblk0p1): error count since last fsck: 51
EXT4-fs (mmcblk0p2): error count since last fsck: 9
```

Read-only fsck was run while mounted, so it did not modify anything:

```sh
e2fsck -n /dev/mmcblk0p1
e2fsck -n /dev/mmcblk0p2
```

Results:

| Partition | Read-only fsck result | Notes |
| --- | --- | --- |
| `/dev/mmcblk0p1` | `rc=4` | Inode block count mismatch, block bitmap differences, free block count wrong. |
| `/dev/mmcblk0p2` | `rc=4` | Inode block count mismatch, block bitmap differences, free block count wrong. |

Both reports ended with `Filesystem still has errors`.


## Repair Performed On 2026-06-27

A local backup was captured before repair:

```text
C:\temp\Deneb\artifacts\sd-backup-20260627-121240\deneb-sd-backup.tgz
```

Repair sequence:

1. Stopped Deneb/stock services that write to print files or logs.
2. Stopped the system log service.
3. Unmounted `/home/3D` and `/tmp/log/ultimaker`.
4. Ran unmounted repair:

```sh
e2fsck -f -y /dev/mmcblk0p1
e2fsck -f -y /dev/mmcblk0p2
```

Repair results:

| Partition | Repair result | Immediate unmounted verification |
| --- | --- | --- |
| `/dev/mmcblk0p1` | Filesystem modified; inode block count, block bitmap, and free block counts fixed. | `e2fsck -f -n /dev/mmcblk0p1` returned `0`. |
| `/dev/mmcblk0p2` | Filesystem modified; inode block count, block bitmap, and free block counts fixed. | `e2fsck -f -n /dev/mmcblk0p2` returned `0`. |

After reboot, the printer returned on `10.10.10.241`, API status was `"idle"`, `deneb-ui`, `deneb-api`, `deneb-printsvc`, and `lighttpd` were running, and both SD partitions remounted normally:

```text
/dev/mmcblk0p1 -> /home/3D           3.9G size, 19.8M used
/dev/mmcblk0p2 -> /tmp/log/ultimaker 3.9G size, 16.7M used
```

Post-repair kernel log check showed only normal ext4 mount lines for `mmcblk0p1` and `mmcblk0p2`; the previous `running e2fsck`, `block bitmap`, and `error count since last fsck` messages were not present in the final boot log sample.

Note: later attempts to unmount the log partition after services had restarted showed it could become busy again, primarily from stock `coordinator.py` and logging processes holding `/var/log/ultimaker`. Avoid treating mounted `e2fsck -n` free-block-count output as authoritative while the log partition is live and being written.
## Repair Guidance

Do not repair these filesystems while they are mounted read/write.

Preferred repair path:

1. Stop services that write to `/home/3D` or `/var/log/ultimaker`.
2. Stop logging or redirect logs away from `/var/log/ultimaker`.
3. Unmount `/home/3D` and `/tmp/log/ultimaker`.
4. Run:

```sh
e2fsck -f -y /dev/mmcblk0p1
e2fsck -f -y /dev/mmcblk0p2
```

5. Reboot.
6. Re-check with `dmesg`, `logread`, `df -h`, and `e2fsck -n`.

If unmounting fails because the running system keeps the log partition busy, the safer option is to power down, remove the SD card, and run fsck from a separate Linux machine.

## Deneb Impact

The splash-screen hang observed on 2026-06-27 was not caused directly by the SD filesystem errors. The immediate cause was that `deneb-ui` was not running at boot despite the backend services being alive. Starting `/etc/init.d/deneb-ui start` restored the touchscreen, and `/etc/init.d/deneb-ui enable` recreated the boot symlink.

However, SD filesystem inconsistency can still create future confusing behavior:

- Lost or corrupted uploaded print files.
- Lost or truncated logs.
- Failed print-history writes.
- Strange persistence failures if stock or Deneb code expects `/home/3D` or `/var/log/ultimaker` to be reliable.

Track SD health separately from de-Python service parity so storage corruption is not mistaken for coordinator, printsvc, or UI logic regressions.