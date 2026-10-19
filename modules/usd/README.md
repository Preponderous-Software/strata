# Strata USD modules

Optional engine modules for games whose world, save and mod format is OpenUSD
(RFC 0019, RFC 0020 §10). They are built only with `-DSTRATA_WITH_USD=ON`;
nothing else in Strata depends on them.

| Target | Directory | What it is |
|---|---|---|
| `strata::world` | `modules/usd` | The flat world: structure-of-arrays entity tables, a chunked int grid, int64 globals, variant selections. No USD dependency. |
| `strata::usd` | `modules/usd` | Sync points: `compose` (every layer opened by Strata; a missing or unreadable one is an error), `hydrate`, `writeFull` (base), `writeDelta` (save) and the save manifest. |
| `strata::save` | `modules/save` | The save core: content-addressed saves behind an atomic `HEAD`, SHA-256 before parsing, verify-before-commit, fallback, migrations and asynchronous saves. See `modules/save/include/strata/save/slot.h`. |

The engine never names a game's data: tables, fields, channels and variant sets
come from the game's schema, in the game's own USD namespace (`colony:` for
steading).

## Save safety

The save protocol and every guarantee it makes are documented at the top of
[`slot.h`](../save/include/strata/save/slot.h). Each guarantee has a test in
[`test_save.cpp`](../save/tests/test_save.cpp). The suite runs against a toy game,
so the engine is tested without any real game's code:

- exact round trips, including `-0.0`, NaN payloads, infinities, denormals and
  int64 extremes, and byte-identical files for identical state;
- a crash at each of the five protocol steps, deleted temp files, the previous
  save loaded;
- verify-before-commit refusing a truncated temp file and a valid-but-wrong one;
- damaged, truncated, emptied and deleted saves, a damaged or deleted `HEAD`
  and a damaged base: each is detected, and only integrity failures fall back;
- USD's own behaviour (a truncated sublayer is skipped with a warning and the
  bare base world loads) next to Strata's refusal;
- mod order, mod-list mismatch (refused, or forced with a warning), changed mod
  content (a warning), another game, a newer schema or format (refused);
- migration of an older schema on a copy, leaving the original files
  byte-identical;
- asynchronous saves (main-thread snapshot only), single-writer locking and
  pruning;
- `--crash`: a single-bit flip at about 400 offsets, each loaded in a child
  process capped at 4 GB of address space, and 25 rounds of `kill -9` during a
  save loop, after each of which the slot must load and replay exactly.

Never run corruption probes without the memory cap: a flipped size field can
make the crate reader try to allocate about 150 GB.

On Windows the fast suite runs unchanged; `--crash` needs fork and
`setrlimit`, so it prints that it was skipped. The Windows backend
(`modules/save/src/fs_win32.cpp`) renames with
`MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`, so there is
no separate directory flush. Windows cannot rename or delete a file that is
still open or memory-mapped, which is why every stage over a save file is
released before the file is committed or pruned.

## OpenUSD

OpenUSD is pinned in [`openusd/openusd.lock`](openusd/openusd.lock) (tag, commit,
the minimal `build_usd.py` flags from RFC 0019 §11, and the toolchain image). CI
never builds it in a normal run. The `openusd` job in
`.github/workflows/usd-modules.yml` computes a key from the lock file and the
build script, then takes the prebuilt install from the Actions cache, or else
from this repository's release asset of the same name. Only a new key builds
OpenUSD, once, and publishes it as a pre-release asset named after the key.
Games' CI (for example steading's) downloads the same asset.

Windows has its own key and asset: `key.sh windows` hashes the lock and
`build-openusd-windows.sh` (MSVC 14.3 on `windows-2022`, run from Git Bash after
`vcvars64.bat`), and the `openusd-windows` job fetches or builds it the same way.
At run time OpenUSD finds its plugins relative to its own DLLs
(`lib/usd/plugInfo.json`), so put `<prefix>\lib` and `<prefix>\bin` on `PATH`;
do not copy the DLLs beside the executable.

To build locally against your own OpenUSD install:

```sh
cmake -S . -B build -DSTRATA_WITH_USD=ON -DSTRATA_WITH_SDL=OFF -DSTRATA_WITH_EGL=OFF \
  -DCMAKE_PREFIX_PATH=/path/to/openusd
cmake --build build --target strata-usd-tests strata-save-tests strata-save-bench
ctest --test-dir build -R 'strata-(usd|save)'
```

`modules/usd/openusd/build-openusd.sh <out> [jobs]` reproduces the CI build.
OpenUSD v26.08 needs CMake 3.27 or newer. GCC 9 needs `-Wmismatched-tags`
removed from OpenUSD's compiler defaults; GCC 11 or newer does not.

OpenUSD is under the Tomorrow Open Source Technology License 1.0 and oneTBB
under Apache-2.0. The prebuilt carries both licences in `share/licenses/`.

## Benchmark

`strata-save-bench <dir> <agents> <grid> <ticks> [reps]` prints one JSON line.
CI runs it at 1,000 and 10,000 agents and `modules/save/bench/gate.py` fails
the job when a RFC 0019 §9 budget is exceeded or a metric regresses more than
20% against `modules/save/bench/baseline.json`.
