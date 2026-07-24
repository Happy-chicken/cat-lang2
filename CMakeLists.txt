cmake_minimum_required(VERSION 3.10)
project(catlang)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) 

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
    find_package(LLVM 20.1.8 REQUIRED CONFIG)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    set(MLIR_CMAKE_DIR /usr/lib/llvm-20/lib/cmake/mlir)
    message(STATUS "Found MLIR ${MLIR_PACKAGE_VERSION}")
    message(STATUS "Found MLIRConfig.cmake in:  ${MLIR_CMAKE_DIR}")
    list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
    include(MLIRConfig)
    include(AddLLVM)
    include(AddMLIR)
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
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_analyzer/dataflow)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_analyzer/pointer)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_analyzer/dataflow/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_analyzer/pointer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_emitter/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/mlir_emitter/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_optimizer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/midend/ir_optimizer/passes/*.cpp"
)

add_library(catlang_lib STATIC ${SOURCES})

llvm_map_components_to_libnames(LLVM_LIBS
    Analysis Core ExecutionEngine InstCombine Object OrcJIT
    Passes RuntimeDyld ScalarOpts Support TransformUtils native
)
list(REMOVE_DUPLICATES LLVM_LIBS)

set(MLIR_LIBS
    MLIRIR MLIRParser MLIRPass MLIRSupport MLIRExecutionEngine
    MLIRArithDialect MLIRControlFlowDialect MLIRFuncDialect MLIRMemRefDialect
)

add_executable(catlang main.cpp)
if(IS_MACOS)
    target_link_libraries(catlang PRIVATE catlang_lib ${LLVM_LIBS} ${MLIR_LIBS} ncurses)
elseif(IS_LINUX)
    target_link_libraries(catlang PRIVATE catlang_lib ${LLVM_LIBS} ${MLIR_LIBS} tinfo)
endif()

target_compile_options(catlang PRIVATE -g -O0 -fstandalone-debug)
target_compile_options(catlang_lib PRIVATE -g -O0 -fstandalone-debug)

if(IS_MACOS)
    target_link_options(catlang PRIVATE -Wl,-search_paths_first)
elseif(IS_LINUX)
    target_link_options(catlang PRIVATE -rdynamic)
endif()

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    message(STATUS "System GTest not found. Fetching from GitHub...")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    set(GTEST_LIBRARIES gtest gtest_main)
else()
    message(STATUS "Found system GTest")
    set(GTEST_LIBRARIES GTest::gtest GTest::gtest_main)
endif()

file(GLOB_RECURSE TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
add_executable(tests ${TEST_SOURCES})
if (IS_MACOS)
    target_link_libraries(tests PRIVATE
        catlang_lib
        ${GTEST_LIBRARIES}
        pthread
        ${LLVM_LIBS}
        ${MLIR_LIBS}
        ncurses
    )
elseif(IS_LINUX)
    target_link_libraries(tests PRIVATE
        catlang_lib
        ${GTEST_LIBRARIES}
        pthread
        ${LLVM_LIBS}
        ${MLIR_LIBS}
        tinfo
    )
endif()

if(IS_LINUX)
    set_target_properties(tests PROPERTIES
        BUILD_RPATH "/usr/lib/x86_64-linux-gnu:/usr/lib/llvm-20/lib"
        INSTALL_RPATH "/usr/lib/x86_64-linux-gnu:/usr/lib/llvm-20/lib"
    )
endif()

target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(tests PRIVATE -g -O0 -fstandalone-debug)

if(IS_MACOS)
    target_link_options(tests PRIVATE -Wl,-search_paths_first)
endif()

include(GoogleTest)
gtest_discover_tests(tests)

