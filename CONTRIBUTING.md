# Contributing

Strata serves two games: Beyond Nations and the colony sim. A feature goes in
when one of them needs it. "An engine usually has this" is not enough.

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
```

Useful switches:

| Switch | Effect |
|---|---|
| `-DSTRATA_SANITIZE=ON` | AddressSanitizer and UBSan (GCC/Clang) |
| `-DSTRATA_WITH_SDL=OFF` | No window surface; headless only |
| `-DSTRATA_WARNINGS_AS_ERRORS=ON` | Treat warnings in Strata's own code as errors |
| `STRATA_LOG_DEBUG=1` | Log debug messages, including the pass list |

## Rules for changes

- **ABI changes.** Changing a struct or a signature in `strata.h` means:
  - bumping `STRATA_ABI_VERSION`;
  - updating the layout table in `src/capi.cpp`;
  - updating the `static_assert`s in `tests/unit/test_abi.cpp`;
  - updating the C# mirror in `bindings/csharp/Strata.Interop`.

  The C# layout tests fail if one of these is missed. Adding a new function is
  not a break.
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

- CI must be green. That means Linux build, unit tests, golden images on both
  the headless and the Xvfb surface, the C# ABI tests and harness, the sanitizer
  job, and the Windows build.
- Keep a PR to one milestone step.

## Licence

By contributing you agree that your contribution is licensed under the MIT
licence in [LICENSE](LICENSE).
