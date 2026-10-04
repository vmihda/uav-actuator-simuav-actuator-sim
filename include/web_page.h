#pragma once

#include <Arduino.h>

static const char kWebPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="uk">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark"><title>Розумний вогнегасник</title>
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
<header><h1>РОЗУМНИЙ ВОГНЕГАСНИК</h1><span>ESP32 / ПРОГРАМНА ІМІТАЦІЯ</span></header>
<p class="intro">Вогнегасник з контактними вусиками. «Старт» запускає таймер безпеки, а коли він закінчиться,
вогнегасник зведено: досить торкнутися вусиками об’єкта, і він спрацює.</p>
<section class="panel" aria-label="Стан контролера">
<div class="label">Стан</div><div id="state" class="state">ПІДКЛЮЧЕННЯ</div>
<div class="metrics"><div><div class="label">Таймер безпеки</div><div id="timer" class="value">-- с</div></div>
<div><div class="label">Імпульс спрацювання</div><div id="pulse" class="value">ВИМК</div></div></div>
<dl><dt class="label">Помилка</dt><dd id="error">--</dd></dl>
<div class="controls"><button id="start" disabled>Старт (зведення)</button><button id="stop" disabled>Стоп (знешкодження)</button>
<button id="deploy" disabled>Спрацювання</button></div>
<p id="message" class="message" role="status" aria-live="polite">Очікування контролера...</p>
</section>
<p class="foot">Спрацювати можна тільки в стані ЗВЕДЕНО, кнопкою або дотиком вусиків; поки йде таймер безпеки,
вусики не реагують. Якщо дотику так і не було, вогнегасник згодом сам повертається в стан БЕЗПЕЧНИЙ.
Стоп скасовує зведення й обриває імпульс. Сам імпульс лише імітація, силового виходу в прошивці немає.
Стан оновлюється раз на секунду.</p>
</main>
<script>
'use strict';
const elements = Object.fromEntries(['state','timer','pulse','error','start','stop','deploy','message'].map(id => [id,document.getElementById(id)]));
const errors = ['Немає','Збій самоперевірки','Некоректна команда','Тайм-аут керування','Переповнення рядка UART',
  'Збій сховища','Некоректні налаштування','Кнопка залипла','Некоректний PWM-сигнал','Втрата PWM-сигналу',
  'Напруга живлення поза межами','Збій датчика','Вусики замкнені (залипання)'];
const states = {POST:'САМОПЕРЕВІРКА',SAFE:'БЕЗПЕЧНИЙ',ARMING:'ЗВЕДЕННЯ',ARMED:'ЗВЕДЕНО',ACTUATED:'СПРАЦЮВАВ',FAULT:'АВАРІЯ'};
let current = null;
let commandPending = false;
let inFlight = 0;
let issued = 0;
let shown = 0;
function buttons() {
  // Only a command in flight locks the controls; background polling never does.
  elements.start.disabled = commandPending || !current || current.state !== 'SAFE';
  elements.stop.disabled = commandPending || !current || current.state === 'POST';
  elements.deploy.disabled = commandPending || !current || current.state !== 'ARMED' || current.err !== 0;
}
function show(seq, update) {
  // A response to an older request must not overwrite a newer one.
  if (seq < shown) return;
  shown = seq;
  update();
}
function render(data) {
  if (!data || !Object.prototype.hasOwnProperty.call(states, data.state)
      || !Number.isFinite(data.time_left) || !Number.isInteger(data.err)) throw new Error('Invalid status');
  current = data;
  elements.state.textContent = states[data.state];
  elements.state.dataset.state = data.state;
  elements.timer.textContent = data.time_left + ' с';
  elements.pulse.textContent = data.pulse_active ? 'УВІМК' : 'ВИМК';
  elements.error.textContent = data.err + ' / ' + (errors[data.err] || 'Невідома');
  elements.message.textContent = data.accepted === false ? 'Команду відхилено в поточному стані.' :
    data.state === 'FAULT' ? 'Аварія. Стоп запустить самоперевірку ще раз і, якщо все гаразд, поверне в стан БЕЗПЕЧНИЙ.' :
    data.state === 'ARMING' ? 'Іде таймер безпеки, вусики ще не активні. Не закривайте сторінку: поки вона відкрита, зв’язок не обривається.' :
    data.state === 'ARMED' ? 'Зведено: дотик вусиків або кнопка «Спрацювання» запускає імітацію.' :
    data.state === 'ACTUATED' ? 'Спрацювання зафіксовано. Стоп повертає в стан БЕЗПЕЧНИЙ.' :
    'Підключено. Таймер безпеки: ' + data.arming_seconds + ' с.';
}
function unavailable(state, message) {
  current = null;
  elements.state.textContent = state;
  elements.state.dataset.state = '';
  elements.timer.textContent = '-- с';
  elements.pulse.textContent = '--';
  elements.error.textContent = '--';
  elements.message.textContent = message;
}
async function request(path, method = 'GET') {
  const command = method !== 'GET';
  if (command ? commandPending : inFlight > 0) return;
  const seq = ++issued;
  ++inFlight;
  if (command) {
    commandPending = true;
    buttons();
  }
  const controller = new AbortController();
  const deadline = setTimeout(() => controller.abort(), 2000);
  try {
    const response = await fetch(path, {method,cache:'no-store',signal:controller.signal});
    if (response.status === 503) {
      show(seq, () => unavailable('НЕВІДОМО', 'Контролер зайнятий: результат команди невідомий. Оновлюю стан...'));
      return;
    }
    if (!response.ok && response.status !== 409) throw new Error('HTTP ' + response.status);
    const data = await response.json();
    show(seq, () => render(data));
  } catch (error) {
    show(seq, () => unavailable('НЕМАЄ ЗВ’ЯЗКУ', 'Зв’язок втрачено. Перепідключення...'));
  } finally {
    clearTimeout(deadline);
    --inFlight;
    if (command) commandPending = false;
    buttons();
  }
}
for (const action of ['start','stop','deploy']) elements[action].addEventListener('click', () => request('/' + action,'POST'));
request('/status');
setInterval(() => request('/status'),1000);
</script></body></html>)HTML";
