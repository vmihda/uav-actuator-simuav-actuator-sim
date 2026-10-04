const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const page = fs.readFileSync(path.resolve(__dirname, '../../include/web_page.h'), 'utf8');
const script = page.match(/<script>([\s\S]*?)<\/script>/)[1];
const settle = () => new Promise(resolve => setImmediate(resolve));

function fixture() {
  const elements = Object.fromEntries(['state','timer','pulse','error','start','stop','deploy','message']
    .map(id => [id, {textContent:'', disabled:true, dataset:{}, callbacks:{},
      addEventListener(event, callback) { this.callbacks[event] = callback; }}]));
  const calls = [];
  let next = {state:'SAFE', time_left:0, err:0, pulse_active:false, accepted:true, arming_seconds:10};
  let fail = false;
  let busy = false;
  let hold = false;
  const held = [];
  let poll;
  vm.runInNewContext(script, {
    document:{getElementById:id => elements[id]}, AbortController,
    setTimeout:()=>1, clearTimeout:()=>{}, setInterval:callback => { poll = callback; },
    fetch:async (url, options) => {
      calls.push({url, method:options.method});
      if (fail) throw new Error('offline');
      if (busy) return {ok:false, status:503, json:async()=>({error:'command_outcome_unknown'})};
      if (hold) return new Promise(resolve => held.push(data => resolve({ok:true, status:200, json:async()=>data})));
      return {ok:true, status:200, json:async()=>next};
    }
  });
  return {elements,calls,poll:()=>poll(),
    status(value) { next = {...next,...value}; }, offline() { fail = true; },
    busy(value) { busy = value; },
    hold(value) { hold = value; },
    release(index, value) { held[index]({...next,...value}); },
    click(id) { if (!elements[id].disabled) elements[id].callbacks.click(); }};
}

test('DEPLOY is enabled only after ARMED and sends POST', async () => {
  const ui = fixture();
  await settle();
  assert.equal(ui.elements.start.disabled, false);
  assert.equal(ui.elements.deploy.disabled, true);
  ui.status({state:'ARMING',time_left:9}); ui.poll(); await settle();
  assert.equal(ui.elements.deploy.disabled, true);
  assert.equal(ui.elements.timer.textContent, '9 с');
  ui.status({state:'ARMED',time_left:0}); ui.poll(); await settle();
  assert.equal(ui.elements.deploy.disabled, false);
  ui.status({state:'ACTUATED',pulse_active:true}); ui.click('deploy'); await settle();
  assert.deepEqual(ui.calls.at(-1), {url:'/deploy',method:'POST'});
  assert.equal(ui.elements.deploy.disabled, true);
  assert.equal(ui.elements.pulse.textContent, 'УВІМК');
});

test('failed or malformed status disables controls', async () => {
  for (const failure of ['offline','malformed']) {
    const ui = fixture(); await settle();
    if (failure === 'offline') ui.offline(); else ui.status({state:'UNKNOWN'});
    ui.poll(); await settle();
    assert.equal(ui.elements.state.textContent, 'НЕМАЄ ЗВ’ЯЗКУ');
    for (const action of ['start','stop','deploy']) assert.equal(ui.elements[action].disabled, true);
  }
});

test('FAULT allows STOP recovery and shows the error', async () => {
  const ui = fixture(); await settle();
  ui.status({state:'FAULT',err:5}); ui.poll(); await settle();
  assert.equal(ui.elements.stop.disabled, false);
  assert.equal(ui.elements.deploy.disabled, true);
  assert.equal(ui.elements.error.textContent, '5 / Збій сховища');
  ui.status({state:'SAFE',err:0}); ui.click('stop'); await settle();
  assert.deepEqual(ui.calls.at(-1), {url:'/stop',method:'POST'});
  assert.equal(ui.elements.start.disabled, false);
});

test('unconfirmed command reports unknown outcome and locks controls until refreshed', async () => {
  const ui = fixture(); await settle();
  ui.status({state:'ARMED',time_left:0}); ui.poll(); await settle();
  ui.busy(true); ui.click('deploy'); await settle();
  assert.deepEqual(ui.calls.at(-1), {url:'/deploy',method:'POST'});
  assert.equal(ui.elements.state.textContent, 'НЕВІДОМО');
  assert.match(ui.elements.message.textContent, /результат команди невідомий/);
  for (const action of ['start','stop','deploy']) assert.equal(ui.elements[action].disabled, true);
  ui.busy(false); ui.status({state:'ACTUATED',pulse_active:true}); ui.poll(); await settle();
  assert.equal(ui.elements.state.textContent, 'СПРАЦЮВАВ');
  assert.equal(ui.elements.stop.disabled, false);
});

test('status polling never disables or flickers the controls', async () => {
  const ui = fixture(); await settle();
  ui.hold(true); ui.poll();
  assert.equal(ui.calls.at(-1).url, '/status');
  assert.equal(ui.elements.start.disabled, false);
  assert.equal(ui.elements.stop.disabled, false);
});

test('a click during an in-flight poll is sent, not dropped', async () => {
  const ui = fixture(); await settle();
  ui.hold(true); ui.poll(); ui.click('start');
  assert.deepEqual(ui.calls.at(-1), {url:'/start',method:'POST'});
  assert.equal(ui.elements.start.disabled, true);
  assert.equal(ui.elements.stop.disabled, true);
});

test('an older poll response cannot overwrite a newer command response', async () => {
  const ui = fixture(); await settle();
  ui.hold(true); ui.poll(); ui.click('start');
  ui.release(1, {state:'ARMING',time_left:10}); await settle();
  ui.release(0, {state:'SAFE',time_left:0}); await settle();
  assert.equal(ui.elements.state.textContent, 'ЗВЕДЕННЯ');
  assert.equal(ui.elements.start.disabled, true);
  assert.equal(ui.elements.stop.disabled, false);
});

test('new fault codes have readable names', async () => {
  const ui = fixture(); await settle();
  const names = {6:'Некоректні налаштування', 7:'Кнопка залипла', 8:'Некоректний PWM-сигнал',
    9:'Втрата PWM-сигналу', 10:'Напруга живлення поза межами', 11:'Збій датчика',
    12:'Вусики замкнені (залипання)', 13:'Невідома'};
  for (const [code, name] of Object.entries(names)) {
    ui.status({state:'FAULT', err:Number(code)}); ui.poll(); await settle();
    assert.equal(ui.elements.error.textContent, code + ' / ' + name);
  }
});

test('the page is in Ukrainian and names every state', async () => {
  assert.match(page, /<html lang="uk">/);
  assert.match(page, /<title>Розумний вогнегасник<\/title>/);
  const ui = fixture(); await settle();
  const labels = {POST:'САМОПЕРЕВІРКА', SAFE:'БЕЗПЕЧНИЙ', ARMING:'ЗВЕДЕННЯ', ARMED:'ЗВЕДЕНО',
    ACTUATED:'СПРАЦЮВАВ', FAULT:'АВАРІЯ'};
  for (const [state, label] of Object.entries(labels)) {
    ui.status({state, err:0}); ui.poll(); await settle();
    assert.equal(ui.elements.state.textContent, label);
    assert.equal(ui.elements.state.dataset.state, state);
  }
});

test('ARMED explains that the whiskers can trigger deployment', async () => {
  const ui = fixture(); await settle();
  ui.status({state:'ARMING', time_left:5}); ui.poll(); await settle();
  assert.match(ui.elements.message.textContent, /вусики ще не активні/);
  ui.status({state:'ARMED', time_left:0}); ui.poll(); await settle();
  assert.match(ui.elements.message.textContent, /дотик вусиків/);
});
