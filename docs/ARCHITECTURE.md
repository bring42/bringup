# Architecture

## Layout

```
src/
  main.cpp          globals, setup(), loop(), auth        ← rarely edit
  app.cpp / app.h   YOUR PROJECT                          ← this is the file
  constants.h       identity, timings, GitHub repo        ← edit once per project
  storage.h/.cpp    NVS config
  logging.h         LOG_INFO(tag, fmt, ...)
  core/
    body_guard.h    one-body-at-a-time guard for async POSTs
  network/
    wifi.cpp        AP+STA, provisioning, diagnostics
    server.cpp      static serving, health, routes, WebSocket
    ota.cpp         push OTA + mDNS (dev)
    updater.cpp     pull OTA from GitHub Releases (deployed)
    semver.h        version comparison (pure, host-tested)
  api/
    status.cpp      GET /api/status + the shared status payload
    config.cpp      GET/POST /api/config (WiFi provisioning)
    firmware.cpp    /api/firmware/* (update control)
data/               web UI, packed into LittleFS
partitions/         flash layouts — see docs/PARTITIONS.md
scripts/            PlatformIO hooks + release tooling
test/               host unit tests (`pio test -e native`)
```

## The two tasks

Almost every bug in a project like this comes from confusing these.

**The loop task** runs `loop()`: WiFi maintenance, `appLoop()`, the watchdog.
It owns hardware and long-lived state.

**The AsyncTCP task** runs every HTTP handler. It must never block — a slow
handler stalls the whole web server — and it must never touch hardware or
mutate state the loop reads every pass.

The pattern used everywhere here: a handler validates the request, publishes an
intent (a flag, an atomic, a queue), and returns. The loop task picks it up.

- `requestWifiConnect()` — the smallest example: sets one atomic bool.
- `network/updater.cpp` — the full version: a FreeRTOS queue and a worker task,
  because the HTTPS download would block for tens of seconds.
- `app.cpp` — the example blinker does the same with three atomics.

Two related traps the base already handles:

- **Chunked bodies interleave.** AsyncTCP delivers body chunks from concurrent
  requests on the same task, so a bare `static String` accumulator lets two
  POSTs corrupt each other. `beginBody()`/`endBody()` grant one body at a time,
  with a timeout so a client that disconnects mid-body doesn't wedge the slot.
- **Chunks aren't NUL-terminated.** `String((char*)data)` reads past the buffer;
  always use `String((char*)data, len)`.

## The app seam

Six functions in `app.h`. The base calls them; you fill them in.

| Hook | Task | Purpose |
|---|---|---|
| `appSetup()` | loop | Initialize hardware |
| `appLoop()` | loop | Per-pass work, non-blocking |
| `appRegisterRoutes(server)` | setup | Your HTTP endpoints |
| `appBuildStatus(json)` | either | Add fields to the status payload |
| `appOnNetworkUp/Down()` | loop | Start/stop network-dependent work |
| `appOnUpdateStarting()` | either | Quiesce before flash is rewritten |

`appRegisterRoutes()` is called *after* the base routes, so your endpoints can
never shadow `/api/status`, `/api/config`, or `/api/firmware/*`.

`appBuildStatus()` feeds both `GET /api/status` and the WebSocket broadcast —
one shape, so the UI reads the same fields whether it polled or was pushed.

## Web UI

`data/` is the source of truth; it is packed into a LittleFS image by
`pio run -t uploadfs`. Two things happen automatically at pack time
(`scripts/gzip_web_files.py`):

1. **Gzip.** ESPAsyncWebServer serves `.gz` transparently. Roughly a 4× saving.
2. **Cache stamps.** Every `/assets/…?v=` URL is rewritten to an 8-hex hash of
   the file's bytes. That is what makes the week-long `max-age` on `/assets/`
   safe: the URL changes exactly when the content does. HTML entry points are
   served `no-cache` because they carry those stamped URLs.

Without the stamps, "I updated the UI but my phone still shows the old one" is
a bug you cannot fix from the device.

The server also stops serving static files entirely while an OTA is rewriting
the filesystem partition, returning 503 instead of reading a partition
mid-erase.

### Controls the user drives

Any control the user can move needs **echo suppression**, or a periodic status
broadcast will fight the user for it. Two races produce the same symptom — the
control jumps to the new value, snaps back for up to a second, then jumps
forward again:

- a broadcast lands *mid-interaction* and writes the old value over the control;
- a frame serialized just before the POST was handled arrives just after it.

Guarding on `document.activeElement` does **not** fix this. On touch devices,
dragging a range input frequently never focuses it, so the guard silently does
nothing on exactly the platform where sliders matter most.

`data/assets/app.js` uses `holdLocal(key, value)` / `settle(key, serverValue)`:
from the user's first movement until the device echoes the value back, the
user's value wins. If the device never confirms, the hold expires after
`ECHO_TIMEOUT_MS` and server truth reasserts — a stuck UI showing a value the
device doesn't have is worse than a brief flicker. Add a control, and you add
one `holdLocal()` where it changes and one `settle()` where it renders.

## WiFi

Permanent AP+STA. The setup access point never goes down, so a device whose
network disappeared is always reachable at `192.168.4.1`.

The non-obvious parts, each of which is a bug someone already paid for:

- **Auto-reconnect is disabled** (`setAutoReconnect(false)`,
  `persistent(false)`). The IDF's built-in reconnect scans behind your back, and
  every scan channel-hops the single radio away from the SoftAP — which kills a
  provisioning client's DHCP handshake. Reconnects are owned by
  `handleWifiMaintenance()` instead.
- **Reconnect is skipped while a client sits on the setup AP**, for the same
  reason. It resumes the moment they leave, and says so in the log rather than
  being silently idle.
- **Saving credentials connects immediately** rather than waiting for the next
  retry — which would never fire during provisioning, because the phone doing
  the provisioning is the client blocking retries.
- **Every WiFi event is logged**, with IDF disconnect reasons translated
  (`AUTH_FAIL`, `NO_AP_FOUND`, `4WAY_HANDSHAKE_TIMEOUT`…). While disconnected, a
  periodic scan logs every BSSID broadcasting your SSID with channel, RSSI and
  auth mode. This distinguishes "not visible", "too weak", "wrong password" and
  "wrong auth mode" from each other in seconds.

## Auth

`config.authToken` empty means open — necessary for first-boot provisioning.
Once set, it is accepted as `Authorization: Bearer`, `X-API-Key`, or `?token=`.

This is plain HTTP on a LAN: the token crosses the wire in the clear. It stops
casual access and nothing more. Don't treat it as protection from anyone
already on your network.
