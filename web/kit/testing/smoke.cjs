// Smoke test for a page built on the Strata web kit, in headless Chromium.
//
//   node smoke.cjs URL OUT_DIR [--dpr 1,2,3] [--viewport WxH] [--quality N]
//                  [--live] [--context-loss] [--min-targets N] [--max-targets N]
//                  [--min-colours N]
//
// For each device pixel ratio (default 1, 2 and 3: a desktop with a mouse, a
// tablet and a phone with touch), it loads the page with ?drive=1 (the frame
// loop off; frames are drawn with __strataKit.drive(t)), scrolls the canvas
// into view if it is not wholly on screen (a scene in one section of a longer
// page, below the fold), and checks:
//   - the page comes up with no errors and its drawing buffer is the canvas's
//     CSS size times the (capped) device pixel ratio;
//   - the frame is not one flat colour (a CDP capture, OUT_DIR/dpr<N>.png);
//   - tap == pick: for each target the page lists (__strataKit.targets(),
//     viewport CSS pixels) that the canvas is the top element at, a real tap
//     or click there selects exactly what
//     a direct pick at that point returns. This is what catches a pointer
//     scaled by the drawing buffer instead of CSS pixels, which only shows
//     at a capped DPR (3 here, with the kit's default cap of 2);
//   - with --context-loss, the WebGL context is lost and restored
//     (WEBGL_lose_context) and the page draws and picks again;
//   - every .wasm the page loads is requested exactly once (cache hits
//     count): the loader's own fetch is handed to the glue, not repeated.
// Before the scene, the loading shell (../strata-loader.js, when it is beside
// this file) is checked on a blank page: progress() clamps to 0..1, never
// moves the bar back and shows its label; slow() called twice shows one bar,
// and at 390 px the slow bar's text and buttons keep one line each; with
// contain: true the shell covers its mount (a section below the fold), not
// the viewport, and without it, the viewport as before.
// Then strata-kit.js (beside testing/) is checked on blank pages with a stub
// module in place of the wasm: at DPR 2 an unstyled canvas (no CSS size)
// keeps one size over 12 frames of the real loop, its CSS size pinned, and
// canvases with a CSS width (and height) keep theirs, unpinned.
// Gestures on the same stub (#90): two synthetic touch pointers (a two-finger
// tap, a second finger during an orbit, a small pinch) and a right or
// middle click never call onTap; one finger moved under 6 px, a real
// touchscreen tap and a left click each call it once.
// --live keeps the page's own frame loop (no ?drive=1); captures are then of
// whatever frame is on screen. Exits 1 on any failure.
'use strict';
const fs = require('fs');
const path = require('path');
const kit = require('./lib.cjs');

function parseArgs(argv) {
  const [url, out, ...rest] = argv;
  const o = { url, out, dprs: [1, 2, 3], viewport: null, quality: null, live: false, contextLoss: false,
    minTargets: 1, maxTargets: 8, minColours: 8 };
  for (let i = 0; i < rest.length; i++) {
    const a = rest[i], next = () => rest[++i];
    if (a === '--dpr') o.dprs = next().split(',').map(Number);
    else if (a === '--viewport') { const [w, h] = next().split('x').map(Number); o.viewport = { width: w, height: h }; }
    else if (a === '--quality') o.quality = Number(next());
    else if (a === '--live') o.live = true;
    else if (a === '--context-loss') o.contextLoss = true;
    else if (a === '--min-targets') o.minTargets = Number(next());
    else if (a === '--max-targets') o.maxTargets = Number(next());
    else if (a === '--min-colours') o.minColours = Number(next());
    else throw new Error('unknown option ' + a);
  }
  if (!url || !out) {
    console.error('usage: node smoke.cjs URL OUT_DIR [--dpr 1,2,3] [--viewport WxH] [--quality N] [--live] [--context-loss]');
    process.exit(2);
  }
  return o;
}

const failures = [];
function check(ok, what) {
  console.log((ok ? 'ok   ' : 'FAIL ') + what);
  if (!ok) failures.push(what);
  return ok;
}

async function drawFrames(page, o, t) {
  if (o.live) { await page.waitForTimeout(1500); return; }
  await page.evaluate((tt) => window.__strataKit.drive(tt), t);
}

async function runDpr(browser, o, dpr) {
  const device = kit.deviceFor(dpr, o.viewport);
  const context = await browser.newContext(device);
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => {
    if (m.type() === 'error') errors.push(m.text());
    if (process.env.STRATA_KIT_VERBOSE) console.log(`  page: ${m.text()}`);
  });
  // Every .wasm the page asks for, cache hits included: the loader streams
  // the file once and hands the bytes to the glue (Module.wasmBinary), so a
  // second request means the glue ignored them and downloaded it again.
  const wasmRequests = new Map();
  page.on('request', (r) => {
    let pathname = '';
    try { pathname = new URL(r.url()).pathname; } catch (e) { return; }
    if (pathname.endsWith('.wasm')) wasmRequests.set(pathname, (wasmRequests.get(pathname) || 0) + 1);
  });
  const label = `DPR ${dpr} (${device.viewport.width}x${device.viewport.height}${device.hasTouch ? ', touch' : ''})`;
  console.log(`-- ${label}`);
  try {
    await page.goto(kit.withQuery(o.url, { drive: o.live ? null : 1, quality: o.quality }), { waitUntil: 'commit' });
    await kit.waitReady(page);
    await canvasIntoView(page);
    // Two frames: the first pays for shader compiles, the second is the one checked.
    await drawFrames(page, o, 0);
    await drawFrames(page, o, 0.5);
    const cdp = await context.newCDPSession(page);
    const png = await kit.capture(page, cdp);
    fs.writeFileSync(path.join(o.out, `dpr${dpr}.png`), png);
    const v = kit.variety(png);
    check(v.colours >= o.minColours && v.lumaStd > 2,
      `${label}: the frame is not one flat colour (${v.colours} colours, luma sd ${v.lumaStd.toFixed(1)})`);

    const size = await page.evaluate(() => {
      const k = window.__strataKit.kit, c = k.canvas, s = window.__strataKit.stats();
      return { cssW: c.clientWidth, cssH: c.clientHeight, w: c.width, h: c.height, dpr: k.dpr, cap: k.dprCap,
        engineW: s.width, engineH: s.height, primitives: s.primitives, meshes: s.meshes };
    });
    const want = Math.min(dpr, size.cap);
    check(Math.abs(size.w - Math.round(size.cssW * want)) <= 1 && size.engineW === size.w && size.engineH === size.h,
      `${label}: drawing buffer ${size.w}x${size.h} = CSS ${size.cssW}x${size.cssH} x ${want} (cap ${size.cap}), engine agrees`);
    check(size.primitives > 0, `${label}: the frame drew ${size.primitives} primitives`);
    check(Number.isInteger(size.meshes) && size.meshes >= 0, `${label}: stats count meshes (${size.meshes})`);

    await tapEqualsPick(page, o, label, device.hasTouch);

    if (o.contextLoss) {
      const lost = await page.evaluate(() => window.__strataKit.loseContext());
      if (check(lost, `${label}: WEBGL_lose_context is available`)) {
        await page.waitForFunction(() => window.__strataKit.lost === true, null, { timeout: 10000 });
        await page.evaluate(() => window.__strataKit.restoreContext());
        await page.waitForFunction(() => window.__strataKit.ready === true, null, { timeout: 30000 });
        await drawFrames(page, o, 1.0);
        await drawFrames(page, o, 1.5);
        const png2 = await kit.capture(page, cdp);
        fs.writeFileSync(path.join(o.out, `dpr${dpr}-restored.png`), png2);
        const v2 = kit.variety(png2);
        check(v2.colours >= o.minColours && v2.lumaStd > 2,
          `${label}: after a lost and restored context the frame is drawn again (${v2.colours} colours)`);
        await tapEqualsPick(page, o, label + ' after restore', device.hasTouch);
      }
    }
    const fetched = [...wasmRequests].map(([p, n]) => `${p.split('/').pop()} x${n}`).join(', ') || 'none';
    check(wasmRequests.size > 0 && [...wasmRequests.values()].every((n) => n === 1),
      `${label}: each .wasm is requested once (${fetched})`);
    check(errors.length === 0, `${label}: no page errors${errors.length ? ': ' + errors.slice(0, 3).join(' | ') : ''}`);
  } catch (e) {
    check(false, `${label}: ${String(e).split('\n')[0]}`);
  } finally {
    await context.close();
  }
}

// Scrolls the kit's canvas to the middle of the viewport, unless it is wholly
// on screen already (a full-screen canvas never moves), so that the capture
// and the tap targets are of the scene and not of the page above it.
async function canvasIntoView(page) {
  const moved = await page.evaluate(() => {
    const c = window.__strataKit.kit.canvas, r = c.getBoundingClientRect();
    if (r.top >= 0 && r.left >= 0 && r.bottom <= innerHeight && r.right <= innerWidth) return false;
    c.scrollIntoView({ block: 'center', inline: 'center' });
    return true;
  });
  if (moved) await page.evaluate(() => new Promise((r) => requestAnimationFrame(() => r())));
}

// The loading shell on its own, on a blank page: no scene, so it is quick.
async function loaderChecks(browser) {
  const src = path.join(__dirname, '..', 'strata-loader.js');
  if (!fs.existsSync(src)) { console.log('-- loader: strata-loader.js is not beside testing/, skipped'); return; }
  console.log('-- loader');
  const context = await browser.newContext();
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  try {
    await page.setContent('<!doctype html><html><head></head><body></body></html>');
    await page.addScriptTag({ content: fs.readFileSync(src, 'utf8') });
    const r = await page.evaluate(() => {
      const L = window.StrataLoader, out = {};
      const read = () => {
        const bar = document.querySelector('.sk-loader .sk-bar');
        return { now: bar && bar.getAttribute('aria-valuenow'), width: document.querySelector('.sk-loader .sk-fill').style.width,
          text: document.querySelector('.sk-loader .sk-size').textContent, valuetext: bar && bar.getAttribute('aria-valuetext') };
      };
      out.shown = L.show({ title: 'the test', fallback: { href: '#simpler', text: 'Simpler' } });
      out.state = L.state;
      L.progress(0.25); out.quarter = read();
      L.progress(0.1, 'Going back'); out.back = read();
      L.progress(0.6, 'Loading assemblies'); out.labelled = read();
      L.progress(7); out.over = read();
      L.progress(-1); out.under = read();
      L.progress(NaN); out.nan = read();
      L.done(); L.progress(0.5); out.afterDone = L.state;
      L.slow(); L.slow();
      out.slowBars = document.querySelectorAll('.sk-slow').length;
      document.querySelector('.sk-slow button[aria-label=Dismiss]').click();
      L.slow();
      out.slowAgain = document.querySelectorAll('.sk-slow').length;
      return out;
    });
    check(r.shown === true && r.state === 'loading', `loader: show() mounts the shell (${r.state})`);
    check(r.quarter.now === '25' && r.quarter.width === '25%' && r.quarter.text === '25%',
      `loader: progress(0.25) fills a quarter and says ${r.quarter.text}`);
    check(r.back.now === '25' && r.back.text === 'Going back', `loader: progress(0.1) after 0.25 keeps ${r.back.now}% (monotonic), shows its label`);
    check(r.labelled.now === '60' && r.labelled.text === 'Loading assemblies' && r.labelled.valuetext === 'Loading assemblies',
      `loader: progress(0.6, label) shows the label and sets aria-valuetext`);
    check(r.over.now === '100' && r.over.width === '100%', `loader: progress(7) clamps to ${r.over.now}%`);
    check(r.under.now === '100' && r.nan.now === '100', 'loader: progress(-1) and progress(NaN) do not move the bar back');
    check(r.afterDone === 'done', `loader: progress() after done() leaves the shell done (${r.afterDone})`);
    check(r.slowBars === 1, `loader: slow() twice shows ${r.slowBars} bar(s) (want 1)`);
    check(r.slowAgain === 1, `loader: slow() after the bar is dismissed shows it again (${r.slowAgain})`);
    // On a 390 px phone, with the default (longest) button text, the bar's
    // text and each button keep one line: the buttons wrap under the text.
    const narrow = await context.newPage();
    narrow.on('pageerror', (e) => errors.push(String(e)));
    await narrow.setViewportSize({ width: 390, height: 844 });
    await narrow.setContent('<!doctype html><html><head></head><body></body></html>');
    await narrow.addScriptTag({ content: fs.readFileSync(src, 'utf8') });
    const lines = await narrow.evaluate(() => {
      const L = window.StrataLoader;
      L.show({ title: 'the test', fallback: { href: '#simpler' } });
      L.done(); L.slow();
      const count = (n) => {
        const range = document.createRange();
        range.selectNodeContents(n);
        return new Set([...range.getClientRects()].map((q) => Math.round(q.top))).size;
      };
      return [...document.querySelectorAll('.sk-slow span, .sk-slow button')].map((n) => `${n.textContent}: ${count(n)}`);
    });
    check(lines.length === 3 && lines.every((l) => l.endsWith(': 1')),
      `loader: at 390 px the slow bar's text and buttons keep one line each (${lines.join('; ')})`);
    // contain: true scopes the shell to its mount, a section below the fold of
    // a long page; without contain the same mount still gets a viewport overlay.
    const embed = await context.newPage();
    embed.on('pageerror', (e) => errors.push(String(e)));
    await embed.setViewportSize({ width: 390, height: 844 });
    const embedPage = '<!doctype html><html><head></head><body style="margin:0">' +
      '<div style="height:1200px"></div><section id="s" style="height:300px;margin:0 20px"></section>' +
      '<div style="height:1200px"></div></body></html>';
    for (const contain of [true, false]) {
      await embed.setContent(embedPage);
      await embed.addScriptTag({ content: fs.readFileSync(src, 'utf8') });
      const g = await embed.evaluate((c) => {
        const s = document.getElementById('s');
        window.StrataLoader.show({ title: 'the test', mount: s, contain: c, fallback: { href: '#simpler' } });
        window.scrollTo(0, 1000);
        const rect = (n) => { const q = n.getBoundingClientRect(); return [q.left, q.top, q.width, q.height].map(Math.round).join(','); };
        const shell = document.querySelector('.sk-loader');
        const out = { pos: getComputedStyle(shell).position, shell: rect(shell), section: rect(s),
          viewport: `0,0,${innerWidth},${innerHeight}`, sectionPos: getComputedStyle(s).position };
        window.StrataLoader.done(); window.StrataLoader.slow();
        const bar = document.querySelector('.sk-slow');
        out.barInside = s.contains(bar);
        out.barPos = getComputedStyle(bar).position;
        return out;
      }, contain);
      if (contain) {
        check(g.pos === 'absolute' && g.shell === g.section && g.sectionPos === 'relative',
          `loader: contain covers its mount, scrolled (${g.pos} ${g.shell}, section ${g.section})`);
        check(g.barInside && g.barPos === 'absolute', `loader: contain puts the slow bar in its mount (${g.barPos})`);
      } else {
        check(g.pos === 'fixed' && g.shell === g.viewport && g.sectionPos === 'static',
          `loader: without contain the shell covers the viewport (${g.pos} ${g.shell})`);
        check(!g.barInside && g.barPos === 'fixed', `loader: without contain the slow bar is on the viewport (${g.barPos})`);
      }
    }
    check(errors.length === 0, `loader: no page errors${errors.length ? ': ' + errors.slice(0, 3).join(' | ') : ''}`);
  } catch (e) {
    check(false, `loader: ${String(e).split('\n')[0]}`);
  } finally {
    await context.close();
  }
}

// strata-kit.js on its own, on a blank page, with a stub module standing in
// for the wasm (the kit only calls its sk_* exports): checks of the page
// shell's own logic that no particular scene's CSS can hide.
const KIT_STUB = `window.__stubModule = function () {
  const heap = new ArrayBuffer(4096), i32 = new Int32Array(heap), f32 = new Float32Array(heap);
  f32.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], 256);
  const size = (w, h) => { i32[6] = w; i32[7] = h; };
  return {HEAP32: i32, HEAPF32: f32, stringToNewUTF8: () => 0, _free() {},
    _sk_init(sel, w, h) { size(w, h); return 0; }, _sk_resize: size, _sk_restore: () => 0,
    _sk_set_quality() {}, _sk_set_camera() {}, _sk_frame() { i32[8]++; }, _sk_stats: () => 0,
    _sk_view_proj: () => 1024};
};`;

async function kitStubPage(context, body) {
  const src = path.join(__dirname, '..', 'strata-kit.js');
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  await page.setContent(`<!doctype html><html><head></head><body>${body}</body></html>`);
  await page.addScriptTag({ content: fs.readFileSync(src, 'utf8') });
  await page.addScriptTag({ content: KIT_STUB });
  return { page, errors };
}

async function kitChecks(browser) {
  if (!fs.existsSync(path.join(__dirname, '..', 'strata-kit.js'))) {
    console.log('-- kit: strata-kit.js is not beside testing/, skipped');
    return;
  }
  console.log('-- kit (stub module)');
  const context = await browser.newContext(kit.deviceFor(2));
  try {
    await canvasSizeChecks(context);
    await gestureChecks(context);
  } catch (e) {
    check(false, `kit: ${String(e).split('\n')[0]}`);
  } finally {
    await context.close();
  }
}

// A canvas sized only by its width/height attributes is shown at them, so a
// buffer of CSS size x DPR used to feed back into its CSS size on every
// resize (#89). Each case runs the real frame loop (and ResizeObserver) for
// 12 frames at DPR 2.
async function canvasSizeChecks(context) {
  const cases = [
    { name: 'an unstyled canvas', css: '', pinned: true },
    { name: 'a canvas with a CSS size', css: 'width: 100%; height: 300px; display: block', pinned: false },
    { name: 'a canvas with only a CSS width', css: 'width: 100%; display: block', pinned: false },
  ];
  for (const c of cases) {
    const { page, errors } = await kitStubPage(context, `<canvas id="view" style="${c.css}"></canvas>`);
    const r = await page.evaluate(async () => {
      const canvas = document.getElementById('view');
      const k = StrataKit.create({ module: window.__stubModule(), canvas, idleOrbit: false });
      k.start();
      const seen = [];
      for (let i = 0; i < 12; i++) {
        await new Promise((res) => requestAnimationFrame(() => res()));
        seen.push(`${canvas.clientWidth}x${canvas.clientHeight}/${canvas.width}x${canvas.height}`);
      }
      k.running = false;
      return { seen, pinned: k.cssPinned, dpr: k.dpr, css: [canvas.clientWidth, canvas.clientHeight],
        buf: [canvas.width, canvas.height], engine: k.stats() };
    });
    const stable = new Set(r.seen).size === 1;
    check(stable && r.dpr === 2 && r.buf[0] === Math.round(r.css[0] * r.dpr) && r.buf[1] === Math.round(r.css[1] * r.dpr) &&
      r.engine.width === r.buf[0] && r.engine.height === r.buf[1],
      `kit: ${c.name} at DPR 2 keeps its size over 12 frames (CSS/buffer ${stable ? r.seen[0] : r.seen.join(' -> ')})`);
    check(r.pinned === c.pinned, `kit: ${c.name} is ${r.pinned ? '' : 'not '}pinned (want ${c.pinned ? '' : 'not '}pinned)`);
    check(errors.length === 0, `kit: ${c.name}: no page errors${errors.length ? ': ' + errors.slice(0, 3).join(' | ') : ''}`);
    await page.close();
  }
}

// One gesture, one tap at most (#90): a second finger used to reset the
// drag distance, and every finger lifted under 6 px tapped. Touch pointers
// are synthetic PointerEvents (two at once, which Playwright's touchscreen
// cannot make); the clean tap and the mouse clicks are real input.
async function gestureChecks(context) {
  const { page, errors } = await kitStubPage(context,
    '<canvas id="view" style="display:block;width:400px;height:300px;touch-action:none"></canvas>');
  await page.evaluate(() => {
    const canvas = document.getElementById('view');
    window.__taps = [];
    StrataKit.create({ module: window.__stubModule(), canvas, driven: true, idleOrbit: false,
      pick: (x, y) => ({ id: `${Math.round(x)},${Math.round(y)}` }),
      onTap: (hit) => window.__taps.push(hit && hit.id) });
    const r = canvas.getBoundingClientRect();
    // steps: [type, pointerId, x, y] in canvas CSS pixels; pointer 1 is primary.
    window.__touch = (steps) => {
      window.__taps = [];
      for (const [type, id, x, y] of steps) {
        canvas.dispatchEvent(new PointerEvent(type, { pointerId: id, pointerType: 'touch', isPrimary: id === 1,
          clientX: r.left + x, clientY: r.top + y, button: type === 'pointermove' ? -1 : 0,
          buttons: type === 'pointerup' ? 0 : 1, bubbles: true, cancelable: true }));
      }
      return window.__taps;
    };
  });
  const touches = [
    { name: 'a two-finger tap', steps: [['pointerdown', 1, 100, 100], ['pointerdown', 2, 160, 100],
      ['pointerup', 2, 160, 100], ['pointerup', 1, 100, 100]] },
    { name: 'a two-finger tap, first finger lifted first', steps: [['pointerdown', 1, 100, 100],
      ['pointerdown', 2, 160, 100], ['pointerup', 1, 100, 100], ['pointerup', 2, 160, 100]] },
    { name: 'a second finger touched briefly during a one-finger orbit', steps: [['pointerdown', 1, 100, 100],
      ['pointermove', 1, 130, 110], ['pointermove', 1, 160, 120], ['pointerdown', 2, 250, 200],
      ['pointerup', 2, 250, 200], ['pointermove', 1, 170, 120], ['pointerup', 1, 170, 120]] },
    { name: 'a small pinch', steps: [['pointerdown', 1, 100, 100], ['pointerdown', 2, 140, 100],
      ['pointermove', 1, 99, 100], ['pointermove', 2, 141, 100], ['pointerup', 1, 99, 100], ['pointerup', 2, 141, 100]] },
    { name: 'a cancelled touch', steps: [['pointerdown', 1, 100, 100], ['pointercancel', 1, 100, 100]] },
  ];
  for (const t of touches) {
    const taps = await page.evaluate((steps) => window.__touch(steps), t.steps);
    check(taps.length === 0, `kit: ${t.name} does not tap (${taps.length} tap(s)${taps.length ? ': ' + taps.join(' ') : ''})`);
  }
  const one = await page.evaluate(() => window.__touch([['pointerdown', 1, 120, 80], ['pointermove', 1, 122, 81],
    ['pointerup', 1, 122, 81]]));
  check(one.length === 1 && one[0] === '122,81', `kit: one finger moved 3 px taps once where it lifted (${one.join(' ') || 'no tap'})`);

  const box = await page.evaluate(() => { const r = document.getElementById('view').getBoundingClientRect(); return { x: r.left, y: r.top }; });
  const real = async (what, act) => {
    await page.evaluate(() => { window.__taps = []; });
    await act();
    await page.waitForTimeout(50);
    return page.evaluate(() => window.__taps);
  };
  const tapped = await real('tap', () => page.touchscreen.tap(box.x + 200, box.y + 150));
  check(tapped.length === 1 && tapped[0] === '200,150', `kit: a real touchscreen tap taps once (${tapped.join(' ') || 'no tap'})`);
  const right = await real('right', () => page.mouse.click(box.x + 200, box.y + 150, { button: 'right' }));
  check(right.length === 0, `kit: a right click does not tap (${right.length} tap(s))`);
  const middle = await real('middle', () => page.mouse.click(box.x + 200, box.y + 150, { button: 'middle' }));
  check(middle.length === 0, `kit: a middle click does not tap (${middle.length} tap(s))`);
  const left = await real('left', () => page.mouse.click(box.x + 210, box.y + 140));
  check(left.length === 1 && left[0] === '210,140', `kit: a left click taps once (${left.join(' ') || 'no tap'})`);
  check(errors.length === 0, `kit: gestures: no page errors${errors.length ? ': ' + errors.slice(0, 3).join(' | ') : ''}`);
  await page.close();
}

async function tapEqualsPick(page, o, label, touch) {
  await canvasIntoView(page);
  // Only targets the canvas itself is under: a point covered by the page's
  // own HTML (a panel, a card) is not the canvas's to answer.
  const targets = (await page.evaluate(() => {
    const c = window.__strataKit.kit.canvas;
    return window.__strataKit.targets().filter((t) => t && document.elementFromPoint(t.x, t.y) === c);
  })).filter((t) => t.x > 2 && t.y > 2 && t.x < page.viewportSize().width - 2 && t.y < page.viewportSize().height - 2)
    .slice(0, o.maxTargets);
  if (!check(targets.length >= o.minTargets, `${label}: ${targets.length} target(s) on screen (need ${o.minTargets})`)) return;
  let same = 0, onTarget = 0;
  const bad = [];
  for (const t of targets) {
    // A card the last tap opened may now cover this point.
    if (!(await page.evaluate(([x, y]) => document.elementFromPoint(x, y) === window.__strataKit.kit.canvas, [t.x, t.y]))) continue;
    const direct = await page.evaluate(([x, y]) => window.__strataKit.pickAt(x, y), [t.x, t.y]);
    await page.evaluate(() => { window.__strataKit.kit.lastTapId = '__no-tap__'; });
    if (touch) await page.touchscreen.tap(t.x, t.y);
    else await page.mouse.click(t.x, t.y);
    await page.waitForFunction(() => window.__strataKit.lastTapId !== '__no-tap__', null, { timeout: 10000 }).catch(() => {});
    const tapped = await page.evaluate(() => window.__strataKit.lastTapId);
    if (tapped === direct) same++; else bad.push(`${t.id}: tap ${tapped} != pick ${direct}`);
    if (direct === t.id) onTarget++;
  }
  const tried = same + bad.length;
  check(tried >= o.minTargets && same === tried, `${label}: tap == pick at ${same}/${tried} targets${bad.length ? ' (' + bad.slice(0, 3).join('; ') + ')' : ''}`);
  // Informational: a target can sit behind another thing, so a pick there
  // may rightly return the nearer one.
  console.log(`     ${label}: the pick at a target's own point returned it for ${onTarget}/${tried}`);
}

(async () => {
  const o = parseArgs(process.argv.slice(2));
  fs.mkdirSync(o.out, { recursive: true });
  const browser = await kit.launch();
  try {
    await loaderChecks(browser);
    await kitChecks(browser);
    for (const dpr of o.dprs) await runDpr(browser, o, dpr);
  } finally {
    await browser.close();
  }
  if (failures.length) {
    console.log(`\n${failures.length} check(s) failed`);
    process.exit(1);
  }
  console.log('\nall checks passed');
})().catch((e) => { console.error(e); process.exit(1); });
