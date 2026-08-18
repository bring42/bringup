# Bringup

**Board bring-up, already done.** WiFi provisioning, web UI and self-updating
firmware for ESP32.

A starter template for the plumbing every ESP32 project needs anyway: getting on
the network, being reachable from a browser, and shipping updates after the
board leaves your desk. Copy it, rename it, and start on the part you actually
care about.

Extracted from [LUME](https://github.com/bring42/LUME), where all of it is
running on hardware.

## What you get before writing any project code

- **WiFi provisioning** — permanent AP+STA. Join the setup AP, enter
  credentials, done. The AP never goes down, so a device whose network vanished
  is always reachable again.
- **Web UI** — served from LittleFS, gzipped, with content-hashed asset URLs so
  a browser can never pin a stale build. WebSocket push plus a polling
  fallback.
- **Self-updating firmware** — the device checks your GitHub releases, compares
  both its firmware *and* its filesystem version, and installs SHA-256 verified
  images into the inactive OTA slot. `git push --tags` is the entire deploy.
  Verified end to end on hardware: 0.0.1 → 0.1.0, both images, one reboot,
  41 seconds ([details](docs/OTA.md#verified-on-hardware)).
- **Partition tables** for 4/8/16 MB flash, sized for a web-UI project rather
  than the stock Arduino defaults.
- **WiFi diagnostics** that tell you *why* a connection failed — translated IDF
  disconnect reasons, and a scan showing every AP broadcasting your SSID with
  channel, RSSI and auth mode.
- Push OTA + mDNS for development, NVS config, optional API token, `/health`,
  a watchdog, and host-runnable unit tests.

## Quick start

```bash
git clone https://github.com/bring42/bringup my-project && cd my-project
rm -rf .git
python3 scripts/rename_project.py "My Project" --gh-owner you --gh-repo my-project
python3 scripts/gzip_web_files.py          # re-stamp the asset cache-busters
git init && git add -A && git commit -m "Initial commit from Bringup"
```

Do the rename **first**: board ids are compiled into the firmware, so renaming
after devices are deployed cuts them off from their update path.

Flash it:

```bash
pio run -e esp32-s3-devkitc-1 -t flashall
```

`flashall` uploads the firmware *and* the web UI. Plain `-t upload` skips the
UI, and the device will tell you so with a 503.

Then join the **My Project-Setup** access point, open `192.168.4.1`, and enter
your WiFi credentials. After that the device is at `http://my-project.local`.

## Choose your own adventure

Every default works with zero setup; every upgrade is a build flag. You can ship
something useful without reading past this table.

| | Default (nothing to do) | Upgrades |
|---|---|---|
| **Update source** | GitHub releases | self-host anywhere — private source, your own CA ([OTA.md](docs/OTA.md#self-hosting-and-keeping-your-source-private)) |
| **Time** | none — the base is `millis()`-only | DHCP-supplied (free, no provider to pick) · explicit NTP servers |
| **TLS on updates** | encrypted, certificate not checked | verify against pinned roots ([OTA.md](docs/OTA.md#tls-certificate-verification-opt-in)) — needs a clock |
| **API auth** | open — needed for first-boot provisioning | token via `Authorization` / `X-API-Key` / `?token=` |
| **Partitions** | 8 MB table, 3 MB app slots | 4 / 16 MB tables ([PARTITIONS.md](docs/PARTITIONS.md)) |

Time sync, for example, is off because the base needs no wall clock and it adds
an outbound dependency you didn't ask for. Turning it on:

```ini
build_flags =
    -DBRINGUP_TIME_SYNC=1
    ; optional — omit to use only what your router advertises via DHCP option 42
    -DBRINGUP_NTP_SERVER1='"pool.ntp.org"'
```

## Writing your project

Everything you write goes in [`src/app.cpp`](src/app.cpp), behind six hooks:

```cpp
void appSetup();                              // init hardware
void appLoop();                               // per-pass work
void appRegisterRoutes(AsyncWebServer&);      // your HTTP endpoints
void appBuildStatus(JsonObject status);       // your fields in /api/status + WS
void appOnNetworkUp();  void appOnNetworkDown();
void appOnUpdateStarting();                   // quiesce before flash rewrite
```

`app.cpp` ships with a blinking-LED example wired end to end — a route, an
atomic handoff to the loop task, a status field and a UI control. Delete it and
write your own; it is there to show the shape, particularly the handoff pattern
that keeps HTTP handlers off your hardware.

## Enabling OTA

1. Set `BRINGUP_GH_OWNER` / `BRINGUP_GH_REPO` in `src/constants.h` (or pass
   `--gh-owner`/`--gh-repo` to the rename script).
2. Push a tag:

```bash
git tag v0.1.0 && git push --tags
```

CI builds every board in `boards.json`, checksums the images, and publishes them
with a `manifest.json`. Devices pick it up from the **Firmware update** panel in
their own UI.

Adding a board means editing `boards.json` and adding a matching `[env:…]` —
the version script, the manifest generator and both workflows all read that one
file.

## Before you flash anything you can't reach again

**Pick your partition table first.** A partition change cannot be delivered over
OTA — only over USB. Every env here pins an explicit table from `partitions/`;
match it to your board's flash size and check the `Flash:` percentage the build
prints. Details and the reasoning: [docs/PARTITIONS.md](docs/PARTITIONS.md).

## Commands

```bash
pio run -t flashall              # firmware + web UI (use this for a first flash)
pio run -t upload                # firmware only
pio run -t uploadfs              # web UI only (auto-gzips and re-stamps data/)
pio test -e native               # host unit tests, no hardware
pio device monitor               # serial log
```

## Docs

- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — the two-task model, the app seam,
  and the non-obvious WiFi/web behavior
- [PARTITIONS.md](docs/PARTITIONS.md) — flash layouts and the one-way door
- [OTA.md](docs/OTA.md) — the release pipeline, and an honest account of what
  the update path does and does not protect against
- [POWER.md](docs/POWER.md) — why the board runs warm, what was measured, and
  how to tell a normal warm board from a fault

## Known limits

- **OTA is not code-signed.** The checksum ships in the same manifest as the
  image, so anyone who can publish to your repo can publish firmware to your
  devices. Certificate verification is available but **off by default** — see
  [OTA.md](docs/OTA.md#security-posture) before exposing a device beyond a LAN.
- **TLS verification is not yet hardware-verified.** `BRINGUP_TLS_VERIFY=1`
  compiles and its compile-time guards work, but the runtime paths have not been
  observed on a device. Off by default, so it affects nothing unless you opt in.
- **The API token is plain HTTP.** It stops casual access on your LAN, nothing
  more.
- Only the **XIAO ESP32-S3** has been run on hardware — including a full OTA
  update. The other three envs build in CI and are inherited from a working
  project, but are otherwise untested. Adding a board is a `boards.json` entry,
  an env, and a build.
- **The board runs warm** (~58 °C die at idle). That is normal for an S3 with
  WiFi up; see [POWER.md](docs/POWER.md) for measurements and levers.
