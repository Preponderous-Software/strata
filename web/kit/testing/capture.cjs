// Frame capture for a page built on the Strata web kit, in headless Chromium.
//
//   node capture.cjs URL OUT_DIR [--frames N] [--fps N] [--start T]
//                    [--dpr N] [--viewport WxH] [--orbit RAD_PER_S] [--gif] [--mp4]
//
// Loads the page with ?drive=1 and steps scene time with __strataKit.drive(t)
// (t = start + i / fps), saving each frame as OUT_DIR/frame-0000.png and so
// on, through the DevTools protocol (lib.cjs says why). Every frame is a
// function of t, so a capture repeats. --orbit turns the camera at that rate
// from where the page put it. --gif and --mp4 assemble the frames with
// ffmpeg, when it is installed.
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const kit = require('./lib.cjs');

(async () => {
  const [url, out, ...rest] = process.argv.slice(2);
  if (!url || !out) {
    console.error('usage: node capture.cjs URL OUT_DIR [--frames N] [--fps N] [--start T] [--dpr N] [--viewport WxH] [--orbit R] [--gif] [--mp4]');
    process.exit(2);
  }
  const o = { frames: 30, fps: 15, start: 0, dpr: 1, viewport: { width: 960, height: 540 }, orbit: 0, gif: false, mp4: false };
  for (let i = 0; i < rest.length; i++) {
    const a = rest[i], next = () => rest[++i];
    if (a === '--frames') o.frames = Number(next());
    else if (a === '--fps') o.fps = Number(next());
    else if (a === '--start') o.start = Number(next());
    else if (a === '--dpr') o.dpr = Number(next());
    else if (a === '--viewport') { const [w, h] = next().split('x').map(Number); o.viewport = { width: w, height: h }; }
    else if (a === '--orbit') o.orbit = Number(next());
    else if (a === '--gif') o.gif = true;
    else if (a === '--mp4') o.mp4 = true;
    else throw new Error('unknown option ' + a);
  }
  fs.mkdirSync(out, { recursive: true });
  const browser = await kit.launch();
  const context = await browser.newContext({ viewport: o.viewport, deviceScaleFactor: o.dpr });
  const page = await context.newPage();
  page.on('pageerror', (e) => console.error('page error: ' + e));
  await page.goto(kit.withQuery(url, { drive: 1 }), { waitUntil: 'commit' });
  await kit.waitReady(page);
  const cdp = await context.newCDPSession(page);
  const yaw0 = await page.evaluate(() => window.__strataKit.cam.yaw);
  await page.evaluate((t) => window.__strataKit.drive(t), o.start);  // shader compiles
  for (let i = 0; i < o.frames; i++) {
    const t = o.start + i / o.fps;
    await page.evaluate(([tt, yaw]) => window.__strataKit.drive(tt, yaw == null ? {} : { yaw }),
      [t, o.orbit ? yaw0 + o.orbit * (t - o.start) : null]);
    fs.writeFileSync(path.join(out, `frame-${String(i).padStart(4, '0')}.png`), await kit.capture(page, cdp));
    process.stdout.write(`\rframe ${i + 1}/${o.frames}`);
  }
  process.stdout.write('\n');
  await browser.close();
  const pattern = path.join(out, 'frame-%04d.png');
  const ffmpeg = (args) => execFileSync('ffmpeg', ['-y', '-loglevel', 'error', ...args], { stdio: 'inherit' });
  try {
    if (o.gif) {
      ffmpeg(['-framerate', String(o.fps), '-i', pattern, '-vf',
        'split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4', path.join(out, 'capture.gif')]);
      console.log('wrote ' + path.join(out, 'capture.gif'));
    }
    if (o.mp4) {
      ffmpeg(['-framerate', String(o.fps), '-i', pattern, '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
        '-vf', 'pad=ceil(iw/2)*2:ceil(ih/2)*2', path.join(out, 'capture.mp4')]);
      console.log('wrote ' + path.join(out, 'capture.mp4'));
    }
  } catch (e) {
    console.error('ffmpeg failed or is not installed; the PNG frames are in ' + out);
    process.exit(1);
  }
})().catch((e) => { console.error(e); process.exit(1); });
