// kit_abi_floor.h: force-included (-include) into a build of the web kit's
// example scene, so the kit is compiled as it would be against the 0.7.0
// library's strata.h (ABI 1.12). docs/WEB.md promises that a vendored kit
// compiles its newer-ABI parts out; this keeps the promise checked.
//
// The current strata.h is included first (its include guard then skips the
// kit's own #include), STRATA_ABI_MINOR is lowered to 12, and every name added
// after ABI 1.12 is poisoned: any use outside an `#if STRATA_ABI_MINOR >= n`
// guard is a compile error. A name added to strata.h in a later minor belongs
// in the list below.
#ifndef STRATA_KIT_ABI_FLOOR_H
#define STRATA_KIT_ABI_FLOOR_H

#include "strata/strata.h"

#undef STRATA_ABI_MINOR
#define STRATA_ABI_MINOR 12u

// Macros are undefined first: poisoning a defined macro is itself a warning.
#undef STRATA_LINES_ROUND_CAPS
#undef STRATA_MESH_COLORS_RGBA
// ABI 1.13
#pragma GCC poison StrataWideLineVertex strata_submit_wide_lines STRATA_LINES_ROUND_CAPS
#pragma GCC poison STRATA_STRUCT_WIDE_LINE_VERTEX
// ABI 1.14
#pragma GCC poison strata_project_point strata_submit_world_text
// ABI 1.15
#pragma GCC poison STRATA_MESH_COLORS_RGBA
// ABI 1.16
#pragma GCC poison strata_set_ambient strata_set_sun_intensity

#endif  // STRATA_KIT_ABI_FLOOR_H
