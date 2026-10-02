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
  let poll;
  vm.runInNewContext(script, {
    document:{getElementById:id => elements[id]}, AbortController,
    setTimeout:()=>1, clearTimeout:()=>{}, setInterval:callback => { poll = callback; },
    fetch:async (url, options) => {
      calls.push({url, method:options.method});
      if (fail) throw new Error('offline');
      return {ok:true, status:200, json:async()=>next};
    }
  });
  return {elements,calls,poll:()=>poll(),
    status(value) { next = {...next,...value}; }, offline() { fail = true; },
    click(id) { if (!elements[id].disabled) elements[id].callbacks.click(); }};
}

test('DEPLOY is enabled only after ARMED and sends POST', async () => {
  const ui = fixture();
  await settle();
  assert.equal(ui.elements.start.disabled, false);
  assert.equal(ui.elements.deploy.disabled, true);
  ui.status({state:'ARMING',time_left:9}); ui.poll(); await settle();
  assert.equal(ui.elements.deploy.disabled, true);
  assert.equal(ui.elements.timer.textContent, '9 s');
  ui.status({state:'ARMED',time_left:0}); ui.poll(); await settle();
  assert.equal(ui.elements.deploy.disabled, false);
  ui.status({state:'ACTUATED',pulse_active:true}); ui.click('deploy'); await settle();
  assert.deepEqual(ui.calls.at(-1), {url:'/deploy',method:'POST'});
  assert.equal(ui.elements.deploy.disabled, true);
  assert.equal(ui.elements.pulse.textContent, 'ON');
});

test('failed or malformed status disables controls', async () => {
  for (const failure of ['offline','malformed']) {
    const ui = fixture(); await settle();
    if (failure === 'offline') ui.offline(); else ui.status({state:'UNKNOWN'});
    ui.poll(); await settle();
    assert.equal(ui.elements.state.textContent, 'DISCONNECTED');
    for (const action of ['start','stop','deploy']) assert.equal(ui.elements[action].disabled, true);
  }
});

test('FAULT allows STOP recovery and shows the error', async () => {
  const ui = fixture(); await settle();
  ui.status({state:'FAULT',err:5}); ui.poll(); await settle();
  assert.equal(ui.elements.stop.disabled, false);
  assert.equal(ui.elements.deploy.disabled, true);
  assert.equal(ui.elements.error.textContent, '5 / Storage failure');
  ui.status({state:'SAFE',err:0}); ui.click('stop'); await settle();
  assert.deepEqual(ui.calls.at(-1), {url:'/stop',method:'POST'});
  assert.equal(ui.elements.start.disabled, false);
});
