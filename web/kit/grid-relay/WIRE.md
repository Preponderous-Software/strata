# The grid relay

A web-kit scene for games whose code runs somewhere in the page that cannot
call Strata itself: Python under [pygbag](https://pypi.org/project/pygbag/),
or any script. The game describes a 3D grid scene (a board of tiles, pieces
standing on cells, a HUD) and hands it to the relay as small binary
messages; the relay draws it with the same camera, sun, shadows, clear colour,
screen panels and screen text a desktop Strata view draws for the same
scene, and hands the player's input back. It knows nothing about any game.

| File | What it is |
|---|---|
| `relay.h` | The wire format reader and the scene state: checks each message, keeps the board, pieces, HUD and view, and builds each frame's primitives, lights, panels and text. Pure C++17 on `strata.h`'s structs (no GL), so `tests/unit` checks it natively |
| `main.cpp` | The kit scene around it (`strata_kit.h`): desktop lighting (four 2048 cascades, shadows to 40 units, no fog, no MSAA), the view's camera, a flat clear colour. Exports `gr_submit`, `gr_wire_version`, `gr_set_ui_scale`, `gr_orbit`, `gr_messages`, `gr_has_board` |
| `grid-relay.js` | The page side (`window.StrataGridRelay`): loading on request under a size gate, base64 messages in, input events out, all as strings and numbers |
| `index.html` | A test page: plays a recorded game (`?wire=<url>`) into the relay |
| `kit.conf` | `build-kit.sh` settings |

Build it like any kit scene:

```sh
KIT_OUT=out STRATA_LIB=build-web/libstrata.a STRATA_INCLUDE=include web/kit/build-kit.sh web/kit/grid-relay/kit.conf
```

`out/` then holds `grid-relay-scene.js`, `grid-relay-scene.wasm` (about 1 MB,
0.33 MB gzipped, plus 46 KB of gzipped glue), `grid-relay.js`, `strata-kit.js`, `strata-loader.js` and
`index.html`. A game's page loads the three scripts and has a canvas for the
relay:

```html
<canvas id="relay" hidden style="position:fixed;inset:0;width:100vw;height:100vh;touch-action:none"></canvas>
<script src="strata-loader.js"></script>
<script src="strata-kit.js"></script>
<script src="grid-relay.js"></script>
```

## The page API

Every call takes and returns strings and numbers, so a bridge that passes
nothing richer (pygbag's `platform.window`) can drive it.

| Call | What it does |
|---|---|
| `StrataGridRelay.request(json)` | Starts loading the 3D view and returns at once. Options: `canvas` (id, default `relay`), `maxBytes` (the size gate, default 4 MiB: the `.wasm` and glue, measured with `HEAD` before any download), `base`, `title`, `quality`, `reducedMotion`, `slowMs` (the frame time over which the kit lowers its quality, default 40: 25 frames a second, so a page held to 30 by a battery saver keeps its quality), `gate` (`StrataLoader`'s data-saver prompt), `keys` (default `true`; `false` for a page with its own keyboard UI: see "Input"). Nothing is downloaded before it is called. `StrataGridRelay.open(options)` takes the same options as an object, for a page in JavaScript |
| `StrataGridRelay.state()` | `idle`, `loading`, `ready` or `failed:<reason>` (over the size gate, no WebGL 2, the engine failed, a lost context that did not come back). A game keeps its 2D view on `failed:`. The frame loop starts with the first message the module accepts, not at `ready`: until then there is nothing to draw, and the game in the page is usually still starting on the same thread |
| `StrataGridRelay.send(base64)` | One message. Returns 0, 1 when queued (the module is still loading; the latest message of each kind is kept, as each replaces the last of its kind) or a negative status (below) |
| `StrataGridRelay.sendBytes(u8)` | The same, from a `Uint8Array` |
| `StrataGridRelay.poll()` | The input since the last poll, as a JSON array (below) |
| `StrataGridRelay.reducedMotion()` | 1 when the player asked for reduced motion |
| `StrataGridRelay.stats()` | JSON: what the last frame drew, messages taken, frame times |
| `StrataGridRelay.close()` | Stops drawing and hides the canvas; `request` shows it again. A `close` while the module loads is kept: it finishes loading hidden and `idle` |

The relay owns its camera: the kit's drag-to-orbit and arrow keys never move
it, because the game's controls get every pointer and key instead.

## Wire format 1.0

Every message is one little-endian byte string: an 8-byte header and a body.

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | magic `G3` (0x47 0x33) |
| 2 | 1 | major version: 1 |
| 3 | 1 | minor version: 0 |
| 4 | 1 | kind: 1 BOARD, 2 PIECES, 3 HUD, 4 VIEW |
| 5 | 1 | flags: 0 |
| 6 | 2 | reserved: 0 |

**Versioning.** A message whose major is not 1 is refused (status -2), and
`grid-relay.js` says so in the console in words (`wire format 2.0 is not
readable by this relay (reads 1.x); rebuild the game and the relay from the
same release`). A later minor may only append fields at the end of a message
body; a 1.0 reader reads a 1.x message and ignores what follows the fields
it knows. Record sizes are fixed within a major.

**Errors.** `send` returns 0 or one of: -1 not a relay message (fewer than
two bytes, or no magic), -2 another major version, -3 truncated (including a
header cut short after its magic and major), -4 unknown kind, -5 a bad value,
-6 over the limits (65536 shapes, 32768 anchors, 32 lights or 4 shadow-casting
lights in one message, 4096 panels or labels). A refused message changes
nothing. A bad value is any of:

- a shape kind over 3, an attachment or anchor index out of range;
- a non-finite number anywhere;
- a world coordinate, offset, anchor, size or light radius beyond ±10⁶ (so
  every sum the relay makes stays finite);
- a light the engine would refuse: a radius of 0 or less, a negative
  intensity or colour component;
- a panel or label colour the engine would refuse: a negative component, or
  alpha over 1;
- a view whose field of view is not between 0 and 180 degrees, whose planes
  are not 0 < near < far, or whose eye is on its target.

### Records

**Shape**, 48 bytes: one of Strata's primitives, axis-aligned.

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | kind: `STRATA_PRIMITIVE_*` (0 cube, 1 capsule, 2 cylinder, 3 sphere) |
| 1 | u8 | attach: 0 world, 1 piece, 2 bridge |
| 2 | u16 | anchor a |
| 4 | u16 | anchor b (bridge only) |
| 6 | u16 | reserved |
| 8 | f32 × 3 | position |
| 20 | f32 × 3 | scale (size along x, y, z) |
| 32 | f32 × 4 | colour, linear RGBA |

`attach` says where the position is measured from. **World**: it is the
position. **Piece**: x and z are offsets from anchor `a`'s position this frame
(y is absolute). **Bridge**: x and z are offsets from the midpoint of anchors
`a` and `b`, and the x and z scale grow to at least the distance between the
two anchors along that axis, so a joint between two sliding segments of a
chain stretches with them.

**Light**, 36 bytes: a point light.

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | attach: 0 world, 1 piece |
| 1 | u8 | flags: bit 0 casts shadows (`STRATA_POINT_LIGHT_CAST_SHADOWS`) |
| 2 | u16 | anchor a |
| 4 | f32 × 3 | position (x, z offsets for a piece light) |
| 16 | f32 × 3 | colour, linear RGB |
| 28 | f32 | radius (`range`) |
| 32 | f32 | intensity |

**Anchor**, 16 bytes: where a piece slides from and to over one tick:
`f32 startX, startZ, endX, endZ`.

**Panel**, 32 bytes: `f32 x, y, width, height` in CSS pixels from the
canvas's top-left, then `f32 × 4` linear RGBA. Drawn under every label.

**Label**, 32 bytes plus its text: `f32 x, y, pixelSize` (CSS pixels; the
top-left of the first glyph of Strata's 8 × 8 font), `f32 × 4` linear RGBA,
`u16` the text's byte length, `u16` reserved, then the UTF-8 text padded with
zeros to a multiple of 4 bytes. No NUL inside the text.

### Messages

| Kind | Body | When |
|---|---|---|
| 1 BOARD | `u32 shapes, u32 lights`, then the shapes and lights (world only) | Once; again when the board, its theme or its tint changes |
| 2 PIECES | `f32 tickSeconds, u32 anchors, u32 shapes, u32 lights`, then the anchors, shapes, lights | Once per game tick |
| 3 HUD | `u32 panels, u32 labels`, then the panels and labels | When the HUD changes |
| 4 VIEW | `f32 × 3 eye, f32 × 3 target, f32 fovDegrees, f32 near, f32 far, f32 × 4 clear colour, f32 × 3 direction to the sun` (64 bytes) | With the board, and when the window's aspect changes |

Each message replaces what the last one of its kind said. The board's
shapes are drawn first, then the pieces' in their order; the board's lights
come before the pieces'. Each message keeps to the engine's limit of four
shadow-casting lights, and a frame does too: when the board's and the pieces'
together have more, the first four (board first) cast shadows and the rest
still shine without them.

**Between ticks.** A PIECES message starts its tick at the first frame drawn
after it arrives. Each anchor moves from its start to its end over
`tickSeconds` and then stays; a tick of 0 places it at its end at once, as
does reduced motion (`prefers-reduced-motion`, which the kit reads). Whether
a piece slides at all (a new piece, a teleport, a wrap-around jump) is the
game's choice, made when it sets the anchor's start: the relay only
interpolates. Nothing is sent per display frame.

**The HUD at any pixel density.** Panels and labels are in CSS pixels and
are scaled to the drawing buffer by the device pixel ratio the kit settled
on, so a HUD laid out for the canvas's CSS size lands in the same place at
DPR 1, 2 or 3. A panel or label so far off screen that scaling takes it past
a float's range is left out of the frame.

### Why binary

A board of 14 × 10 cells is about 150 shapes: 6.9 KB as a BOARD message, and
a nine-piece tick 1.1 KB; as JSON (Python's `json.dumps` of the same
shapes) they are 15.4 KB and 2.5 KB. Parsing JSON would also mean building
an object per shape in the page's JavaScript and copying it again into the
module; the binary form is copied into the
module's heap as it is and read in place by `relay.h`, with every count and
index checked before anything is kept. Base64 carries it through bridges
that pass only strings (pygbag's): it adds a third and never puts a NUL in a
string. Under pygbag a tick costs Python about 1 ms to build, encode and
send.

## Input

`poll()` returns what the player did since the last poll, as objects with
the fields of `strata.h`'s `StrataEvent`, so a game reads the same events on
the web as from desktop Strata's `strata_poll_event`:

| type | Event | Fields |
|---|---|---|
| 2, 3 | `STRATA_EVENT_KEY_DOWN`, `KEY_UP` | `scancode` (USB HID usage id, desktop Strata's and SDL3's), `modifiers` (SDL3 `SDL_Keymod` bits, left and right apart), `repeat`; also the browser's `key` and `code` |
| 9 | `STRATA_EVENT_TEXT_INPUT` | `text` |
| 4 | `STRATA_EVENT_MOUSE_MOTION` | `x`, `y` (CSS pixels from the canvas's top-left), `dx`, `dy` |
| 5, 6 | `STRATA_EVENT_MOUSE_BUTTON_DOWN`, `UP` | `button` (1 left, 2 middle, 3 right, 4 and 5 extra), `x`, `y` |
| 7 | `STRATA_EVENT_MOUSE_WHEEL` | `x`, `y` (positive `y` scrolls up, as SDL) |
| 8 | `STRATA_EVENT_RESIZED` | `width`, `height`: the canvas's CSS size, which the HUD is laid out in |
| 16, 17, 18, 19 | web only: `FINGER_DOWN`, `FINGER_UP`, `FINGER_MOTION`, `FINGER_CANCELED` | `id`, `x`, `y`, `nx`, `ny` (0..1 across the canvas), `dx`, `dy` (0..1), `pressure` |

Every pointer event also carries `pointer` (`mouse`, `touch` or `pen`) and
`id`. Touch is first-class: a touch or pen's primary pointer also produces
mouse events, as SDL turns touch into mouse by default, so a game whose 2D
controls are taps and drags works on a phone unchanged, and every finger
also produces `FINGER_*` events for swipes and pinches. The relay prevents
the browser's own use of arrows, space, tab and the page-scrolling keys
while it is showing.

**A page with its own keyboard UI** (a page in React or plain DOM whose
buttons and focus order the player uses with Tab, Space and Enter, and which
sends the relay its messages itself) opts out with `keys: false`:

```js
StrataGridRelay.request('{"keys": false}');   // or StrataGridRelay.open({keys: false})
```

Then no key is forwarded (`poll()` has no key or text events), no key has
its browser default prevented, on the canvas or anywhere else, and the
kit's own camera keys are off too. The pointer, wheel, touch and resize
events are forwarded as before. The default, `keys: true`, is unchanged.

## Checking it

`tests/unit/test_grid_relay.cpp` checks the reader natively: version and
error handling, sliding, bridges, reduced motion, the HUD's scaling and the
view's camera; that everything it accepts passes the engine's own checks on
lights, panels and text; malformed input (every truncation, byte flips, wild
counts, noise), which the Sanitizers job runs under ASan and UBSan; and
golden messages encoded by graphik's grid3d (`grid_relay_wire_1_0.inc`, the
bytes graphik's own tests hold its encoder to), read back as their scene.
`tests/web/grid_relay_js.cjs` checks `grid-relay.js`'s page logic in Node
with the module stubbed (closing while loading, the queue, `keys: false`). `tests/web/run_grid_relay.cjs` builds nothing: given the
relay's build and a directory of recorded games (`tests/web/grid-relay/`),
it draws each recording in headless Chromium (WebGL 2 on SwiftShader) and
compares it with the same scene drawn by desktop Strata (`<still>.desktop.png`,
headless on llvmpipe), then checks versioning, every kind of input and the
frame loop:

```sh
node tests/web/run_grid_relay.cjs out tests/web/grid-relay golden-out/grid-relay
```

The recordings in `tests/web/grid-relay/` were made with graphik's grid3d
scene description (a 14 × 10 board, a six-segment snake with eyes and
chain joints, glowing food, two rocks and a HUD), sent through a web backend
that writes this format; the `.desktop.png` stills are the same scene drawn
by graphik's desktop `Grid3DView` (headless) on this Strata. Re-record both
when the format or the engine's output changes.
