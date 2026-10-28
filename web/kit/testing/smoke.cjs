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
//     viewport CSS pixels), a real tap or click there selects exactly what
//     a direct pick at that point returns. This is what catches a pointer
//     scaled by the drawing buffer instead of CSS pixels, which only shows
//     at a capped DPR (3 here, with the kit's default cap of 2);
//   - with --context-loss, the WebGL context is lost and restored
//     (WEBGL_lose_context) and the page draws and picks again.
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
        engineW: s.width, engineH: s.height, primitives: s.primitives };
    });
    const want = Math.min(dpr, size.cap);
    check(Math.abs(size.w - Math.round(size.cssW * want)) <= 1 && size.engineW === size.w && size.engineH === size.h,
      `${label}: drawing buffer ${size.w}x${size.h} = CSS ${size.cssW}x${size.cssH} x ${want} (cap ${size.cap}), engine agrees`);
    check(size.primitives > 0, `${label}: the frame drew ${size.primitives} primitives`);

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

async function tapEqualsPick(page, o, label, touch) {
  const targets = (await page.evaluate(() => window.__strataKit.targets()))
    .filter((t) => t && t.x > 2 && t.y > 2 && t.x < page.viewportSize().width - 2 && t.y < page.viewportSize().height - 2)
    .slice(0, o.maxTargets);
  if (!check(targets.length >= o.minTargets, `${label}: ${targets.length} target(s) on screen (need ${o.minTargets})`)) return;
  let same = 0, onTarget = 0;
  const bad = [];
  for (const t of targets) {
    const direct = await page.evaluate(([x, y]) => window.__strataKit.pickAt(x, y), [t.x, t.y]);
    await page.evaluate(() => { window.__strataKit.kit.lastTapId = '__no-tap__'; });
    if (touch) await page.touchscreen.tap(t.x, t.y);
    else await page.mouse.click(t.x, t.y);
    await page.waitForFunction(() => window.__strataKit.lastTapId !== '__no-tap__', null, { timeout: 10000 }).catch(() => {});
    const tapped = await page.evaluate(() => window.__strataKit.lastTapId);
    if (tapped === direct) same++; else bad.push(`${t.id}: tap ${tapped} != pick ${direct}`);
    if (direct === t.id) onTarget++;
  }
  check(same === targets.length, `${label}: tap == pick at ${same}/${targets.length} targets${bad.length ? ' (' + bad.slice(0, 3).join('; ') + ')' : ''}`);
  // Informational: a target can sit behind another thing, so a pick there
  // may rightly return the nearer one.
  console.log(`     ${label}: the pick at a target's own point returned it for ${onTarget}/${targets.length}`);
}

(async () => {
  const o = parseArgs(process.argv.slice(2));
  fs.mkdirSync(o.out, { recursive: true });
  const browser = await kit.launch();
  try {
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
