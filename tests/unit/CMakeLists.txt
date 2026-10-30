add_executable(strata-unit-tests
    main.cpp
    test_abi.cpp
    test_drive.cpp
    test_log.cpp
    test_primitive_mesh.cpp
    test_cascades.cpp
    test_sun.cpp
    test_lights.cpp
    test_lighting_validation.cpp
    test_terrain_mesh.cpp
    test_static_mesh.cpp
    test_ray_pick.cpp
    test_depth_readback.cpp
    test_pass_list.cpp
    test_overlay.cpp
    test_image_compare.cpp
    test_image_png.cpp
    test_glsl_es.cpp
    test_headless_driver.cpp
    test_kit_lights.cpp)
target_link_libraries(strata-unit-tests PRIVATE strata_static)
# test_kit_lights.cpp includes the web kit's header (without STRATA_KIT_MAIN).
target_include_directories(strata-unit-tests PRIVATE ${PROJECT_SOURCE_DIR}/web/kit)
strata_set_warnings(strata-unit-tests)
add_test(NAME strata-unit-tests COMMAND strata-unit-tests)

# Compile-only: the web kit's example scene built as against the 0.7.0
# library's strata.h (ABI 1.12), with every newer name poisoned, so a kit call
# that needs a newer ABI without an STRATA_ABI_MINOR guard fails the build.
# See kit_abi_floor.h. `#pragma GCC poison` is GCC and Clang only.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_library(strata-kit-abi-floor OBJECT ${PROJECT_SOURCE_DIR}/web/kit/example/main.cpp)
    target_include_directories(strata-kit-abi-floor PRIVATE ${PROJECT_SOURCE_DIR}/include
                                                            ${PROJECT_SOURCE_DIR}/web/kit)
    target_compile_options(strata-kit-abi-floor PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/kit_abi_floor.h)
    strata_set_warnings(strata-kit-abi-floor)
endif()
