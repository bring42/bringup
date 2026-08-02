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

## Verified on hardware

Not a design sketch — this ran on a Seeed XIAO ESP32-S3 (8 MB), updating itself
from a real GitHub release:

```
Updater: app=0.0.1 fs=0.0.1 latest=0.1.0 -> UPDATE AVAILABLE (app=yes fs=yes)
   fs    0% → 97%   (1,966,080 B)
   app   0% → 98%   (1,059,664 B)
Updater: app image verified + flashed
Updater: update complete, rebooting
=== Bringup v0.1.0 ===
```

**41 seconds** from trigger to reboot. Afterwards the device reported
`firmware 0.1.0 / filesystem 0.1.0 / up_to_date`.

That second number is the one that matters. `filesystem: 0.1.0` is what proves
the LittleFS image was actually written rather than silently skipped — the
"new firmware, stale UI" failure this whole two-version design exists to
prevent. A single version field cannot tell you that.

What the run exercised, all of which had only been reasoned about before:

- the `/releases/latest/download/` redirect, which crosses **three** hosts
  (`github.com` → `objects.githubusercontent.com` →
  `release-assets.githubusercontent.com`) — hence the forced redirect-following
- board-id matching between the compiled-in `BRINGUP_BOARD_ID` and the manifest
- the semver compare against a real published manifest
- streaming SHA-256 verification of both images before either boot switch
- the atomic fs-then-app ordering with exactly **one** reboot
- `esp_ota_mark_app_valid_cancel_rollback()` on the way back up

Resource cost during the transfer: free heap fell from ~250 KB to 189 KB
(minimum 181 KB) for the TLS session and buffers, and die temperature rose
50 °C → 54 °C, settling afterwards. Comfortable margins on an S3; worth
re-checking on a C3, which has less RAM to spare.

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

## Self-hosting (and keeping your source private)

The updater is not tied to GitHub. Two strings in `src/constants.h` decide where
it looks, and they default to a GitHub release only because that needs no setup:

```c
BRINGUP_MANIFEST_URL     // where manifest.json lives
BRINGUP_ASSET_BASE_URL   // prefix for the .bin names inside it (must end in '/')
```

Override them and nothing else changes:

```ini
build_flags =
    -DBRINGUP_MANIFEST_URL='"https://fw.example.com/manifest.json"'
    -DBRINGUP_ASSET_BASE_URL='"https://fw.example.com/"'
```

**Why you might want to.** GitHub releases inherit repo visibility, so OTA from
GitHub means a public repo. Self-hosting separates the two: the source stays
private and only the images are served. It also lets you pin **your own CA**
instead of betting on a third party's certificate rotation — see the TLS notes
below, where that turns out to be the strongest option available.

Be clear about what it does *not* buy you. Anything the device can fetch without
per-device credentials, anyone can fetch. Self-hosting makes your **source**
private, not your **binaries**. Genuinely confidential firmware needs encrypted
images or per-device keys, which is a different project.

### What a host must do

- Serve `manifest.json` and the `.bin` files it names, over **HTTPS**
- Be reachable **without credentials** — the device sends none
- That's it. Redirects are followed, so CDNs and object storage are fine, and
  `setInsecure()` means even a self-signed certificate works today

Cloudflare R2, S3, a static bucket, nginx on a VPS, or a Pi on your LAN all
qualify. CI uploads the same files `gen_manifest.py` already produces; you
overwrite `manifest.json` at a fixed URL instead of relying on GitHub's
`latest` redirect.

### Manifest format

`scripts/gen_manifest.py` writes this, but if you self-host you own it, so here
is the contract the firmware actually parses:

```json
{
  "version": "1.2.0",
  "buildHash": "a1b2c3d",
  "notes": "Shown in the update panel.",
  "boards": {
    "seeed-xiao-esp32s3": {
      "app": { "file": "bringup-seeed-xiao-esp32s3.bin",
               "sha256": "<64 lowercase hex>", "size": 1059664 },
      "fs":  { "file": "littlefs-seeed-xiao-esp32s3.bin",
               "sha256": "<64 lowercase hex>", "size": 1966080 }
    }
  }
}
```

Rules the device enforces:

- `version` is required, and compared as `x.y.z` (suffixes ignored)
- the key under `boards` must equal the firmware's `BRINGUP_BOARD_ID` — a
  mismatch is reported as *"manifest has no assets for board ..."*, not a
  silent no-op
- `app` is required; `sha256` must be 64 hex chars and `size` non-zero, or the
  check fails before anything is downloaded
- `fs` is optional — omit it and only the firmware updates
- `file` is appended to `BRINGUP_ASSET_BASE_URL` verbatim
- `size` must match the server's `Content-Length`; a mismatch aborts

Only these fields are parsed (ArduinoJson filters the rest at parse time to keep
memory down on a C3), so extra keys are safe to add for your own tooling.

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
