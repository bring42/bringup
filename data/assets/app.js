/* Bringup — device UI.
 *
 * Two ways in, deliberately:
 *   • a WebSocket at /ws pushes the same status object once a second
 *   • GET /api/status returns that identical object on demand
 * The socket is the fast path; the poll is the fallback that keeps the page
 * honest if the socket drops (which it will, every time the device reboots
 * after an update).
 */

const $ = (id) => document.getElementById(id);

// ── Fetch helper ────────────────────────────────────────────────────────────
// If you set an API token on the device, put it in localStorage under
// 'bringup-token' and every request below will carry it.
async function api(path, options = {}) {
  const token = localStorage.getItem('bringup-token');
  const headers = Object.assign({}, options.headers);
  if (token) headers['X-API-Key'] = token;
  if (options.body) headers['Content-Type'] = 'application/json';

  const res = await fetch(path, Object.assign({}, options, { headers }));
  if (!res.ok) {
    let detail = res.statusText;
    try { detail = (await res.json()).error || detail; } catch (_) {}
    throw new Error(`${res.status} ${detail}`);
  }
  return res.status === 204 ? null : res.json();
}

function setMsg(el, text, kind) {
  el.textContent = text;
  el.className = 'msg' + (kind ? ' ' + kind : '');
}

function fmtUptime(seconds) {
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  if (d) return `${d}d ${h}h`;
  if (h) return `${h}h ${m}m`;
  return `${m}m ${seconds % 60}s`;
}

// ── Local-intent tracking ───────────────────────────────────────────────────
// A control the user just moved must not be overwritten by a server frame until
// the device echoes the new value back. Two things go wrong without this:
//
//   • A ~1 Hz broadcast landing mid-drag writes the OLD value over the control.
//     Guarding on document.activeElement does NOT cover this — on touch,
//     dragging a range input frequently never focuses it.
//   • A frame serialized microseconds before the POST was processed arrives
//     after it, carrying the pre-change value.
//
// Both show up as the same symptom: the control jumps to the new position,
// snaps back for up to a second, then jumps forward again.
//
// So: from the first movement until the device confirms, the user's value wins.
// If the device never confirms (request lost, device rebooted), we stop holding
// after ECHO_TIMEOUT_MS and let the truth reassert itself — the UI must never
// get stuck showing a value the device doesn't actually have.
//
// ADDING A CONTROL: call holdLocal(key, value) the moment the user changes it,
// and read it back through settle(key, serverValue) when rendering.

const pendingLocal = new Map();     // key -> { value, since }
const ECHO_TIMEOUT_MS = 4000;       // > the 1 Hz broadcast interval, with slack

function holdLocal(key, value) {
  pendingLocal.set(key, { value, since: Date.now() });
}

function settle(key, serverValue) {
  const p = pendingLocal.get(key);
  if (!p) return serverValue;
  if (p.value === serverValue) {            // device confirmed — hand back control
    pendingLocal.delete(key);
    return serverValue;
  }
  if (Date.now() - p.since > ECHO_TIMEOUT_MS) {   // never confirmed — stop pretending
    pendingLocal.delete(key);
    return serverValue;
  }
  return p.value;
}

// ── Status rendering ────────────────────────────────────────────────────────

let userIsEditing = false;   // don't stomp inputs while they're being typed in

function renderStatus(s) {
  $('device-name').textContent = s.name || 'Bringup';
  document.title = s.name || 'Bringup';

  $('st-version').textContent = 'v' + s.version;
  $('st-board').textContent   = s.boardId || '—';
  $('st-ip').textContent      = s.ip || '—';
  $('st-uptime').textContent  = fmtUptime(s.uptime || 0);
  $('st-heap').textContent    = Math.round((s.heap || 0) / 1024) + ' KB';
  // Internal die temperature, not ambient — it reads well above room temp by
  // design. Watch the trend, not the absolute number.
  $('st-temp').textContent    = s.tempC != null ? s.tempC.toFixed(1) + ' °C' : '—';
  $('st-hash').textContent    = s.buildHash || '';

  const w = s.wifi || {};
  if (w.connected) {
    $('st-wifi').textContent = `${w.ssid} (${w.rssi} dBm)`;
  } else if (w.ssid) {
    $('st-wifi').textContent = `connecting to ${w.ssid}…`;
  } else {
    $('st-wifi').textContent = 'setup AP only';
  }

  if (!userIsEditing) {
    $('wifi-ssid').value = settle('wifiSSID', w.ssid || '');
    $('dev-name').value = settle('deviceName', s.name || '');
  }

  // Example app fields — remove with the rest of the blink example.
  if ('blinking' in s) {
    const v = settle('blinking', s.blinking);
    if ($('blink-on').checked !== v) $('blink-on').checked = v;
  }
  if ('periodMs' in s) {
    const v = settle('periodMs', s.periodMs);
    // Only write when it differs: assigning to a range input mid-interaction
    // can interrupt the drag even when the value is unchanged.
    if (Number($('blink-period').value) !== v) $('blink-period').value = v;
    $('blink-out').textContent = v;
  }
}

// ── Connection: WebSocket with polling fallback ─────────────────────────────

function setConn(text, kind) {
  const el = $('conn');
  el.textContent = text;
  el.className = 'pill pill-' + kind;
}

let ws = null;
let wsRetry = 1000;

function connectWs() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  ws = new WebSocket(`${proto}://${location.host}/ws`);

  ws.onopen = () => { setConn('live', 'ok'); wsRetry = 1000; };
  ws.onmessage = (ev) => {
    try {
      const msg = JSON.parse(ev.data);
      if (msg.type === 'state' && msg.status) renderStatus(msg.status);
    } catch (_) { /* ignore malformed frame */ }
  };
  ws.onclose = () => {
    setConn('reconnecting…', 'warn');
    // Back off to 10 s. A device mid-update is gone for ~30 s; hammering it
    // with reconnects just wastes its heap when it comes back.
    setTimeout(connectWs, wsRetry);
    wsRetry = Math.min(wsRetry * 2, 10000);
  };
  ws.onerror = () => ws.close();
}

async function pollStatus() {
  try {
    renderStatus(await api('/api/status'));
    if (!ws || ws.readyState !== WebSocket.OPEN) setConn('polling', 'warn');
  } catch (e) {
    setConn('offline', 'err');
  }
}

// ── WiFi ────────────────────────────────────────────────────────────────────

$('wifi-save').addEventListener('click', async () => {
  const body = {
    wifiSSID: $('wifi-ssid').value.trim(),
    // Empty password means "keep the saved one" — the device never sends it
    // back, so an empty field must not wipe a working credential.
    wifiPassword: $('wifi-pass').value,
    deviceName: $('dev-name').value.trim(),
  };
  if (!body.wifiSSID) { setMsg($('wifi-msg'), 'Enter a network name', 'err'); return; }

  $('wifi-save').disabled = true;
  setMsg($('wifi-msg'), 'Saving…');
  try {
    await api('/api/config', { method: 'POST', body: JSON.stringify(body) });
    holdLocal('wifiSSID', body.wifiSSID);
    holdLocal('deviceName', body.deviceName);
    $('wifi-pass').value = '';
    userIsEditing = false;
    setMsg($('wifi-msg'),
      'Saved. Connecting… if you are on the setup AP, the device will drop it ' +
      'briefly while it tries — this page will keep updating once it returns.',
      'ok');
  } catch (e) {
    setMsg($('wifi-msg'), 'Failed: ' + e.message, 'err');
  } finally {
    $('wifi-save').disabled = false;
  }
});

['wifi-ssid', 'wifi-pass', 'dev-name'].forEach((id) => {
  $(id).addEventListener('input', () => { userIsEditing = true; });
});

// ── Firmware update ─────────────────────────────────────────────────────────

let fwPollTimer = null;

function renderFw(s) {
  const msg = $('fw-msg');
  const bar = $('fw-progress');
  const installing = ['downloading', 'verifying', 'flashing', 'rebooting'].includes(s.phase);

  bar.hidden = !installing;
  if (installing) $('fw-bar').style.width = (s.percent || 0) + '%';

  $('fw-check').disabled = installing || s.phase === 'checking';
  $('fw-install').hidden = !s.updateAvailable || installing;

  switch (s.phase) {
    case 'checking':
      setMsg(msg, 'Checking GitHub…');
      break;
    case 'up_to_date':
      setMsg(msg, `Up to date (v${s.current})`, 'ok');
      break;
    case 'available': {
      const parts = [];
      if (s.appAvailable) parts.push('firmware');
      if (s.fsAvailable) parts.push('web UI');
      setMsg(msg, `v${s.latest} available — updates ${parts.join(' + ')}. ` +
                  (s.notes || ''), 'ok');
      break;
    }
    case 'downloading':
      setMsg(msg, `Downloading ${s.stage === 'fs' ? 'web UI' : 'firmware'}… ${s.percent}%`);
      break;
    case 'verifying':
      setMsg(msg, 'Verifying checksum…');
      break;
    case 'flashing':
      setMsg(msg, 'Writing to flash — do not power off');
      break;
    case 'rebooting':
      setMsg(msg, 'Rebooting. This page will reconnect on its own.', 'ok');
      break;
    case 'error':
      setMsg(msg, 'Failed: ' + (s.error || 'unknown error'), 'err');
      break;
    default:
      setMsg(msg, '');
  }

  // Keep polling while anything is in flight; stop once it settles.
  const busy = installing || s.phase === 'checking';
  if (busy && !fwPollTimer) {
    fwPollTimer = setInterval(pollFw, 700);
  } else if (!busy && fwPollTimer) {
    clearInterval(fwPollTimer);
    fwPollTimer = null;
  }
}

async function pollFw() {
  try {
    renderFw(await api('/api/firmware/status'));
  } catch (e) {
    // Expected while the device reboots after an update — keep trying.
  }
}

$('fw-check').addEventListener('click', async () => {
  setMsg($('fw-msg'), 'Checking…');
  try {
    await api('/api/firmware/check', { method: 'POST' });
    pollFw();
    if (!fwPollTimer) fwPollTimer = setInterval(pollFw, 700);
  } catch (e) {
    setMsg($('fw-msg'), 'Failed: ' + e.message, 'err');
  }
});

$('fw-install').addEventListener('click', async () => {
  if (!confirm('Install the update and reboot?')) return;
  try {
    await api('/api/firmware/update', { method: 'POST' });
    pollFw();
  } catch (e) {
    setMsg($('fw-msg'), 'Failed: ' + e.message, 'err');
  }
});

// ── Example app controls — delete with the blink example ────────────────────

async function sendBlink() {
  const body = {
    blinking: $('blink-on').checked,
    periodMs: Number($('blink-period').value),
  };
  holdLocal('blinking', body.blinking);
  holdLocal('periodMs', body.periodMs);
  try {
    await api('/api/blink', { method: 'POST', body: JSON.stringify(body) });
    setMsg($('blink-msg'), '');
  } catch (e) {
    setMsg($('blink-msg'), 'Failed: ' + e.message, 'err');
  }
}

$('blink-on').addEventListener('change', () => {
  holdLocal('blinking', $('blink-on').checked);
  sendBlink();
});
// 'input' fires continuously during a drag; 'change' only on release. Claim the
// value on the FIRST movement so a broadcast mid-drag can't snap it back.
$('blink-period').addEventListener('input', () => {
  const v = Number($('blink-period').value);
  holdLocal('periodMs', v);
  $('blink-out').textContent = v;
});
$('blink-period').addEventListener('change', sendBlink);

// ── Boot ────────────────────────────────────────────────────────────────────

connectWs();
pollStatus();
pollFw();
setInterval(pollStatus, 5000);   // fallback while the socket is down
