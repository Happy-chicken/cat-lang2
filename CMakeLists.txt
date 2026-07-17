cmake_minimum_required(VERSION 3.10)
project(catlang)

# 设置 C++ 标准（LLVM 20 要求 C++17 以上）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------ 自动查找 LLVM ------------------
# 方法：优先使用 llvm-config 来获得 cmake 目录
find_program(LLVM_CONFIG_EXECUTABLE
    NAMES llvm-config-20 llvm-config
    PATHS /opt/homebrew/bin /usr/local/bin /usr/bin
    NO_DEFAULT_PATH  # 可选，防止找到旧版本
)

if(LLVM_CONFIG_EXECUTABLE)
    execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --cmakedir
        OUTPUT_VARIABLE LLVM_CMAKE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    # 设置 LLVM_DIR 以便 find_package 能找到
    set(LLVM_DIR ${LLVM_CMAKE_DIR})
    message(STATUS "Found llvm-config, using LLVM_DIR = ${LLVM_DIR}")
else()
    # 如果 llvm-config 没找到，尝试一些常见路径
    set(LLVM_SEARCH_PATHS
        /opt/homebrew/opt/llvm@20/lib/cmake/llvm
        /usr/local/opt/llvm@20/lib/cmake/llvm
        /usr/lib/llvm-20/lib/cmake/llvm
        /usr/lib/llvm-20/cmake
    )
    find_path(LLVM_DIR
        NAMES LLVMConfig.cmake
        PATHS ${LLVM_SEARCH_PATHS}
        NO_DEFAULT_PATH
    )
endif()

# 必须找到 LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

# ------------------ 头文件路径 ------------------
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/backend)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/ast)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/lexer)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/parser)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/sema_checker)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/type_checker)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ast_optimizer)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_analyzer)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_emitter)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_optimizer)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_optimizer/passes)

# ------------------ 源文件收集 ------------------
file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/ast/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/lexer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/parser/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/sema_checker/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/type_checker/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ast_optimizer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_analyzer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_emitter/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_optimizer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_optimizer/passes/*.cpp"
)

# ------------------ 生成可执行文件 ------------------
add_executable(catlang ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${SOURCES})

# 链接 LLVM 库（根据你示例中的组件调整）
llvm_map_components_to_libnames(LLVM_LIBS
    Analysis
    Core
    ExecutionEngine
    InstCombine
    Object
    OrcJIT
    RuntimeDyld
    ScalarOpts
    Support
    native
)
list(REMOVE_DUPLICATES LLVM_LIBS)
target_link_libraries(catlang PRIVATE ${LLVM_LIBS})

# 编译选项
target_compile_options(catlang PRIVATE -g -O0 -fstandalone-debug)

# 可选的测试目标（如果你需要，可以取消注释并根据实际情况调整）
# set(TEST_DIR "${PROJECT_SOURCE_DIR}/test")
# add_custom_target(test ...)