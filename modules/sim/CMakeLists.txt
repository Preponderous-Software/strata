# Strata simulation module: deterministic CPU-side building blocks for grid
# simulations -- a seeded RNG, an entity-component registry with a fixed
# iteration order, 2D scalar fields with diffusion and decay, a fixed-timestep
# accumulator, a state fingerprint and field colormaps. No GL, no
# dependencies; C++ only (it is not part of the C ABI). Built when
# STRATA_WITH_SIM is ON (the default).

add_library(strata_sim STATIC src/field.cpp)
target_include_directories(strata_sim PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(strata_sim PUBLIC cxx_std_17)
# Determinism: the same inputs must give bit-identical fields on every
# compiler, so never fuse multiply-adds into FMAs.
if(NOT MSVC)
    target_compile_options(strata_sim PRIVATE -ffp-contract=off)
else()
    target_compile_options(strata_sim PRIVATE /fp:precise)
endif()
strata_set_warnings(strata_sim)
add_library(strata::sim ALIAS strata_sim)

if(STRATA_BUILD_TESTS)
    enable_testing()  # the top level enables testing only after the modules are added
    add_executable(strata-sim-tests
        ${PROJECT_SOURCE_DIR}/tests/unit/main.cpp
        tests/test_sim.cpp)
    target_include_directories(strata-sim-tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/unit)
    target_link_libraries(strata-sim-tests PRIVATE strata_sim)
    if(NOT MSVC)
        target_compile_options(strata-sim-tests PRIVATE -ffp-contract=off)
    endif()
    strata_set_warnings(strata-sim-tests)
    add_test(NAME strata-sim-tests COMMAND strata-sim-tests)
endif()
