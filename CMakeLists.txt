cmake_minimum_required(VERSION 3.10)
project(catlang)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(IS_MACOS TRUE)
    message(STATUS "Detected macOS")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(IS_LINUX TRUE)
    message(STATUS "Detected Linux")
else()
    message(WARNING "Unsupported OS: ${CMAKE_SYSTEM_NAME}, assuming Linux")
    set(IS_LINUX TRUE)
endif()

if(IS_MACOS)
    # macOS: 使用 llvm-config 查找
    find_program(LLVM_CONFIG_EXECUTABLE
        NAMES llvm-config-20 llvm-config
        PATHS /opt/homebrew/bin /usr/local/bin /usr/bin
        NO_DEFAULT_PATH
    )
    
    if(LLVM_CONFIG_EXECUTABLE)
        execute_process(
            COMMAND ${LLVM_CONFIG_EXECUTABLE} --cmakedir
            OUTPUT_VARIABLE LLVM_CMAKE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        set(LLVM_DIR ${LLVM_CMAKE_DIR})
        message(STATUS "Found llvm-config, using LLVM_DIR = ${LLVM_DIR}")
    else()
        set(LLVM_SEARCH_PATHS
            /opt/homebrew/opt/llvm@20/lib/cmake/llvm
            /usr/local/opt/llvm@20/lib/cmake/llvm
        )
        find_path(LLVM_DIR
            NAMES LLVMConfig.cmake
            PATHS ${LLVM_SEARCH_PATHS}
            NO_DEFAULT_PATH
        )
    endif()
    
    find_package(LLVM REQUIRED CONFIG)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

    find_package(MLIR REQUIRED CONFIG)
    message(STATUS "Found MLIR ${MLIR_PACKAGE_VERSION}")

elseif(IS_LINUX)
    # Ubuntu/Linux: 直接指定版本查找
    find_package(LLVM 20.1.8 REQUIRED CONFIG)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
endif()

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${MLIR_INCLUDE_DIRS})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/backend)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/ast)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/lexer)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/parser)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/sema_checker)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/type_checker)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ast_optimizer)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_analyzer)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_emitter)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/mlir_emitter)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_optimizer)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_optimizer/passes)

file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/ast/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/lexer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/parser/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/sema_checker/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/type_checker/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ast_optimizer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_analyzer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_emitter/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/mlir_emitter/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_optimizer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_optimizer/passes/*.cpp"
)

list(APPEND SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

add_executable(catlang ${SOURCES})

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
target_link_libraries(catlang PRIVATE ${LLVM_LIBS}
    MLIRIR
    MLIRParser
    MLIRPass
    MLIRSupport
    MLIRExecutionEngine
    MLIRArithDialect
    MLIRControlFlowDialect
    MLIRFuncDialect
    MLIRMemRefDialect
)

if(IS_MACOS)
    target_compile_options(catlang PRIVATE -g -O0 -fstandalone-debug)
    target_link_options(catlang PRIVATE -Wl,-search_paths_first)
elseif(IS_LINUX)
    target_compile_options(catlang PRIVATE -g -O0 -fstandalone-debug)
    target_link_options(catlang PRIVATE -rdynamic)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 可选的测试目标（如果你需要，可以取消注释并根据实际情况调整）
# set(TEST_DIR "${PROJECT_SOURCE_DIR}/test")
# add_custom_target(test ...)
