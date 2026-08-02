# Partitions

**Read this before changing a partition table. It is the one decision in this
template that you cannot undo over the air.**

## Why the template pins a table at all

The stock Arduino `default.csv` splits 4 MB into 1.25 MB app slots and 1.375 MB
of filesystem. A project with a web UI is the opposite shape — the UI is a few
hundred KB, while the app carries TLS, an async web server, and your logic.
Left on the stock table, a project like this hits 90%+ of its app slot while
most of the filesystem sits empty.

The tables in `partitions/` rebalance toward the app:

| Flash | app0 / app1 (each) | filesystem | table |
|---|---|---|---|
| 4 MB | 1.75 MB | 384 KB | `partitions_4mb.csv` |
| 8 MB | 3 MB | 1.875 MB | `partitions_8mb.csv` |
| 16 MB | 6 MB | 3.875 MB | `partitions_16mb.csv` |

Two OTA app slots are mandatory for self-updating: the device writes the new
image into the slot it is *not* running from, and only points the bootloader at
it once the image is complete and its SHA-256 matches. That is what makes a
failed update non-fatal. It also means **every 1 KB of app headroom costs 2 KB
of flash**, because A/B slots must be identical in size.

All three tables keep `nvs` and `otadata` at their stock offsets, so WiFi
credentials and the auth token survive a repartition — only the filesystem
needs re-uploading.

## The rule that matters

> A partition table change takes effect only via a **USB-serial flash**. It
> cannot be delivered over OTA.

A device running an old layout must not OTA onto images built for a new one:
an app image larger than the old slot physically cannot fit, and the filesystem
image size won't match its partition. The update would fail at best and leave a
half-written filesystem at worst.

The practical consequence: **decide your table before you flash anything you
can't easily reach again.** Fixing it later means physical access, a USB cable,
and one `pio run -t flashall` per device. This is why the template ships an
explicit table for every board rather than inheriting whatever the board
definition happens to default to.

## Picking a table for a new board

Match the flash size, and make sure the env declares it:

```ini
[env:my-board]
extends = common
board = my-board
board_upload.flash_size = 8MB                            ; must match the table
board_build.partitions = partitions/partitions_8mb.csv
```

`board_upload.flash_size` and the table must agree. If the table describes more
flash than the chip has, the device fails to boot after flashing — the symptom
is a bootloader message about a partition extending past the end of the device.

Not sure how much flash a board has? `esptool.py flash_id`, or check the chip
marking (an `N8` suffix in a module name means 8 MB, `N16` means 16 MB).

## Checking your headroom

Every build prints it:

```
Flash: [======    ]  59.6% (used 1093384 bytes from 1835008 bytes)
```

The denominator is your app slot size, so it is also a direct check that the
right table got applied. If it reads 1,310,720 on a 4 MB board, you are on the
stock table and `board_build.partitions` isn't taking effect.

Leave real headroom. An app slot above ~85% is uncomfortable: the OTA image you
publish later has to fit the slot on devices already in the field, and by then
the table is frozen.

## Writing your own table

Constraints the bootloader enforces:

- App partitions must be aligned to 64 KB (`0x10000`).
- `app0` and `app1` must be the same size.
- Offsets must be ordered and non-overlapping, and the total must fit the chip.
- Keep `nvs` at `0x9000` and `otadata` at `0xe000` if you want existing device
  config to survive.

After editing, a full USB flash is required — `pio run -t flashall`, not OTA.
