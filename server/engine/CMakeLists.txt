cmake_minimum_required(VERSION 3.16)

project(abe_engine C CXX)

option(ABE_ENGINE_BUILD_TESTS "Build ABE engine tests" ON)

add_subdirectory(src/base)
add_subdirectory(src/common)
add_subdirectory(src/log)
add_subdirectory(src/adapters)

if(ABE_ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../test/unit/base
        ${CMAKE_CURRENT_BINARY_DIR}/test/unit/base)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../test/unit/common
        ${CMAKE_CURRENT_BINARY_DIR}/test/unit/common)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../test/unit/log
        ${CMAKE_CURRENT_BINARY_DIR}/test/unit/log)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../test/unit/adapters
        ${CMAKE_CURRENT_BINARY_DIR}/test/unit/adapters)
endif()
