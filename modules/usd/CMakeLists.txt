# Strata USD module (RFC 0019 §4-5, RFC 0020 §10): the flat world model and the
# USD sync points (compose with every layer opened by Strata, hydrate, write a
# full base layer or a delta). Optional: only built with -DSTRATA_WITH_USD=ON.
#
# OpenUSD is found as a CMake package: pass -DCMAKE_PREFIX_PATH=<openusd install>
# (the prebuilt CI artifact, see docs/OPENUSD.md), not pxr_DIR.

find_package(pxr CONFIG REQUIRED)

# The flat world (tables, grid, globals, variants). No USD dependency, so games
# can build their component <-> world conversion without OpenUSD headers.
add_library(strata_world STATIC src/world.cpp)
target_include_directories(strata_world PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
strata_set_warnings(strata_world)
add_library(strata::world ALIAS strata_world)

add_library(strata_usd STATIC src/world_io.cpp)
target_include_directories(strata_usd PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
# SYSTEM: OpenUSD's own headers are not held to Strata's warning flags.
target_include_directories(strata_usd SYSTEM PUBLIC ${PXR_INCLUDE_DIRS})
target_link_libraries(strata_usd PUBLIC strata_world usd sdf tf vt gf)
# OpenUSD's tf headers pull in libstdc++'s <ext/hash_*> "backward" headers,
# whose #warning -isystem does not silence.
target_compile_options(strata_usd PUBLIC $<$<CXX_COMPILER_ID:GNU>:-Wno-deprecated>)
strata_set_warnings(strata_usd)
add_library(strata::usd ALIAS strata_usd)

if(STRATA_BUILD_TESTS)
    enable_testing()  # the top level enables testing only after the modules are added
    add_executable(strata-usd-tests tests/test_usd.cpp)
    target_link_libraries(strata-usd-tests PRIVATE strata_usd)
    target_compile_definitions(strata-usd-tests PRIVATE
        STRATA_USD_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    strata_set_warnings(strata-usd-tests)
    add_test(NAME strata-usd-tests COMMAND strata-usd-tests "${CMAKE_CURRENT_BINARY_DIR}/usd-test-out")
endif()
