cmake_minimum_required(VERSION 3.16)

project(rv32ima-sim LANGUAGES C)

# 为编辑器提供编译参数。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(sim
    src/riscvsim/riscvsim_cpu.c
    src/riscvsim/core/core.c
    src/riscvsim/core/decode.c
    src/riscvsim/core/execute.c
    src/riscvsim/utils/trace.c
)

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
endif()

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME trace_tools
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools_test.py)
    endif()
endif()
