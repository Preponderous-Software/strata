cmake_minimum_required(VERSION 3.24)

project(strata
    VERSION 0.3.0
    DESCRIPTION "A small C++17 engine for two simulation games"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(STRATA_BUILD_TESTS "Build the unit tests" ON)
option(STRATA_BUILD_TOOLS "Build strata-golden and strata-imgcmp" ON)
option(STRATA_WITH_SDL "Build the SDL3 window surface" ON)
option(STRATA_SANITIZE "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)
option(STRATA_WARNINGS_AS_ERRORS "Treat compiler warnings in Strata's own code as errors" OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(STRATA_WITH_EGL_DEFAULT ON)
else()
    set(STRATA_WITH_EGL_DEFAULT OFF)
endif()
option(STRATA_WITH_EGL "Build the EGL surfaceless headless surface (Linux)" ${STRATA_WITH_EGL_DEFAULT})

# Emscripten (emcmake): a WebGL 2 build. Strata compiles to static
# libraries plus browser executables; there is no shared library, no EGL and no
# glad (Emscripten provides the GLES 3 / WebGL 2 bindings). See docs/WEB.md.
if(EMSCRIPTEN)
    set(STRATA_WASM_EXCEPTIONS "wasm" CACHE STRING
        "How C++ exceptions are compiled for WebAssembly: wasm (-fwasm-exceptions) or js (-fexceptions)")
    set_property(CACHE STRATA_WASM_EXCEPTIONS PROPERTY STRINGS wasm js)
    # Strata reports errors as exceptions inside the library and turns them
    # into status codes at the C ABI, so exceptions must be caught, not
    # aborted on (Emscripten's default). Everything linked into one module
    # must use the same mode.
    if(STRATA_WASM_EXCEPTIONS STREQUAL "wasm")
        set(STRATA_WASM_EH_FLAG -fwasm-exceptions)
    elseif(STRATA_WASM_EXCEPTIONS STREQUAL "js")
        set(STRATA_WASM_EH_FLAG -fexceptions)
    else()
        message(FATAL_ERROR "STRATA_WASM_EXCEPTIONS must be wasm or js")
    endif()
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${STRATA_WASM_EH_FLAG}>")
    add_link_options(${STRATA_WASM_EH_FLAG})
    # Link options for Strata's own browser executables (strata-golden, the
    # examples). WebGL 2 itself is required by strata_static (below).
    set(STRATA_WEB_LINK_OPTIONS
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=1MB)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(StrataDependencies)
include(StrataWarnings)

if(STRATA_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------- glad
# Generated GL 4.5 core + GLES 3.0 (merged) and EGL 1.5 loaders; see
# third_party/glad/README.md for the exact generator command.
if(NOT EMSCRIPTEN)
    set(STRATA_GLAD_SOURCES third_party/glad/src/gl.c)
    if(STRATA_WITH_EGL)
        list(APPEND STRATA_GLAD_SOURCES third_party/glad/src/egl.c)
    endif()
    add_library(strata_glad STATIC ${STRATA_GLAD_SOURCES})
    target_include_directories(strata_glad PUBLIC third_party/glad/include)
    target_link_libraries(strata_glad PUBLIC ${CMAKE_DL_LIBS})
endif()

# ---------------------------------------------------------------- image I/O
add_library(strata_image STATIC
    src/image/image.cpp
    src/image/compare.cpp)
target_include_directories(strata_image
    PUBLIC src
    PRIVATE third_party/stb)
strata_set_warnings(strata_image)

# ---------------------------------------------------------------- engine
set(STRATA_ENGINE_SOURCES
    src/core/log.cpp
    src/core/error.cpp
    src/platform/surface.cpp
    src/platform/host_surface.cpp
    src/rhi/gles3_device.cpp
    src/rhi/glsl_es.cpp
    src/render/primitive_mesh.cpp
    src/render/cascades.cpp
    src/render/sun.cpp
    src/render/lights.cpp
    src/render/terrain_mesh.cpp
    src/render/static_mesh.cpp
    src/render/ray_pick.cpp
    src/render/pass_list.cpp
    src/render/overlay.cpp
    src/render/renderer.cpp
    src/engine.cpp
    src/capi.cpp)
if(NOT EMSCRIPTEN)
    list(APPEND STRATA_ENGINE_SOURCES src/rhi/gl45_device.cpp)
endif()
if(STRATA_WITH_SDL)
    list(APPEND STRATA_ENGINE_SOURCES src/platform/sdl_surface.cpp)
endif()
if(STRATA_WITH_EGL)
    list(APPEND STRATA_ENGINE_SOURCES src/platform/egl_surface.cpp)
endif()

# Everything is compiled once into a static library. libstrata (shared) is
# that library plus nothing else, and the unit tests link the static one so
# they can reach internal code that the shared library does not export.
add_library(strata_static STATIC ${STRATA_ENGINE_SOURCES})
target_include_directories(strata_static
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_compile_definitions(strata_static
    PUBLIC STRATA_STATIC
    PRIVATE
        STRATA_BUILDING_LIBRARY
        STRATA_VERSION_STRING="${PROJECT_VERSION}"
        $<$<BOOL:${STRATA_WITH_SDL}>:STRATA_HAVE_SDL=1>
        $<$<BOOL:${STRATA_WITH_EGL}>:STRATA_HAVE_EGL=1>)
target_link_libraries(strata_static
    PUBLIC strata_image strata::glm)
if(NOT EMSCRIPTEN)
    target_link_libraries(strata_static PUBLIC strata_glad)
endif()
if(STRATA_WITH_SDL)
    target_link_libraries(strata_static PRIVATE SDL3::SDL3-static)
endif()
strata_set_warnings(strata_static)
if(EMSCRIPTEN)
    # The GLES 3 device calls the WebGL 2 entry points; anything that links
    # Strata must link Emscripten's WebGL 2 bindings.
    target_link_options(strata_static INTERFACE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2)
endif()

add_library(strata::static ALIAS strata_static)

if(EMSCRIPTEN)
    # No shared library in a browser: consumers link libstrata.a, one archive
    # holding the engine, its image code and SDL3 (merged below), or the
    # separate archives through CMake. Tools link strata_static.
    add_library(strata ALIAS strata_static)
    set(STRATA_WEB_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/libstrata.a")
    set(STRATA_WEB_MRI "${CMAKE_CURRENT_BINARY_DIR}/libstrata.mri")
    set(mri "CREATE ${STRATA_WEB_ARCHIVE}\nADDLIB $<TARGET_FILE:strata_static>\nADDLIB $<TARGET_FILE:strata_image>\n")
    if(STRATA_WITH_SDL)
        string(APPEND mri "ADDLIB $<TARGET_FILE:SDL3::SDL3-static>\n")
    endif()
    string(APPEND mri "SAVE\nEND\n")
    file(GENERATE OUTPUT "${STRATA_WEB_MRI}" CONTENT "${mri}")
    add_custom_command(OUTPUT "${STRATA_WEB_ARCHIVE}"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${STRATA_WEB_ARCHIVE}"
        COMMAND ${CMAKE_AR} -M < "${STRATA_WEB_MRI}"
        DEPENDS strata_static strata_image "${STRATA_WEB_MRI}"
        COMMENT "Merging the Strata archives into libstrata.a"
        VERBATIM)
    add_custom_target(strata_web_archive ALL DEPENDS "${STRATA_WEB_ARCHIVE}")
else()
    add_library(strata SHARED src/shared_library.cpp)
    target_include_directories(strata PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
    target_compile_definitions(strata PRIVATE STRATA_BUILDING_LIBRARY)
    if(MSVC)
        # Pull every object of the static library in, so its exported C ABI
        # symbols end up in the DLL.
        target_link_libraries(strata PRIVATE strata_static)
        target_link_options(strata PRIVATE "/WHOLEARCHIVE:strata_static")
    else()
        target_link_libraries(strata PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,strata_static>")
        if(NOT APPLE)
            # Only the C ABI is exported (cmake/strata.map), whatever the static
            # archives linked in would otherwise make visible.
            target_link_options(strata PRIVATE
                "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/strata.map"
                "-Wl,--no-undefined")
            set_property(TARGET strata APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/strata.map")
        endif()
    endif()
    set_target_properties(strata PROPERTIES
        OUTPUT_NAME strata
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        BUILD_RPATH_USE_ORIGIN ON)
endif()

# ---------------------------------------------------------------- optional modules
# Optional engine modules live under modules/<name>/ with their own
# CMakeLists.txt, are off by default, and link against strata_static. Jobs
# that do not enable a module never fetch or build its dependencies.
option(STRATA_WITH_USD "Build the OpenUSD scene/save modules (modules/usd, modules/save)" OFF)
if(STRATA_WITH_USD)
    foreach(module usd save)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/${module}/CMakeLists.txt")
            add_subdirectory(modules/${module})
        endif()
    endforeach()
endif()

# The simulation module (modules/sim) has no dependencies and no GL, so it is
# on by default: a seeded RNG, an entity-component registry with a fixed
# iteration order, 2D fields with diffusion, a fixed-step clock and a state
# fingerprint, for games whose simulation is C++.
option(STRATA_WITH_SIM "Build the deterministic simulation module (modules/sim)" ON)
if(STRATA_WITH_SIM)
    add_subdirectory(modules/sim)
endif()

# ---------------------------------------------------------------- tools
if(STRATA_BUILD_TOOLS)
    add_library(strata_scene STATIC tools/common/scene_file.cpp)
    target_include_directories(strata_scene PUBLIC tools/common include)
    target_link_libraries(strata_scene PUBLIC strata)
    strata_set_warnings(strata_scene)

    add_executable(strata-golden tools/strata-golden/main.cpp)
    target_link_libraries(strata-golden PRIVATE strata strata_scene strata_image)
    strata_set_warnings(strata-golden)

    add_executable(strata-imgcmp tools/strata-imgcmp/main.cpp)
    target_link_libraries(strata-imgcmp PRIVATE strata_image)
    strata_set_warnings(strata-imgcmp)

    if(EMSCRIPTEN)
        # A WebGL 2 page: strata-golden.html runs `check` in a browser with the
        # scenes and goldens packaged at /scenes and /golden
        # (tests/web/run_goldens.cjs drives it from headless Chromium).
        set_target_properties(strata-golden PROPERTIES SUFFIX ".html")
        target_link_options(strata-golden PRIVATE ${STRATA_WEB_LINK_OPTIONS}
            -sEXIT_RUNTIME=1
            -sEXPORTED_RUNTIME_METHODS=FS,ENV
            "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/scenes@/scenes"
            "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden@/golden"
            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/web/golden_shell.html")
        set_property(TARGET strata-golden APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/web/golden_shell.html")
    endif()
endif()

# ---------------------------------------------------------------- examples
option(STRATA_BUILD_EXAMPLES "Build the example programs (examples/)" ON)
if(STRATA_BUILD_EXAMPLES AND STRATA_BUILD_TOOLS AND STRATA_WITH_SDL)
    # The L2 village through a day: a window natively, a canvas with Emscripten.
    add_executable(strata-village-day examples/village-day/main.cpp)
    target_link_libraries(strata-village-day PRIVATE strata strata_scene)
    strata_set_warnings(strata-village-day)
    if(EMSCRIPTEN)
        set_target_properties(strata-village-day PROPERTIES SUFFIX ".html")
        target_link_options(strata-village-day PRIVATE ${STRATA_WEB_LINK_OPTIONS}
            "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/scenes/_village_items.scene@/village.scene"
            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/examples/village-day/shell.html")
        set_property(TARGET strata-village-day APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/village-day/shell.html")
    endif()
endif()

# ---------------------------------------------------------------- tests
if(STRATA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
endif()

# ---------------------------------------------------------------- install
include(GNUInstallDirs)
if(EMSCRIPTEN)
    install(FILES "${STRATA_WEB_ARCHIVE}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
else()
    install(TARGETS strata
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(FILES include/strata/strata.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/strata)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/strata)
if(STRATA_BUILD_TOOLS AND NOT EMSCRIPTEN)
    install(TARGETS strata-golden strata-imgcmp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
