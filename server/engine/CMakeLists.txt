cmake_minimum_required(VERSION 3.16)

project(abe_engine C CXX)

option(ABE_ENGINE_BUILD_TESTS "Build ABE engine tests" ON)
option(ABE_ENGINE_BUILD_LOGIC "Build ABE logic modules" ON)
option(ABE_ENGINE_BUILD_SERVICES "Build ABE service modules" ON)

if(ABE_ENGINE_BUILD_SERVICES AND NOT ABE_ENGINE_BUILD_LOGIC)
    message(FATAL_ERROR "ABE_ENGINE_BUILD_SERVICES requires ABE_ENGINE_BUILD_LOGIC")
endif()

add_subdirectory(src/base)
add_subdirectory(src/common)
add_subdirectory(src/backends)
add_subdirectory(src/log)
add_subdirectory(src/adapters)

if(ABE_ENGINE_BUILD_LOGIC)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../share/proto/client
        ${CMAKE_CURRENT_BINARY_DIR}/share/proto/client)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../share/proto/store
        ${CMAKE_CURRENT_BINARY_DIR}/share/proto/store)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../logic
        ${CMAKE_CURRENT_BINARY_DIR}/logic)
endif()

if(ABE_ENGINE_BUILD_SERVICES)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../services
        ${CMAKE_CURRENT_BINARY_DIR}/services)
endif()

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
        ${CMAKE_CURRENT_SOURCE_DIR}/../../test/unit/backends
        ${CMAKE_CURRENT_BINARY_DIR}/test/unit/backends)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../test/unit/adapters
        ${CMAKE_CURRENT_BINARY_DIR}/test/unit/adapters)
    if(ABE_ENGINE_BUILD_LOGIC)
        add_subdirectory(
            ${CMAKE_CURRENT_SOURCE_DIR}/../../test/unit/logic
            ${CMAKE_CURRENT_BINARY_DIR}/test/unit/logic)
    endif()
    if(ABE_ENGINE_BUILD_SERVICES)
        add_subdirectory(
            ${CMAKE_CURRENT_SOURCE_DIR}/../../test/unit/services
            ${CMAKE_CURRENT_BINARY_DIR}/test/unit/services)
    endif()
endif()
