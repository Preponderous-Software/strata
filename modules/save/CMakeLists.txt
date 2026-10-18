# Strata save core (RFC 0019 §6, M1): content-addressed saves behind an atomic
# HEAD pointer, SHA-256 checked before parsing, verify-before-commit, fallback,
# migrations and asynchronous saves. Optional: built with -DSTRATA_WITH_USD=ON.

find_package(Threads REQUIRED)

add_library(strata_save STATIC
    src/sha256.cpp
    src/fs_posix.cpp
    src/slot.cpp)
target_include_directories(strata_save PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(strata_save PUBLIC strata_usd Threads::Threads)
strata_set_warnings(strata_save)
add_library(strata::save ALIAS strata_save)

add_executable(strata-save-bench bench/save_bench.cpp tests/toy_game.cpp)
target_link_libraries(strata-save-bench PRIVATE strata_save)
target_compile_definitions(strata-save-bench PRIVATE STRATA_SAVE_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
strata_set_warnings(strata-save-bench)

if(STRATA_BUILD_TESTS)
    enable_testing()  # the top level enables testing only after the modules are added
    set(STRATA_SAVE_TEST_DATA "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_executable(strata-save-tests tests/test_save.cpp tests/toy_game.cpp)
    target_link_libraries(strata-save-tests PRIVATE strata_save)
    target_compile_definitions(strata-save-tests PRIVATE STRATA_SAVE_TEST_DATA="${STRATA_SAVE_TEST_DATA}")
    strata_set_warnings(strata-save-tests)
    add_test(NAME strata-save-tests COMMAND strata-save-tests "${CMAKE_CURRENT_BINARY_DIR}/save-test-out")
    # The slow probes (single-bit flips in capped child processes, 25 rounds of
    # kill -9 during saves) run as their own test so their timing is visible.
    add_test(NAME strata-save-crash-tests COMMAND strata-save-tests "${CMAKE_CURRENT_BINARY_DIR}/save-crash-out" --crash)
    set_tests_properties(strata-save-crash-tests PROPERTIES TIMEOUT 1800)
endif()
