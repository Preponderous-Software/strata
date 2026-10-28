// Shared pieces of the web kit's browser tools (smoke.cjs, capture.cjs).
//
// Headless Chromium draws WebGL 2 with SwiftShader, on the CPU: about one
// frame a second for a real scene. Two traps follow, and both are handled
// here once:
//   - page.goto must use waitUntil: 'commit'. A page whose frame loop starts
//     before the load event can keep 'load' from arriving in time.
//   - page.screenshot waits for the page to be stable across animation
//     frames, which a slow WebGL page may never be. Frames are taken with the
//     DevTools protocol's Page.captureScreenshot instead, which reads what is
//     on screen now (the kit's contexts keep preserveDrawingBuffer on).
// Playwright is the only dependency (NODE_PATH or a local node_modules); its
// version pins Chromium, and so SwiftShader.
'use strict';
const zlib = require('zlib');

const CHROMIUM_ARGS = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'];

function playwright() {
  try {
    return require('playwright');
  } catch (e) {
    console.error('The web kit tools need Playwright: npm install playwright (or set NODE_PATH).');
    throw e;
  }
}

async function launch(opts = {}) {
  const { chromium } = playwright();
  return chromium.launch({ args: CHROMIUM_ARGS, headless: opts.headless !== false });
}

// A browser context for one device pixel ratio. 1 is a desktop with a mouse;
// 2 and 3 are touch screens (a tablet and a phone), unless a viewport is given.
function deviceFor(dpr, viewport) {
  const touch = dpr > 1;
  const size = viewport || (dpr >= 3 ? { width: 390, height: 844 } : dpr >= 2 ? { width: 820, height: 1180 } : { width: 1280, height: 800 });
  return { viewport: size, deviceScaleFactor: dpr, hasTouch: touch, isMobile: dpr >= 3 };
}

function withQuery(url, extra) {
  const u = new URL(url);
  for (const [k, v] of Object.entries(extra)) if (v != null) u.searchParams.set(k, String(v));
  return u.toString();
}

async function waitReady(page, timeout = 180000) {
  await page.waitForFunction(() => window.__strataKit && window.__strataKit.ready === true, null, { timeout });
}

async function capture(page, cdp) {
  const session = cdp || (await page.context().newCDPSession(page));
  const { data } = await session.send('Page.captureScreenshot', { format: 'png' });
  return Buffer.from(data, 'base64');
}

// A minimal PNG decoder: 8-bit greyscale, RGB or RGBA, not interlaced, which
// is what Chromium writes. Returns {width, height, channels, pixels}.
function decodePng(buf) {
  if (buf.readUInt32BE(0) !== 0x89504e47) throw new Error('not a PNG');
  let off = 8, width = 0, height = 0, depth = 0, type = 0, interlace = 0;
  const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE(off), kind = buf.toString('latin1', off + 4, off + 8);
    const body = buf.subarray(off + 8, off + 8 + len);
    if (kind === 'IHDR') {
      width = body.readUInt32BE(0); height = body.readUInt32BE(4);
      depth = body[8]; type = body[9]; interlace = body[12];
    } else if (kind === 'IDAT') idat.push(body);
    else if (kind === 'IEND') break;
    off += 12 + len;
  }
  const channels = { 0: 1, 2: 3, 4: 2, 6: 4 }[type];
  if (depth !== 8 || !channels || interlace) throw new Error(`unsupported PNG (depth ${depth}, type ${type}, interlace ${interlace})`);
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels, out = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const f = raw[y * (stride + 1)], src = y * (stride + 1) + 1, dst = y * stride;
    for (let x = 0; x < stride; x++) {
      const a = x >= channels ? out[dst + x - channels] : 0;
      const b = y ? out[dst - stride + x] : 0;
      const c = x >= channels && y ? out[dst - stride + x - channels] : 0;
      let v = raw[src + x];
      if (f === 1) v += a;
      else if (f === 2) v += b;
      else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) {
        const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
        v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      out[dst + x] = v & 255;
    }
  }
  return { width, height, channels, pixels: out };
}

// How far a frame is from one flat colour: the count of distinct colours
// (quantised to 4 bits a channel) and the luma standard deviation.
function variety(png) {
  const img = decodePng(png), seen = new Set();
  let n = 0, sum = 0, sum2 = 0;
  for (let i = 0; i < img.pixels.length; i += img.channels * 7) {  // every 7th pixel is plenty
    const grey = img.channels < 3;
    const r = img.pixels[i], g = grey ? r : img.pixels[i + 1], b = grey ? r : img.pixels[i + 2];
    seen.add(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
    const l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    sum += l; sum2 += l * l; n++;
  }
  const mean = sum / n;
  return { colours: seen.size, lumaStd: Math.sqrt(Math.max(0, sum2 / n - mean * mean)), width: img.width, height: img.height };
}

module.exports = { CHROMIUM_ARGS, launch, deviceFor, withQuery, waitReady, capture, decodePng, variety, playwright };
