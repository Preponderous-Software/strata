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
    test_headless_driver.cpp)
target_link_libraries(strata-unit-tests PRIVATE strata_static)
strata_set_warnings(strata-unit-tests)
add_test(NAME strata-unit-tests COMMAND strata-unit-tests)
