# Third-party dependencies, fetched at configure time and pinned by hash.
#
# Updating one is a deliberate change: bump the URL and the SHA-256 together
# and say why in the PR. Nothing here floats.

include(FetchContent)

# ---------------------------------------------------------------- SDL3 (zlib)
if(STRATA_WITH_SDL)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    # Subsystems no milestone uses yet. Each is turned back on by the
    # milestone that needs it (audio, for one), not before.
    set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
    set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
    set(SDL_GPU OFF CACHE BOOL "" FORCE)
    set(SDL_RENDER OFF CACHE BOOL "" FORCE)
    set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
    set(SDL_TRAY OFF CACHE BOOL "" FORCE)
    set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
    if(EMSCRIPTEN)
        # SDL's Emscripten video driver makes WebGL contexts through its GLES path.
        set(SDL_OPENGLES ON CACHE BOOL "" FORCE)
    else()
        set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
    endif()
    set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
        URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(SDL3)
endif()

# ---------------------------------------------------------------- glm (MIT)
# Header-only; only the include directory is used.
FetchContent_Declare(glm
    URL https://github.com/g-truc/glm/archive/refs/tags/1.0.2.tar.gz
    URL_HASH SHA256=19edf2e860297efab1c74950e6076bf4dad9de483826bc95e2e0f2c758a43f65
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_GetProperties(glm)
if(NOT glm_POPULATED)
    FetchContent_Populate(glm)
endif()
add_library(strata_glm INTERFACE)
target_include_directories(strata_glm SYSTEM INTERFACE "${glm_SOURCE_DIR}")
target_compile_definitions(strata_glm INTERFACE GLM_FORCE_CXX17 GLM_ENABLE_EXPERIMENTAL)
add_library(strata::glm ALIAS strata_glm)
