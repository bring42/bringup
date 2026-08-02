# Heat and power

An ESP32-S3 running WiFi continuously gets warm — warm enough that the first
reaction is usually "something's wrong". Usually nothing is. This documents what
was measured, on what, and how, so you can tell a normal warm board from an
actual fault.

## Is my board's heat a problem?

Check these before chasing the temperature:

| Signal | Where | Healthy looks like |
|---|---|---|
| Free heap trend | `vitals` log, every 30 s | Flat. A steady decline is a leak. |
| Reboots | Serial | No unexpected boot banners. |
| Watchdog | Serial | No `Task watchdog got triggered`. |
| Die temp trend | `vitals` log / UI status card | Rises, then **plateaus**. |

A board that plateaus, holds its heap, and never reboots is fine. The ESP32-S3's
die temperature sensor reads well above ambient by design — 55–60 °C at idle
with WiFi up is normal for a small board with no copper to spread heat into.

If instead the temperature climbs without ever levelling off, or the heap
declines steadily, that's worth investigating.

## Measured on a XIAO ESP32-S3

Idle, WiFi AP+STA up, ambient ~22 °C, via `temperatureRead()` in the 30 s
`vitals` log:

| Configuration | Result |
|---|---|
| `yield()` in loop, 240 MHz | 60–62 °C |
| `delay(1)` in loop, 240 MHz | 52 → 62 °C, **still climbing at 6.5 min** |
| `delay(1)` in loop, 160 MHz | **flat 57–58 °C from 30 s** |
| PSRAM disabled (`-UBOARD_HAS_PSRAM`) | No measurable change |
| **STA-only, zero SoftAP clients** (live, 1h45m uptime) | **45 °C** |

That last row is the striking one. Every bench figure above was taken with a
client parked on the setup AP. The same board, joined to a normal network with
`apClients: 0`, reads **45 °C** — about 12 °C cooler, and a bigger effect than
the clock rate. Serving a SoftAP client is the dominant heat source here, which
means a device in its normal deployed state runs considerably cooler than it
does during setup.

**Clock rate is the only lever that moved the number.** 160 MHz is the default
for the XIAO S3 env here; it is ample for a web UI, and it reaches a stable
temperature instead of creeping.

`delay(1)` in `loop()` is still the right call — spinning on `yield()` starves
the FreeRTOS idle task for no benefit, since every task in the loop is
`millis()`-gated — but it is **not** a thermal fix. Don't expect it to be one.

## How to measure it properly

The first pass at this got the wrong answer, in an instructive way.

`delay(1)` was measured for 150 s and compared against a converged baseline. It
looked ~8 °C cooler and was reported as a win. It wasn't: the board simply hadn't
finished warming up. Run out to 390 s, the same build climbed to the same 62 °C.

Two rules that follow:

1. **Run to convergence.** A silicon thermal time constant here is several
   minutes. Any reading under ~5 minutes is a transient, not a result. If the
   number is still moving, you don't have a result yet.
2. **Equalize the starting state.** Back-to-back flashes never let the board
   cool, so each successive test starts hotter and is biased warm. Either let it
   settle between runs or compare only converged plateaus.

The measurement loop used here:

```bash
pio run -e seeed_xiao_esp32s3 -t upload
pio device monitor    # watch the 30 s vitals lines until the value stops moving
```

## Remaining levers, untested

- **WiFi modem sleep.** `setupWiFi()` calls `WiFi.setSleep(false)`, inherited
  from a mains-powered design where sleep made the SoftAP sluggish. This is
  likely the largest remaining lever and has not been measured here. Note that a
  SoftAP must stay awake to beacon regardless, so the gain probably only appears
  once the device is STA-only with no AP clients.
- **Dropping to 80 MHz.** Untested; likely cooler again, at some cost to TLS
  handshake and JSON-parsing speed during OTA.
- **AP-on-demand.** The setup AP runs permanently by design, so a device whose
  network vanished is always recoverable. Shutting it down once STA connects
  would save radio power at the cost of that guarantee — a real tradeoff, not a
  free win.

## If your project is battery powered

None of the above is enough on its own. The base is built for a mains-powered
device that is always reachable — permanent AP+STA, no modem sleep, an always-on
web server. Battery operation needs a different shape entirely: deep sleep
between duty cycles, and the radio down in between. Start from
`WiFi.setSleep(true)`, drop the SoftAP, and expect to restructure `loop()`.
