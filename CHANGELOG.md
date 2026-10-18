# Changelog

## 0.1.0

The skeleton milestone and lighting milestones L1–L2 from RFC 0020.

- A versioned C ABI (`strata.h`, ABI 1) with run-time struct layout queries,
  and a C++ API (`strata.hpp`).
- Surfaces: an SDL3 window, an EGL surfaceless headless surface, and the host's
  current GL context.
- `GpuDevice`, a thin interface with an OpenGL 4.5 core (DSA) backend.
- Beyond Nations' four primitives, instanced, with BN's exact legacy lighting.
- A sun and moon driven by time of day, four stabilised cascaded shadow maps,
  hemisphere ambient, fog and an analytic sky.
- `strata-golden` and `strata-imgcmp`, with ΔE-based golden-image comparison.
- A C# P/Invoke binding, ABI layout tests, and a C# harness that draws BN-shaped
  structs.
