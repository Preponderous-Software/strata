#!/usr/bin/env node
// Runs strata-golden in headless Chromium (WebGL 2 on SwiftShader) and copies
// what it wrote back to disk.
//
//   node tests/web/run_goldens.cjs <build-dir> <out-dir> [strata-golden check options...]
//
// <build-dir> holds strata-golden.html/.js/.wasm/.data from an Emscripten
// build. The scenes and goldens are those packaged into strata-golden.data
// (tests/scenes and tests/golden at build time). Exit code: strata-golden's.
// With --bless, the new web goldens are written to <out-dir>/golden/.
// STRATA_WEB_ENV="NAME=VALUE ..." sets environment variables inside the page.
//
// Needs Playwright (npm install playwright@1.61.1; npx playwright install chromium).
const fs = require('fs');
const http = require('http');
const path = require('path');
const { chromium } = require('playwright');

const [buildArg, outDir, ...options] = process.argv.slice(2);
if (!buildArg || !outDir) {
  console.error('usage: run_goldens.cjs <build-dir> <out-dir> [check options...]');
  process.exit(2);
}
const buildDir = path.resolve(buildArg);

const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };
const server = http.createServer((req, res) => {
  const file = path.join(buildDir, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  if (!file.startsWith(buildDir + path.sep) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
    res.writeHead(404);
    res.end();
    return;
  }
  res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream' });
  fs.createReadStream(file).pipe(res);
});

(async () => {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  // SwiftShader: a software WebGL 2 that every CI machine has, the browser's
  // counterpart of llvmpipe.
  const browser = await chromium.launch({
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  });
  let code = 1;
  try {
    const page = await browser.newPage();
    page.on('console', (m) => console.log(m.text()));
    page.on('pageerror', (e) => console.log('page error: ' + e.message));
    page.on('crash', () => console.log('the page crashed'));
    const args = ['check', '/scenes', '/golden', '/out', ...options];
    // STRATA_WEB_ENV="A=1 B=2": environment variables for strata-golden.
    const env = (process.env.STRATA_WEB_ENV || '').split(/\s+/).filter(Boolean);
    const url = `http://127.0.0.1:${port}/strata-golden.html?` +
      [...args.map((a) => 'arg=' + encodeURIComponent(a)), ...env.map((e) => 'env=' + encodeURIComponent(e))].join('&');
    await page.goto(url, { waitUntil: 'commit' });
    console.log('page opened: ' + url);
    await page.waitForFunction(() => window.strataExit !== undefined, null, { timeout: 15 * 60 * 1000, polling: 500 });
    const exit = await page.evaluate(() => window.strataExit);
    const write = (files, from, to) => {
      for (const [file, base64] of files) {
        const dest = path.join(to, path.relative(from, file));
        fs.mkdirSync(path.dirname(dest), { recursive: true });
        fs.writeFileSync(dest, Buffer.from(base64, 'base64'));
      }
    };
    write(await page.evaluate(() => window.strataReadPngs('/out')), '/out', outDir);
    if (options.includes('--bless')) {
      const at = options.indexOf('--platform');
      const platform = at >= 0 ? options[at + 1] : 'linux-llvmpipe';
      const blessed = (await page.evaluate(() => window.strataReadPngs('/golden'))).filter(([file]) =>
        file.endsWith('/' + platform + '.png'));
      write(blessed, '/golden', path.join(outDir, 'golden'));
    }
    console.log(`strata-golden exited with ${exit}`);
    code = typeof exit === 'number' ? exit : 1;
  } finally {
    await browser.close();
    server.close();
  }
  process.exit(code);
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
