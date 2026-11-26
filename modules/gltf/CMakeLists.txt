# Strata glTF module: loads glTF 2.0 (.gltf/.glb) scenes onto an engine
# through the public C ABI. Built when STRATA_WITH_GLTF is ON (the default on
# desktop). Its one dependency, cgltf (MIT), is vendored in third_party/cgltf;
# images are decoded with the vendored stb_image. Nothing is fetched.
#
#   strata_gltf         C++ library: load (model.hpp), plan (plan.hpp), upload (upload.hpp)
#   strata_gltf_shared  libstrata_gltf.so / strata_gltf.dll: the C API in strata_gltf.h, for bindings
#   strata-gltf-view    headless render of a .gltf/.glb to a PNG
#
# See docs/GLTF.md.

add_library(strata_cgltf STATIC src/cgltf_impl.c)
target_include_directories(strata_cgltf PUBLIC ${PROJECT_SOURCE_DIR}/third_party/cgltf)

add_library(strata_gltf STATIC
    src/load.cpp
    src/image_decode.cpp
    src/atlas.cpp
    src/plan.cpp
    src/upload.cpp)
target_include_directories(strata_gltf
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src
        ${PROJECT_SOURCE_DIR}/third_party/stb)
target_compile_features(strata_gltf PUBLIC cxx_std_17)
# The C ABI through the shared library (strata_static in a browser build).
target_link_libraries(strata_gltf PUBLIC strata PRIVATE strata_cgltf)
strata_set_warnings(strata_gltf)
# A file name of its own: on Windows the shared library's import library is
# strata_gltf.lib, and MSVC refuses to link when the two collide (LNK1149).
set_target_properties(strata_gltf PROPERTIES OUTPUT_NAME strata_gltf_static)
add_library(strata::gltf ALIAS strata_gltf)

if(NOT EMSCRIPTEN)
    add_library(strata_gltf_shared SHARED src/capi.cpp)
    target_link_libraries(strata_gltf_shared PUBLIC strata PRIVATE strata_gltf)
    target_include_directories(strata_gltf_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
    target_compile_definitions(strata_gltf_shared PRIVATE STRATA_GLTF_BUILDING_LIBRARY)
    set_target_properties(strata_gltf_shared PROPERTIES
        OUTPUT_NAME strata_gltf
        # Beside libstrata in the build tree, as in the release archives.
        LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        # Finds libstrata beside itself, as the release archives ship them.
        BUILD_RPATH_USE_ORIGIN ON
        INSTALL_RPATH "$ORIGIN")
    if(NOT MSVC AND NOT APPLE)
        target_link_options(strata_gltf_shared PRIVATE "-Wl,--no-undefined")
    endif()
    strata_set_warnings(strata_gltf_shared)

    if(STRATA_BUILD_TOOLS)
        add_executable(strata-gltf-view tools/strata-gltf-view/main.cpp)
        target_link_libraries(strata-gltf-view PRIVATE strata_gltf strata strata_image)
        set_target_properties(strata-gltf-view PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
        strata_set_warnings(strata-gltf-view)
        install(TARGETS strata-gltf-view RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    install(TARGETS strata_gltf_shared
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES include/strata/gltf/strata_gltf.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/strata/gltf)
endif()

if(STRATA_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()  # the top level enables testing only after the modules are added
    # Loading and planning on the synthetic assets: CPU only.
    add_executable(strata-gltf-tests
        ${PROJECT_SOURCE_DIR}/tests/unit/main.cpp
        tests/test_gltf.cpp)
    target_include_directories(strata-gltf-tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/unit src)
    target_link_libraries(strata-gltf-tests PRIVATE strata_gltf)
    target_compile_definitions(strata-gltf-tests PRIVATE
        STRATA_GLTF_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets")
    strata_set_warnings(strata-gltf-tests)
    # Beside the shared libstrata it links, so Windows finds the DLL.
    set_target_properties(strata-gltf-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
    add_test(NAME strata-gltf-tests COMMAND strata-gltf-tests)

    # The C API on a real headless engine. Skipped (77) without EGL.
    if(NOT WIN32)
        add_executable(strata-gltf-e2e tests/gltf_e2e.cpp)
        target_link_libraries(strata-gltf-e2e PRIVATE strata_gltf_shared strata)
        target_compile_definitions(strata-gltf-e2e PRIVATE
            STRATA_GLTF_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets")
        strata_set_warnings(strata-gltf-e2e)
        set_target_properties(strata-gltf-e2e PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
        add_test(NAME strata-gltf-e2e COMMAND strata-gltf-e2e)
        set_tests_properties(strata-gltf-e2e PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    # Golden images (tests/golden/<name>/linux-llvmpipe.png), docs/GOLDEN_IMAGES.md's
    # rules. Skipped (77) without headless GL.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND AND STRATA_BUILD_TOOLS)
        add_test(NAME strata-gltf-goldens
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_goldens.py
                    --view $<TARGET_FILE:strata-gltf-view> --imgcmp $<TARGET_FILE:strata-imgcmp>
                    --out ${CMAKE_CURRENT_BINARY_DIR}/golden-out --skip-without-gl)
        set_tests_properties(strata-gltf-goldens PROPERTIES SKIP_RETURN_CODE 77)
        # The committed assets are what make_fixtures.py writes.
        add_test(NAME strata-gltf-fixtures
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/make_fixtures.py --check)
    endif()
endif()
