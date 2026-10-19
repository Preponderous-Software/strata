// Captures the browser demo (strata-village-day.html) through a day by
// scrubbing its clock: page.png plus frameNN.png, for tools/make_web_media.py.
//
//   node tools/capture_web_demo.cjs <emscripten-build-dir> <out-dir>
//
// Needs Playwright (npm install playwright@1.61.1; npx playwright install chromium).
const fs = require('fs'), http = require('http'), path = require('path');
const { chromium } = require('playwright');
const dir = path.resolve(process.argv[2]), outDir = process.argv[3];
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const server = http.createServer((req, res) => {
  const f = path.join(dir, new URL(req.url, 'http://x').pathname);
  if (!fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); return res.end(); }
  res.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream' }); fs.createReadStream(f).pipe(res);
});
(async () => {
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const b = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const p = await b.newPage({ viewport: { width: 1024, height: 720 }, deviceScaleFactor: 1 });
  p.on('console', (m) => console.log('console: ' + m.text()));
  await p.goto(`http://127.0.0.1:${server.address().port}/strata-village-day.html`, { waitUntil: 'commit' });
  await p.waitForFunction(() => window.strataReady === true, null, { timeout: 120000 });
  await p.waitForTimeout(1500);
  await p.keyboard.press('Space');  // pause at about 07:20
  for (let i = 0; i < 10; ++i) await p.keyboard.press('ArrowLeft');
  fs.mkdirSync(outDir, { recursive: true });
  await p.waitForTimeout(1500);
  await p.screenshot({ path: path.join(outDir, 'page.png') });
  const frames = Number(process.env.FRAMES || 39);
  for (let i = 0; i < frames; ++i) {
    await p.waitForTimeout(700);
    await p.screenshot({ path: path.join(outDir, `frame${String(i).padStart(2, '0')}.png`) });
    await p.keyboard.press('ArrowRight');
    await p.keyboard.press('ArrowRight');
  }
  await b.close(); server.close();
})().catch((e) => { console.error(e); process.exit(1); });
