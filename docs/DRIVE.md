# Driving a Strata game: injected input, the frame clock and the drive channel

ABI 1.12 lets something other than a person at a window play a Strata game: a
test, a replay tool, or an AI playtesting harness such as
[playtester](https://github.com/Preponderous-Software/playtester). It adds three
calls and one optional environment switch. A game that uses none of them
behaves exactly as before.

| Addition | C ABI | C++ |
|---|---|---|
| Inject an event on any surface | `strata_push_event` | `Engine::pushEvent` |
| The seconds to advance this frame | `strata_frame_seconds` | `Engine::frameSeconds` |
| Was this engine driven? | `strata_is_driven` | `Engine::driven` |
| Relative mouse mode on a headless surface, when driven | `strata_set_relative_mouse_mode` | `Engine::setRelativeMouseMode` |
| The drive channel | `STRATA_DRIVE` and friends (below) | the same |

## What a game changes

Most games need two lines. Use the engine's clock for `dt`:

```cpp
while (running) {
    StrataEvent e;
    while (engine.pollEvent(e)) handle(e);
    const double dt = engine.frameSeconds();   // was: steady_clock::now() - last
    update(dt);
    draw();
    engine.present();
}
```

and keep a driven run away from the player's saves:

```cpp
if (engine.driven()) saves = false;   // a harness is playing, not the player
```

That is all the drive channel needs. Undriven, `frameSeconds` is the time since
its previous call on a monotonic clock (0 the first time), so the game plays as
it did with its own `steady_clock`.

## The drive channel

When `strata_create` runs it reads:

| Variable | Meaning |
|---|---|
| `STRATA_DRIVE` | Path of a Unix socket to **connect** to. Unset: nothing happens. |
| `STRATA_DRIVE_CLOCK` | `free` (default) or `stepped`: the clock from the first frame. |
| `STRATA_DRIVE_STEP` | Seconds per stepped frame, in (0, 1]. Default 1/60. |
| `STRATA_DRIVE_SURFACE` | `headless`: a driven game that asks for a window gets a headless surface of the same size instead (`VSYNC` and `HIDDEN_WINDOW` are ignored). `window` or unset: no change. |

The engine only ever connects. It never opens a listening socket, so a shipped
game has nothing for anyone else to reach. The harness creates the socket
(playtester puts it in the session directory, mode 0600) and the game connects
to it. If the connection fails the engine logs a warning and runs undriven; if
it drops mid-session the clock goes back to free, so a stepped game never hangs
for a harness that has gone. Linux (and WSL2) only; other platforms log once and
ignore the variable.

Everything happens on the engine's thread, inside `pollEvent`,
`frameSeconds` and `present`, where a game already expects input.

### Clocks

- **Free:** `frameSeconds` returns measured time and the game runs in real
  time. The engine reads what the harness sent at the start of each frame,
  without waiting; injected events arrive on that frame.
- **Stepped:** at the start of each frame (the first `pollEvent`, or
  `frameSeconds` if the game calls it first) the engine waits until the harness
  grants frames. `frameSeconds` then returns exactly the step. Lines are read
  only while the engine waits for a grant, and reading stops at the `step`
  line, so events sent before a `step` are delivered on its first frame and
  anything pipelined after it waits for the next one. Game time is
  `frames x step`, whatever the machine's load. Two runs with the same build,
  seed and commands draw the same frames on the same Mesa (the
  `strata-drive-e2e` test checks this byte for byte).

### Protocol, version 1

One JSON object per line in each direction (UTF-8, flat: strings, numbers,
booleans and null; no nested objects or arrays). Every reply carries `ok` and
the request's `op`. A failed request answers `{"ok":false,"op":...,"error":...}`
and does not disconnect.

| Harness sends | Fields | Engine replies |
|---|---|---|
| `hello` | `protocol: 1` | `protocol`, `strata` (version), `abi` (`"1.<STRATA_ABI_MINOR>"`, e.g. `"1.15"`), `width`, `height`, `surface`, `clock`, `step`, `frame` |
| `event` | `type` (`quit`, `key_down`, `key_up`, `mouse_motion`, `mouse_button_down`, `mouse_button_up`, `mouse_wheel`, `resized`, `text_input`), and any of `scancode`, `modifiers`, `repeat`, `button`, `width`, `height`, `x`, `y`, `dx`, `dy`, `text` (at most 31 bytes) | `queued`: the events now waiting |
| `clock` | `mode: "free"` or `"stepped"`, optional `step` (seconds) | `clock`, `step` |
| `step` | `frames` (1 to 1000000) | after the last granted frame's `present`: `frame` (frames presented so far) and `frames_run`. Stepped clock only |
| `shot` | `path` | `path`, `width`, `height`: the last presented frame as a PNG (`strata_save_screenshot_png`) |
| `stats` | | `frame`, `clock`, `step`, `queued`, `draw_calls`, `instances`, `cpu_ms_frame` |
| `bye` | | the reply, then the connection closes and the engine runs undriven with a free clock |

Keys carry USB HID usage ids in `scancode` (the values SDL3 scancodes use and
`StrataEvent.scancode` documents). While relative mouse mode is on, injected
`mouse_motion` events have `x` and `y` set to 0 and carry `dx`/`dy` only, as on
a window. A driven engine accepts `strata_set_relative_mouse_mode` on any
surface; an undriven headless one still returns `STRATA_ERROR_UNSUPPORTED`.

`shot` writes wherever it is told, as `strata_save_screenshot_png` does; a
harness that takes paths from an agent should keep them inside its own
directory.

A stepped session, by hand (`socat` plays the harness):

```bash
socat UNIX-LISTEN:/tmp/game.sock - &
STRATA_DRIVE=/tmp/game.sock STRATA_DRIVE_CLOCK=stepped STRATA_DRIVE_SURFACE=headless ./my-game &
# then type into socat:
{"op":"hello","protocol":1}
{"op":"event","type":"key_down","scancode":79}
{"op":"step","frames":30}
{"op":"shot","path":"/tmp/after-half-a-second.png"}
{"op":"bye"}
```

## Not covered

- The browser build ignores `STRATA_DRIVE` (driving a page is a job for a
  browser harness). `strata_push_event` and `strata_frame_seconds` work there.
- Game state is not part of the protocol. A harness that needs it reads the
  game's own probe files, if it has them.
- Audio, background threads and anything else that runs on wall time keep
  running on wall time. A run is only repeatable when the game's results
  depend on `frameSeconds` and its seed alone.
