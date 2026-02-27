cmake_minimum_required(VERSION 3.10)
project(nvflux VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wformat-security)
endif()

include_directories(include)

# ── Source files shared between the main binary and the test binary ──────────
set(NVFLUX_CORE_SOURCES
    src/nvflux.c
    src/gpu.c
    src/state.c
    src/exec.c
)

# ── Main binary ──────────────────────────────────────────────────────────────
add_executable(nvflux src/main.c ${NVFLUX_CORE_SOURCES})
target_include_directories(nvflux PRIVATE src)

# ── Unit tests ───────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    enable_testing()
    add_executable(test_nvflux tests/test_nvflux.c ${NVFLUX_CORE_SOURCES})
    target_include_directories(test_nvflux PRIVATE include src)
    add_test(NAME unit COMMAND test_nvflux)
    set_tests_properties(unit PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL:")
endif()

# ── Install (optional; scripts/install.sh handles the setuid bit) ────────────
install(TARGETS nvflux DESTINATION bin)
install(FILES man/nvflux.1 DESTINATION share/man/man1)
