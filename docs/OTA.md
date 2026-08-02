# Over-the-air updates

Two independent paths, for two different situations.

| | Push OTA (`network/ota.cpp`) | Pull OTA (`network/updater.cpp`) |
|---|---|---|
| Who starts it | You, from your machine | The device, on request from its UI |
| Source | Your local build | A GitHub release |
| When | Development | Deployed devices |
| Reaches | One device on your LAN | Every device you ever flashed |

## Pull OTA: how a release reaches a device

1. You push a tag: `git tag v1.2.0 && git push --tags`.
2. `.github/workflows/release.yml` builds `firmware.bin` and `littlefs.bin` for
   every board in `boards.json`, and `scripts/gen_manifest.py` writes a
   `manifest.json` with the SHA-256 and size of each image.
3. A device asked to check fetches
   `https://github.com/<owner>/<repo>/releases/latest/download/manifest.json`,
   finds its own entry by `BRINGUP_BOARD_ID`, and compares the published version
   against **both** its firmware version and its filesystem version.
4. On install, each image is streamed straight into flash while being hashed.
   The hash is compared to the manifest **before** the boot partition is
   switched. A mismatch or a truncated download aborts and leaves the running
   app untouched.

The tag push is the entire deploy. There is no separate publish step — which
also means a broken tag reaches every device that checks, so let the Build
workflow go green on `main` first.

## The two versions

The single most useful detail in this design: **firmware and filesystem are
versioned independently.**

- Firmware version is compiled in (`FIRMWARE_VERSION`).
- Filesystem version is a file, `/fsver`, written into the LittleFS image at
  build time by `scripts/version.py`.

Without this, updating the firmware but not the filesystem gives you a new app
serving an old UI, silently, with no way for the device to notice. Here, the
check reports both, and an unstamped filesystem (one built before the stamp
existed) reads as "older than any release" — so it heals itself on the next
check instead of staying stale forever.

`POST /api/firmware/update` is atomic: it flashes whichever images are behind —
filesystem first, then firmware — and reboots **once**, so the two move
together. The order is deliberate. If the firmware flash fails after the
filesystem succeeded, the device reboots onto the *old* app with the *new* UI
(tolerated, and the next check re-offers the firmware). It never boots a new app
on an old UI.

## Setup checklist

1. Set `BRINGUP_GH_OWNER` and `BRINGUP_GH_REPO` in `src/constants.h`.
2. Make sure every board's `id` in `boards.json` is stable — it is baked into
   the firmware, so **renaming an id orphans every deployed device** from its
   update path.
3. Push a tag. Until a release exists, "Check for update" fails with an HTTP
   error, which is correct rather than broken.

The repo must be public, or at least its release assets must be — the device
sends no credentials. It reads public release assets over plain HTTPS, which is
also why no token ever needs to live on the device.

## Security posture

What is protected:

- **Transport** — HTTPS throughout, following GitHub's redirect to
  `objects.githubusercontent.com`.
- **Integrity** — streaming SHA-256, verified before the boot switch.
- **Anti-brick** — writes go to the inactive OTA slot; the bootloader is only
  repointed at a complete, verified image.

What is **not**, and should be understood before deploying anything that
matters:

1. **No code signing.** The checksum comes from the same manifest as the image,
   so anyone who can publish to your repo can publish firmware to your devices.
   Real hardening is ESP32 Secure Boot v2 with signed images.
2. **No certificate pinning.** TLS uses `setInsecure()`. A machine-in-the-middle
   with a forged certificate could serve a substitute image with a matching
   hash. Pinning is the fix, at the cost of a device that breaks when the root
   rotates.
3. **No automatic rollback.** A verified-but-broken build stays booted.
   `esp_ota_mark_app_valid_cancel_rollback()` is already called at startup, so
   enabling `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is the only remaining step
   if you want auto-revert.

For a device on your own LAN this is a reasonable posture. For anything exposed
to the internet, or anything you'd be unhappy to have someone else flash, work
through that list first.

## Push OTA (development)

Uncomment in your env:

```ini
upload_protocol = espota
upload_port = bringup.local
upload_flags =
    --port=3232
    --auth=<your auth token, or the AP password>
```

The password is `config.authToken` if you set one, otherwise `AP_PASSWORD` from
`constants.h`. Push OTA cannot change the partition table either — see
[PARTITIONS.md](PARTITIONS.md).

## What the UI shows

`POST /api/firmware/check` and `/update` return immediately; the blocking HTTPS
transfer runs on its own worker task so it never stalls the async web server.
The UI polls `GET /api/firmware/status` for a phase (`checking`, `available`,
`downloading`, `verifying`, `flashing`, `rebooting`, `error`), a percentage, and
which image is being written.
