#!/usr/bin/env node
// Checks the web kit's grid relay (web/kit/grid-relay) in headless Chromium
// (WebGL 2 on SwiftShader):
//
//   node tests/web/run_grid_relay.cjs <relay-build-dir> <wire-dir> <out-dir>
//
// <relay-build-dir> is build-kit.sh's output for web/kit/grid-relay/kit.conf;
// <wire-dir> holds recorded wire messages (<name>.json: a JSON array of base64
// messages, as a game's web backend sends them). For each recording the page
// plays it into the relay under ?drive=1 and saves <out-dir>/<name>.png at
// scene time 10 (a recording whose name ends in "-slide" is also drawn half
// way through its last tick, as <name>-half.png). When <wire-dir> has
// <still>.desktop.png (the same scene drawn by desktop Strata, headless on
// llvmpipe), the still must match it: at most 0.5% of pixels may differ by
// more than 24 in any channel (about strata-imgcmp's default tolerance).
// Then it checks:
//
//   - a message of another major version is refused with a console error,
//     and leaves the scene as it was;
//   - input is forwarded: keys (desktop Strata's scancodes and modifier
//     bits, text input), mouse buttons, motion and the wheel, a touch (as
//     mouse events and FINGER_* events) and a resize;
//   - frame time over two seconds of the page's own frame loop.
//
// Exit code 0 when every check passed. Needs Playwright (see run_goldens.cjs).
const fs = require('fs');
const http = require('http');
const path = require('path');
const { chromium } = require('playwright');

const [buildArg, wireArg, outDir] = process.argv.slice(2);
if (!buildArg || !wireArg || !outDir) {
  console.error('usage: run_grid_relay.cjs <relay-build-dir> <wire-dir> <out-dir>');
  process.exit(2);
}
const roots = { '/': path.resolve(buildArg), '/wire/': path.resolve(wireArg) };
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json' };
const server = http.createServer((req, res) => {
  const url = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  const [prefix, dir] = url.startsWith('/wire/') ? ['/wire/', roots['/wire/']] : ['/', roots['/']];
  const file = path.join(dir, url.slice(prefix.length));
  if (!file.startsWith(dir + path.sep) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
    res.writeHead(404);
    res.end();
    return;
  }
  res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream',
    'Content-Length': fs.statSync(file).size });
  if (req.method === 'HEAD') return res.end();
  fs.createReadStream(file).pipe(res);
});

let failures = 0;
const MAX_FRACTION = 0.005, CHANNEL = 24;

// Pixel difference between two PNGs, decoded by the browser.
async function compare(page, expectedPng, actualPng) {
  return page.evaluate(async ([a, b, channel]) => {
    const load = async (b64) => {
      const bmp = await createImageBitmap(await (await fetch('data:image/png;base64,' + b64)).blob());
      const c = new OffscreenCanvas(bmp.width, bmp.height);
      const g = c.getContext('2d');
      g.drawImage(bmp, 0, 0);
      return g.getImageData(0, 0, bmp.width, bmp.height);
    };
    const [x, y] = [await load(a), await load(b)];
    if (x.width !== y.width || x.height !== y.height) return {sameSize: false};
    let over = 0, sum = 0;
    for (let i = 0; i < x.data.length; i += 4) {
      let d = 0;
      for (let k = 0; k < 3; k++) d = Math.max(d, Math.abs(x.data[i + k] - y.data[i + k]));
      sum += d;
      if (d > channel) over++;
    }
    const n = x.width * x.height;
    return {sameSize: true, fraction: over / n, mean: sum / n};
  }, [expectedPng.toString('base64'), actualPng.toString('base64'), CHANNEL]);
}
const check = (ok, what) => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`);
  if (!ok) failures++;
};

(async () => {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  fs.mkdirSync(outDir, { recursive: true });
  const browser = await chromium.launch({
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  });
  try {
    const W = parseInt(process.env.RELAY_WIDTH || '800', 10), H = parseInt(process.env.RELAY_HEIGHT || '600', 10);
    const ready = (page) => page.waitForFunction(() => window.StrataGridRelay &&
      window.StrataGridRelay.state() === 'ready' && window.__strataKit, null, { timeout: 120000 });

    // -- stills of every recording
    const recordings = fs.readdirSync(roots['/wire/']).filter((f) => f.endsWith('.json')).sort();
    for (const file of recordings) {
      const name = file.replace(/\.json$/, '');
      const page = await browser.newPage({ viewport: { width: W, height: H }, deviceScaleFactor: 1 });
      const logs = [];
      page.on('console', (m) => logs.push(m.text()));
      await page.goto(`${base}/index.html?drive=1&wire=wire/${file}`);
      await ready(page);
      await page.waitForFunction(() => JSON.parse(window.StrataGridRelay.stats()).messages > 0, null, { timeout: 30000 });
      await page.waitForTimeout(300);  // the recording is played in one go; let the fetch finish
      const t0 = 10;
      await page.evaluate((t) => window.__strataKit.drive(t), t0);
      const stills = [[name, await page.screenshot({ path: path.join(outDir, `${name}.png`) })]];
      if (name.endsWith('-slide')) {
        await page.evaluate((t) => window.__strataKit.drive(t), t0 + 0.125);  // half of a 0.25 s tick
        stills.push([`${name}-half`, await page.screenshot({ path: path.join(outDir, `${name}-half.png`) })]);
      }
      for (const [still, png] of stills) {
        const golden = path.join(roots['/wire/'], `${still}.desktop.png`);
        if (!fs.existsSync(golden)) continue;
        const r = await compare(page, fs.readFileSync(golden), png);
        check(r.sameSize && r.fraction <= MAX_FRACTION, `${still} against the desktop: ` +
          (r.sameSize ? `${(100 * r.fraction).toFixed(3)}% of pixels over ${CHANNEL}, mean ${r.mean.toFixed(3)}`
            : 'different sizes'));
      }
      const st = JSON.parse(await page.evaluate(() => window.StrataGridRelay.stats()));
      check(st.primitives > 0 && logs.every((l) => !/refused|error/i.test(l)),
        `${name}: ${st.messages} messages, ${st.primitives} primitives, ${st.lights} lights, no errors`);
      await page.close();
    }

    // -- versioning, input and frame time on one live page
    const page = await browser.newPage({ viewport: { width: W, height: H }, deviceScaleFactor: 1, hasTouch: true });
    const logs = [];
    page.on('console', (m) => logs.push(m.text()));
    // Pinned at the top tier (the desktop's shadows), so the frame time is that tier's.
    await page.goto(`${base}/index.html?quality=2&wire=wire/${recordings[0]}`);
    await ready(page);
    await page.waitForTimeout(500);

    const before = await page.evaluate(() => JSON.parse(window.StrataGridRelay.stats()).messages);
    const rc = await page.evaluate(() => {
      // A VIEW message from a future major version 2.
      const m = new Uint8Array(8 + 64);
      m.set([0x47, 0x33, 2, 0, 4, 0, 0, 0]);
      return window.StrataGridRelay.sendBytes(m);
    });
    const after = await page.evaluate(() => JSON.parse(window.StrataGridRelay.stats()).messages);
    check(rc === -2 && after === before && logs.some((l) => /wire format 2\.0 is not readable/.test(l)),
      `a major-2 message is refused with a console error (status ${rc}, scene untouched)`);

    await page.evaluate(() => window.StrataGridRelay.poll());  // drop the start-up resize
    await page.keyboard.down('Shift');
    await page.keyboard.press('ArrowUp');
    await page.keyboard.up('Shift');
    await page.keyboard.press('KeyA');
    await page.mouse.move(400, 300);
    await page.mouse.down({ button: 'left' });
    await page.mouse.move(420, 310);
    await page.mouse.up({ button: 'left' });
    await page.mouse.click(200, 200, { button: 'right' });
    await page.mouse.wheel(0, -100);
    await page.touchscreen.tap(300, 250);
    await page.setViewportSize({ width: W - 100, height: H - 50 });
    await page.waitForTimeout(400);
    const events = JSON.parse(await page.evaluate(() => window.StrataGridRelay.poll()));
    const has = (pred) => events.some(pred);
    check(has((e) => e.type === 2 && e.scancode === 225) && has((e) => e.type === 2 && e.scancode === 82 && (e.modifiers & 1)) &&
      has((e) => e.type === 3 && e.scancode === 82), 'keys: Shift (225) then Up (82) with the shift bit, down and up');
    check(has((e) => e.type === 2 && e.scancode === 4) && has((e) => e.type === 9 && e.text === 'a'),
      'keys: A (scancode 4) and its text input');
    check(has((e) => e.type === 5 && e.button === 1 && Math.round(e.x) === 400 && Math.round(e.y) === 300) &&
      has((e) => e.type === 4 && e.dx !== 0) && has((e) => e.type === 6 && e.button === 1),
      'mouse: left button down at (400, 300), motion, up');
    check(has((e) => e.type === 5 && e.button === 3), 'mouse: right button as button 3');
    check(has((e) => e.type === 7 && e.y > 0), 'mouse: wheel up as positive y');
    check(has((e) => e.type === 16 && e.pointer === 'touch') && has((e) => e.type === 17) &&
      has((e) => e.type === 5 && e.pointer === 'touch'), 'touch: FINGER_DOWN/UP and a mouse button for the tap');
    check(has((e) => e.type === 8 && e.width === W - 100 && e.height === H - 50), 'resize: RESIZED with the CSS size');
    fs.writeFileSync(path.join(outDir, 'events.json'), JSON.stringify(events, null, 1));

    await page.setViewportSize({ width: W, height: H });
    await page.waitForTimeout(500);
    await page.evaluate(() => window.StrataGridRelay.resetStats());
    const f0 = await page.evaluate(() => window.__strataKit.stats().frames);
    const ms0 = Date.now();
    await page.waitForTimeout(2000);
    const f1 = await page.evaluate(() => window.__strataKit.stats().frames);
    const st = JSON.parse(await page.evaluate(() => window.StrataGridRelay.stats()));
    const fps = (f1 - f0) / ((Date.now() - ms0) / 1000);
    console.log(`frame loop: ${fps.toFixed(1)} fps, relay + kit CPU ${st.cpuMs.toFixed(2)} ms a frame, ` +
      `quality ${st.quality}, ${st.width}x${st.height}`);
    fs.writeFileSync(path.join(outDir, 'timing.json'), JSON.stringify({ fps, cpuMs: st.cpuMs, quality: st.quality }));
    check(f1 > f0, 'the frame loop runs');
  } finally {
    await browser.close();
    server.close();
  }
  console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
