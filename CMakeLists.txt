cmake_minimum_required(VERSION 3.24)

project(strata
    VERSION 0.2.0
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

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(StrataDependencies)
include(StrataWarnings)

if(STRATA_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------- glad
# Generated GL 4.5 core (no extensions) and EGL 1.5 loaders; see
# third_party/glad/README.md for the exact generator command.
set(STRATA_GLAD_SOURCES third_party/glad/src/gl.c)
if(STRATA_WITH_EGL)
    list(APPEND STRATA_GLAD_SOURCES third_party/glad/src/egl.c)
endif()
add_library(strata_glad STATIC ${STRATA_GLAD_SOURCES})
target_include_directories(strata_glad PUBLIC third_party/glad/include)
target_link_libraries(strata_glad PUBLIC ${CMAKE_DL_LIBS})

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
    src/rhi/gl45_device.cpp
    src/render/primitive_mesh.cpp
    src/render/cascades.cpp
    src/render/sun.cpp
    src/render/lights.cpp
    src/render/terrain_mesh.cpp
    src/render/pass_list.cpp
    src/render/renderer.cpp
    src/engine.cpp
    src/capi.cpp)
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
    PUBLIC strata_glad strata_image strata::glm)
if(STRATA_WITH_SDL)
    target_link_libraries(strata_static PRIVATE SDL3::SDL3-static)
endif()
strata_set_warnings(strata_static)

add_library(strata::static ALIAS strata_static)

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
endif()

# ---------------------------------------------------------------- tests
if(STRATA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
endif()

# ---------------------------------------------------------------- install
include(GNUInstallDirs)
install(TARGETS strata
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/strata/strata.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/strata)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/strata)
if(STRATA_BUILD_TOOLS)
    install(TARGETS strata-golden strata-imgcmp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
