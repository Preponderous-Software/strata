# Contributing

Strata serves the consumer projects listed in the [README](README.md). A
feature goes in when one of them needs it. "An engine usually has this" is not
enough.

## Before you start

- Read [ARCHITECTURE.md](ARCHITECTURE.md). Its layer rules are enforced in
  review:
  - only `src/rhi/` calls GL;
  - `src/render/` draws through `GpuDevice`;
  - nothing throws across the C ABI.
- Large features follow RFC 0020's milestones (L, T, M, U). Open an issue that
  names the milestone before writing a lot of code.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure

export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
./build/strata-golden check tests/scenes tests/golden build/golden-out

export STRATA_NATIVE_DIR=$PWD/build
dotnet test tests/csharp/Strata.Interop.Tests -c Release
STRATA_IMGCMP=$PWD/build/strata-imgcmp python3 -m unittest discover -s tests/python
```

Useful switches:

| Switch | Effect |
|---|---|
| `-DSTRATA_SANITIZE=ON` | AddressSanitizer and UBSan (GCC/Clang) |
| `-DSTRATA_WITH_SDL=OFF` | No window surface; headless only |
| `-DSTRATA_WARNINGS_AS_ERRORS=ON` | Treat warnings in Strata's own code as errors |
| `STRATA_LOG_DEBUG=1` | Log debug messages, including the pass list |

## Rules for changes

- **ABI changes.** Changing an existing struct or signature in `strata.h`
  breaks every shipped host, needs the owner's agreement, and means:
  - bumping `STRATA_ABI_VERSION`;
  - updating the layout table in `src/capi.cpp`;
  - updating the `static_assert`s in `tests/unit/test_abi.cpp`;
  - updating the C# mirror in `bindings/csharp/Strata.Interop`;
  - updating the C# size and offset assertions in
    `tests/csharp/Strata.Interop.Tests`;
  - updating the Python mirror in `bindings/python/strata/_abi.py` and its
    assertions in `tests/python/test_layout.py`.

  The C# and Python layout tests fail if one of these is missed. Adding a new
  function, struct or enum value is not a break: bump `STRATA_ABI_MINOR`
  instead, add the minor to the version list at the top of `strata.h`, mark
  each addition with it, and update the same places for any new struct. New functions go
  into the C# and Python bindings too, with the minor they arrived in. A new
  name goes into the poison list in `tests/unit/kit_abi_floor.h` as well, and
  the web kit uses it only inside an `#if STRATA_ABI_MINOR >= n` guard.
- **Golden images.** A golden changes only in a PR that explains why and shows
  the before and after images. See
  [docs/GOLDEN_IMAGES.md](docs/GOLDEN_IMAGES.md).
- **Dependencies.** New ones must be permissively licensed and pinned by hash
  in `cmake/StrataDependencies.cmake`. Add them to `THIRD_PARTY_NOTICES.md`. A
  heavy dependency goes behind an optional module (`modules/<name>/`, off by
  default).
- **Tests.** Pure logic (maths, culling, LOD selection, parsers) gets unit tests
  in `tests/unit`. Anything that changes pixels gets a golden scene.

## Pull requests

- CI must be green. That means Linux build, unit tests, golden images on the
  headless and the Xvfb surface and through the GLES 3.0 device, the C# ABI
  tests and harness, the Python binding tests, the sanitizer job, the Emscripten build with its browser
  goldens, and the Windows build.
- Keep a PR to one milestone step.

## Releases

The owner tags releases. A release-prep pull request moves the version in
`CMakeLists.txt`, `bindings/python/strata/__init__.py` and `CHANGELOG.md`, and
pins the release's own libraries in the Python binding, so that
`strata.load_release("<version>")` works with the binding from that very tag:

1. Wait for the pull request's CI. Its `package` job's step "Check the Python
   binding pins these libraries" prints the `KNOWN_RELEASES` lines and warns
   that they are missing; its `release-assets` artifact has the `SHA256SUMS`.
2. `python3 tools/known_releases.py pin <version> SHA256SUMS`, then commit
   `bindings/python/strata/_loader.py`. The binding is not part of the
   libraries, and both builds are reproducible (`/Brepro` on MSVC), so this
   does not change the hashes; the next run's step passes.
3. Merge nothing that changes the libraries between that and the tag. If
   something does, the step warns again on `main`; pin again.

On the tag, the same step runs with `--require` and fails the workflow before
the release job if an entry is missing or differs, so no release ships a
binding that cannot load it.

## Licence

By contributing you agree that your contribution is licensed under the MIT
licence in [LICENSE](LICENSE).
