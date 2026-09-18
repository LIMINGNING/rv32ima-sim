cmake_minimum_required(VERSION 3.16)

project(rv32ima-sim LANGUAGES C)

set(MARSS_SOURCE_DIR "" CACHE PATH "MARSS src directory containing iomem.c (optional)")
add_library(sim_memory src/riscvsim/system/memory.c)
set_target_properties(sim_memory PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sim_memory PRIVATE -Wall -Wextra -Werror)
endif()
if(MARSS_SOURCE_DIR)
    foreach(file iomem.c cutils.c iomem.h cutils.h)
        if(NOT EXISTS "${MARSS_SOURCE_DIR}/${file}")
            message(FATAL_ERROR "Missing MARSS source: ${MARSS_SOURCE_DIR}/${file}")
        endif()
    endforeach()
    add_library(marss_iomem STATIC "${MARSS_SOURCE_DIR}/iomem.c" "${MARSS_SOURCE_DIR}/cutils.c")
    target_include_directories(sim_memory PRIVATE "${MARSS_SOURCE_DIR}")
    target_compile_definitions(sim_memory PRIVATE SIM_WITH_MARSS=1)
    target_link_libraries(sim_memory PUBLIC marss_iomem)
endif()

# 为编辑器提供编译参数。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(sim
    src/riscvsim/riscvsim_cpu.c
    src/riscvsim/core/core.c
    src/riscvsim/core/fetch.c
    src/riscvsim/core/memory.c
    src/riscvsim/core/commit.c
    src/riscvsim/core/atomic.c
    src/riscvsim/core/decode.c
    src/riscvsim/core/execute.c
    src/riscvsim/utils/trace.c
)
target_link_libraries(sim PRIVATE sim_memory)

set_target_properties(sim PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sim PRIVATE -Wall -Wextra -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(rv32i_test
        tests/rv32i_test.c
        src/riscvsim/core/core.c
        src/riscvsim/core/fetch.c
        src/riscvsim/core/memory.c
        src/riscvsim/core/commit.c
        src/riscvsim/core/atomic.c
        src/riscvsim/core/decode.c
        src/riscvsim/core/execute.c
        src/riscvsim/utils/trace.c
    )
    target_include_directories(rv32i_test PRIVATE src/riscvsim)
    set_target_properties(rv32i_test PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rv32i_test PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME rv32i COMMAND rv32i_test)

    add_executable(rv32a_test
        tests/rv32a_test.c
        src/riscvsim/core/core.c
        src/riscvsim/core/fetch.c
        src/riscvsim/core/memory.c
        src/riscvsim/core/commit.c
        src/riscvsim/core/atomic.c
        src/riscvsim/core/decode.c
        src/riscvsim/core/execute.c
        src/riscvsim/utils/trace.c)
    target_include_directories(rv32a_test PRIVATE src/riscvsim)
    set_target_properties(rv32a_test PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rv32a_test PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME rv32a COMMAND rv32a_test)
    add_test(NAME atomic_demo COMMAND sim)
endif()

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME trace_tools
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools_test.py)
        add_test(NAME loader_trace
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/loader_trace_test.py
                    $<TARGET_FILE:sim> $<BOOL:${MARSS_SOURCE_DIR}>)
        add_test(NAME spike_trace_tools
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/spike_tools_test.py)
    endif()
endif()
