add_executable(strata-unit-tests
    main.cpp
    test_abi.cpp
    test_primitive_mesh.cpp
    test_cascades.cpp
    test_sun.cpp
    test_lights.cpp
    test_terrain_mesh.cpp
    test_pass_list.cpp
    test_image_compare.cpp)
target_link_libraries(strata-unit-tests PRIVATE strata_static)
strata_set_warnings(strata-unit-tests)
add_test(NAME strata-unit-tests COMMAND strata-unit-tests)
