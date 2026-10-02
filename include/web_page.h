#pragma once

#include <Arduino.h>

static const char kWebPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark"><title>ACTUATOR-SIM</title>
<style>
:root{font:16px system-ui,sans-serif;color:#e9eff5;background:#10161c;--line:#35404b}
*{box-sizing:border-box}body{margin:0;padding:32px 18px}main{max-width:680px;margin:auto}
header{display:flex;justify-content:space-between;align-items:center;gap:12px;border-bottom:1px solid var(--line);padding-bottom:20px}
h1{font-size:20px;letter-spacing:.06em;margin:0}header span{font-size:12px;color:#aac1d3;border:1px solid var(--line);padding:6px 10px;border-radius:4px}
.intro{color:#9eacb8;line-height:1.6;margin:20px 0}.panel{background:#19222b;border:1px solid var(--line);border-radius:10px;padding:24px}
.label{font-size:12px;letter-spacing:.12em;color:#9eacb8;text-transform:uppercase}.state{font:600 clamp(32px,8vw,46px) ui-monospace,monospace;margin:8px 0 24px;color:#a9b6c2}
.state[data-state=SAFE]{color:#8bd7aa}.state[data-state=ARMING]{color:#ffd07c}.state[data-state=ARMED]{color:#ffa391}.state[data-state=ACTUATED]{color:#c7b0f2}.state[data-state=FAULT]{color:#ff8590}
.metrics{display:grid;grid-template-columns:1fr 1fr;gap:20px;border-top:1px solid var(--line);padding-top:20px}.value{font:600 26px ui-monospace,monospace;margin-top:6px}
.controls{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:20px}button{font:600 15px system-ui;padding:15px;border:1px solid #55697a;border-radius:6px;background:#283949;color:#eef6ff;cursor:pointer;min-height:48px}button:hover:enabled{background:#384f64}button:focus-visible{outline:3px solid #9ecbf0;outline-offset:3px}button:disabled{opacity:.35;cursor:default}#deploy{grid-column:1/-1;background:#63382d;border-color:#a76350}#deploy:hover:enabled{background:#81503f}
.message{min-height:48px;line-height:1.5;color:#b6c8d8;margin:18px 0 0}.foot{color:#9eacb8;font-size:13px;line-height:1.7;margin-top:22px}dl{display:flex;gap:8px;margin:12px 0}dt,dd{margin:0}
@media(max-width:420px){body{padding:20px 12px}header{align-items:flex-start;flex-direction:column}.panel{padding:20px}}
</style>
</head>
<body><main>
<header><h1>ACTUATOR-SIM</h1><span>ESP32 / SOFTWARE SIMULATION</span></header>
<p class="intro">Manual recovery-controller test panel. Arm, monitor the countdown and simulate deployment.</p>
<section class="panel" aria-label="Controller status">
<div class="label">Controller state</div><div id="state" class="state">CONNECTING</div>
<div class="metrics"><div><div class="label">Time remaining</div><div id="timer" class="value">-- s</div></div>
<div><div class="label">Simulated pulse</div><div id="pulse" class="value">OFF</div></div></div>
<dl><dt class="label">Error</dt><dd id="error">--</dd></dl>
<div class="controls"><button id="start" disabled>Start (Arm)</button><button id="stop" disabled>Disarm (Stop)</button>
<button id="deploy" disabled>Deploy (Activate)</button></div>
<p id="message" class="message" role="status" aria-live="polite">Waiting for the controller...</p>
</section>
<p class="foot">Deployment is available only in ARMED. STOP cancels preparation and ends an active pulse.
Status refreshes every second. The actuator pulse is simulated; this firmware has no power output.</p>
</main>
<script>
'use strict';
const elements = Object.fromEntries(['state','timer','pulse','error','start','stop','deploy','message'].map(id => [id,document.getElementById(id)]));
const errors = ['None','Self-test failed','Invalid command','Control timeout','UART line overflow','Storage failure'];
let current = null;
let pending = false;
function buttons() {
  elements.start.disabled = pending || !current || current.state !== 'SAFE';
  elements.stop.disabled = pending || !current || current.state === 'POST';
  elements.deploy.disabled = pending || !current || current.state !== 'ARMED' || current.err !== 0;
}
function render(data) {
  if (!data || !['POST','SAFE','ARMING','ARMED','ACTUATED','FAULT'].includes(data.state)
      || !Number.isFinite(data.time_left) || !Number.isInteger(data.err)) throw new Error('Invalid status');
  current = data;
  elements.state.textContent = data.state;
  elements.state.dataset.state = data.state;
  elements.timer.textContent = data.time_left + ' s';
  elements.pulse.textContent = data.pulse_active ? 'ON' : 'OFF';
  elements.error.textContent = data.err + ' / ' + (errors[data.err] || 'Unknown');
  elements.message.textContent = data.accepted === false ? 'Command rejected in the current state.' :
    data.state === 'FAULT' ? 'Fault detected. STOP retries health checks and returns to SAFE when they pass.' :
    data.state === 'ARMING' ? 'Preparing. Keep this page open to maintain the control heartbeat.' :
    data.state === 'ARMED' ? 'Ready for simulated deployment.' :
    data.state === 'ACTUATED' ? 'Deployment latched. STOP returns to SAFE.' :
    'Connected. Preparation interval: ' + data.arming_seconds + ' seconds.';
}
async function request(path, method = 'GET') {
  if (pending) return;
  pending = true;
  buttons();
  const controller = new AbortController();
  const deadline = setTimeout(() => controller.abort(), 2000);
  try {
    const response = await fetch(path, {method,cache:'no-store',signal:controller.signal});
    if (!response.ok && response.status !== 409) throw new Error('HTTP ' + response.status);
    render(await response.json());
  } catch (error) {
    current = null;
    elements.state.textContent = 'DISCONNECTED';
    elements.state.dataset.state = '';
    elements.timer.textContent = '-- s';
    elements.pulse.textContent = '--';
    elements.error.textContent = '--';
    elements.message.textContent = 'Connection lost. Reconnecting...';
  } finally {
    clearTimeout(deadline);
    pending = false;
    buttons();
  }
}
for (const action of ['start','stop','deploy']) elements[action].addEventListener('click', () => request('/' + action,'POST'));
request('/status');
setInterval(() => request('/status'),1000);
</script></body></html>)HTML";
