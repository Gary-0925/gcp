cmake_minimum_required(VERSION 3.16)

project(GCP
    VERSION 0.1.1
    DESCRIPTION "GCP (Greenhouse Code Producer) - Next-Gen C/C++ compiler!"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(ts_runtime OBJECT
    tree-sitter/tree-sitter/src/lib.c
)
target_include_directories(ts_runtime PRIVATE
    tree-sitter/tree-sitter/include
    tree-sitter/tree-sitter/src
)

add_library(ts_grammar_c OBJECT
    tree-sitter/tree-sitter-c/parser.c
)
target_include_directories(ts_grammar_c PRIVATE
    tree-sitter/tree-sitter-c
)

add_library(ts_grammar_cpp OBJECT
    tree-sitter/tree-sitter-cpp/parser.c
    tree-sitter/tree-sitter-cpp/scanner.c
)
target_include_directories(ts_grammar_cpp PRIVATE
    tree-sitter/tree-sitter-cpp
)

foreach(ts_target IN ITEMS ts_runtime ts_grammar_c ts_grammar_cpp)
    if(MSVC)
        target_compile_definitions(${ts_target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${ts_target} PRIVATE /W0 /utf-8)
    else()
        target_compile_options(${ts_target} PRIVATE -w)
    endif()
endforeach()

add_executable(gcp
    src/chemistry.cpp
    src/runtime.cpp
    src/ast_engine.cpp
    src/driver.cpp
    src/main.cpp
    $<TARGET_OBJECTS:ts_runtime>
    $<TARGET_OBJECTS:ts_grammar_c>
    $<TARGET_OBJECTS:ts_grammar_cpp>
)

target_include_directories(gcp PRIVATE
    include
    tree-sitter/tree-sitter/include
)

if(MSVC)
    target_compile_definitions(gcp PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(gcp PRIVATE /W4 /utf-8)
else()
    target_compile_options(gcp PRIVATE -Wall -Wextra)
    if(NOT APPLE)
        target_link_options(gcp PRIVATE $<$<CONFIG:Release>:-s>)
    endif()
endif()

add_custom_command(TARGET gcp POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -f "$<TARGET_FILE_DIR:gcp>/g--${CMAKE_EXECUTABLE_SUFFIX}"
    COMMAND "${CMAKE_COMMAND}" -E create_hardlink
            "$<TARGET_FILE:gcp>"
            "$<TARGET_FILE_DIR:gcp>/g--${CMAKE_EXECUTABLE_SUFFIX}"
    COMMENT "Creating hardlink g--${CMAKE_EXECUTABLE_SUFFIX} (Green--) -> gcp${CMAKE_EXECUTABLE_SUFFIX}"
)

include(GNUInstallDirs)
install(TARGETS gcp RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(PROGRAMS "$<TARGET_FILE_DIR:gcp>/g--${CMAKE_EXECUTABLE_SUFFIX}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
