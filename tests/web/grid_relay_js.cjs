#!/usr/bin/env node
// grid-relay.js's page logic in plain Node, with the loader, the kit and the
// module stubbed: no browser, no WebGL, no build. Checks what the browser
// harness (run_grid_relay.cjs) can't reach without timing luck:
//
//   - close() while the module loads: the relay finishes loading hidden and
//     idle, draws nothing, and the next request() shows it; it draws from
//     the first message the module accepts, not before;
//   - messages sent before the module are kept as the latest of each kind
//     (a bounded queue), then submitted in order once it is in;
//   - a short or foreign message is refused before the module, as after;
//   - keys: by default a key is forwarded to poll() and Tab or Space has its
//     browser default prevented; with {"keys": false} no key listener is
//     added, nothing is forwarded or prevented, and the kit is told the same.
//
//   node tests/web/grid_relay_js.cjs
//
// Exit code 0 when every check passed.
const fs = require('fs');
const path = require('path');
const vm = require('vm');

let failures = 0;
const check = (ok, what) => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`);
  if (!ok) failures++;
};

function page() {
  const submitted = [];
  let release;  // resolves StrataLoader.load
  const canvas = {
    hidden: true, width: 800, height: 600, clientWidth: 800, clientHeight: 600, style: {},
    parentElement: {}, addEventListener() {}, getBoundingClientRect: () => ({ left: 0, top: 0, width: 800, height: 600 }),
  };
  const heap = new Uint8Array(1 << 16);
  let next = 1024;
  const mod = {
    HEAPU8: heap, HEAPF32: new Float32Array(heap.buffer),
    _malloc(n) { const at = next; next += n; return at; }, _free() {},
    _gr_submit(at, n) { submitted.push(Array.from(heap.slice(at, at + n))); return 0; },
    _gr_wire_version: () => 256, _gr_set_ui_scale() {}, _gr_orbit: () => 0, _gr_messages: () => submitted.length,
  };
  const kit = { running: false, starts: 0, cam: {}, start() { this.starts++; this.running = true; return this; },
    stats: () => ({}) };
  let kitOpts = null;
  const listeners = {};
  const window = {
    addEventListener(type, fn) { (listeners[type] = listeners[type] || []).push(fn); },
  };
  const context = {
    window, console: { error() {}, log() {} }, Uint8Array, Float32Array, Map, Set, JSON, Math, Object, String,
    Number, Promise, parseInt, atob: (s) => Buffer.from(s, 'base64').toString('binary'),
    performance: { now: () => 0 },
    document: { getElementById: (id) => (id === 'relay' ? canvas : null) },
    fetch: async () => ({ ok: true, headers: { get: () => '1000' } }),
    matchMedia: () => ({ matches: false }),
    StrataLoader: { load: () => new Promise((r) => { release = () => r(mod); }), done() {}, slow() {}, fallback() {} },
    StrataKit: { create: (o) => { kitOpts = o; return kit; } },
  };
  context.globalThis = context;
  const source = fs.readFileSync(path.join(__dirname, '..', '..', 'web', 'kit', 'grid-relay', 'grid-relay.js'), 'utf8');
  vm.runInNewContext(source, context);
  return { relay: window.StrataGridRelay, canvas, kit, submitted, release: () => release(), listeners,
    kitOpts: () => kitOpts };
}

const settle = () => new Promise((r) => setTimeout(r, 0));
const msg = (kind, tag) => Buffer.from([0x47, 0x33, 1, 0, kind, 0, 0, 0, tag]).toString('base64');

(async () => {
  {
    const p = page();
    check(p.relay.request('{}') === 'loading', 'request() starts loading');
    await settle();
    p.relay.close();
    check(p.canvas.hidden, 'close() while loading hides the canvas');
    p.release();
    await settle();
    await settle();
    check(p.relay.state() === 'idle' && p.canvas.hidden && p.kit.starts === 0,
      'loaded after close(): idle, hidden, not drawing');
    check(p.relay.request('{}') === 'ready' && !p.canvas.hidden && !p.kit.running,
      'the next request() shows it, with nothing to draw yet');
    check(p.relay.send(msg(1, 1)) === 0 && p.kit.running && p.kit.starts === 1,
      'and the first message starts the frame loop');
  }
  {
    // messages queued, then close() while loading: they reach the module, but
    // nothing is drawn until the page asks again
    const p = page();
    p.relay.request('{}');
    await settle();
    p.relay.send(msg(1, 3));
    p.relay.close();
    p.release();
    await settle();
    await settle();
    check(p.submitted.length === 1 && p.relay.state() === 'idle' && p.canvas.hidden && p.kit.starts === 0,
      'queued, then closed while loading: submitted, idle, hidden, not drawing');
    check(p.relay.request('{}') === 'ready' && !p.canvas.hidden && p.kit.running,
      'the next request() shows it and draws what it has');
  }
  {
    // close() and then request() while loading: it is shown after all
    const p = page();
    p.relay.request('{}');
    await settle();
    await settle();
    p.relay.close();
    check(p.relay.request('{}') === 'loading', 'request() after close() while loading: still loading');
    p.release();
    await settle();
    await settle();
    check(p.relay.state() === 'ready' && !p.canvas.hidden && !p.kit.running,
      'loaded: ready and shown, not drawing before the first message');
    p.relay.send(msg(0, 1));
    check(p.kit.running, 'the first message starts the frame loop');
  }
  {
    const p = page();
    p.relay.request('{}');
    await settle();
    let queued = 0;
    for (let i = 0; i < 100; i++) queued += p.relay.send(msg(2, i)) === 1 ? 1 : 0;
    check(queued === 100, 'messages before the module are queued (1)');
    p.relay.send(msg(1, 7));
    p.relay.send(msg(2, 200));
    check(p.relay.send(Buffer.from([0x47, 0x33, 1]).toString('base64')) === -3, 'a short header is refused (-3)');
    check(p.relay.send(Buffer.from('PNG?????').toString('base64')) === -1, 'foreign bytes are refused (-1)');
    p.release();
    await settle();
    await settle();
    const kinds = p.submitted.map((m) => [m[4], m[8]]);
    check(JSON.stringify(kinds) === JSON.stringify([[1, 7], [2, 200]]),
      `the queue kept the latest of each kind, in order (${JSON.stringify(kinds)})`);
    check(p.relay.state() === 'ready' && p.kit.running && !p.canvas.hidden, 'and the relay draws');
  }
  {
    // keys: the default forwards them and keeps the browser's Tab and Space away
    const key = (code, key) => {
      const e = { type: 'keydown', code, key, repeat: false, prevented: false, preventDefault() { this.prevented = true; } };
      return e;
    };
    const p = page();
    p.relay.request('{}');
    await settle();
    p.release();
    await settle();
    await settle();
    const down = p.listeners.keydown || [];
    const tab = key('Tab', 'Tab'), space = key('Space', ' ');
    for (const f of down) { f(tab); f(space); }
    const got = JSON.parse(p.relay.poll()).filter((e) => e.type === 2).map((e) => e.scancode);
    check(down.length === 1 && tab.prevented && space.prevented && JSON.stringify(got) === '[43,44]',
      `keys by default: forwarded (${JSON.stringify(got)}), Tab and Space prevented`);
    check(p.kitOpts().keys === true, 'keys by default: the kit keeps its key handler');

    const q = page();
    q.relay.request('{"keys": false}');
    await settle();
    q.release();
    await settle();
    await settle();
    check(q.relay.state() === 'ready' && !q.listeners.keydown && !q.listeners.keyup,
      '{"keys": false}: no key listener on the window');
    check(q.kitOpts().keys === false, '{"keys": false}: the kit is told to leave keys alone');
    check(JSON.parse(q.relay.poll()).every((e) => e.type !== 2 && e.type !== 3 && e.type !== 9),
      '{"keys": false}: poll() has no key or text events');

    const r = page();
    r.relay.open({ keys: false });
    await settle();
    r.release();
    await settle();
    await settle();
    check(r.relay.state() === 'ready' && !r.listeners.keydown && r.kitOpts().keys === false,
      'open({keys: false}) from JavaScript: the same');
  }
  console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
