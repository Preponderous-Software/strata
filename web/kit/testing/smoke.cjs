// Smoke test for a page built on the Strata web kit, in headless Chromium.
//
//   node smoke.cjs URL OUT_DIR [--dpr 1,2,3] [--viewport WxH] [--quality N]
//                  [--live] [--context-loss] [--min-targets N] [--max-targets N]
//                  [--min-colours N]
//
// For each device pixel ratio (default 1, 2 and 3: a desktop with a mouse, a
// tablet and a phone with touch), it loads the page with ?drive=1 (the frame
// loop off; frames are drawn with __strataKit.drive(t)), and checks:
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
//     (WEBGL_lose_context) and the page draws and picks again.
// Before the scene, the loading shell (../strata-loader.js, when it is beside
// this file) is checked on a blank page: progress() clamps to 0..1, never
// moves the bar back and shows its label; slow() called twice shows one bar.
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
  const label = `DPR ${dpr} (${device.viewport.width}x${device.viewport.height}${device.hasTouch ? ', touch' : ''})`;
  console.log(`-- ${label}`);
  try {
    await page.goto(kit.withQuery(o.url, { drive: o.live ? null : 1, quality: o.quality }), { waitUntil: 'commit' });
    await kit.waitReady(page);
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
    check(errors.length === 0, `${label}: no page errors${errors.length ? ': ' + errors.slice(0, 3).join(' | ') : ''}`);
  } catch (e) {
    check(false, `${label}: ${String(e).split('\n')[0]}`);
  } finally {
    await context.close();
  }
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
    check(errors.length === 0, `loader: no page errors${errors.length ? ': ' + errors.slice(0, 3).join(' | ') : ''}`);
  } catch (e) {
    check(false, `loader: ${String(e).split('\n')[0]}`);
  } finally {
    await context.close();
  }
}

async function tapEqualsPick(page, o, label, touch) {
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
