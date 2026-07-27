option(ABE_ENGINE_ENABLE_REDIS "Build Redis backend when hiredis exists" ON)

if(NOT ABE_ENGINE_ENABLE_REDIS)
    return()
endif()

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(ABE_HIREDIS QUIET hiredis)
    pkg_check_modules(ABE_LIBEVENT QUIET libevent)
endif()

if(NOT ABE_HIREDIS_FOUND)
    find_path(ABE_HIREDIS_INCLUDE_DIR
        NAMES hiredis.h
        PATH_SUFFIXES hiredis
    )

    find_library(ABE_HIREDIS_LIBRARY
        NAMES hiredis
    )

    if(NOT ABE_HIREDIS_INCLUDE_DIR OR NOT ABE_HIREDIS_LIBRARY)
        message(WARNING "hiredis development files were not found; abe_backend_redis will not be built")
        return()
    endif()
endif()

if(NOT ABE_LIBEVENT_FOUND)
    find_path(ABE_LIBEVENT_INCLUDE_DIR NAMES event2/event.h)
    find_library(ABE_LIBEVENT_LIBRARY NAMES event)
    if(NOT ABE_LIBEVENT_INCLUDE_DIR OR NOT ABE_LIBEVENT_LIBRARY)
        message(WARNING "libevent development files were not found; abe_backend_redis will not be built")
        return()
    endif()
endif()

add_library(abe_backend_redis STATIC
    abe_redis.c
    abe_redis_async.c
)

set_target_properties(abe_backend_redis
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_backend_redis
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${ABE_HIREDIS_INCLUDE_DIRS}
        ${ABE_HIREDIS_INCLUDE_DIR}
        ${ABE_LIBEVENT_INCLUDE_DIRS}
        ${ABE_LIBEVENT_INCLUDE_DIR}
)

target_link_directories(abe_backend_redis
    PUBLIC
        ${ABE_HIREDIS_LIBRARY_DIRS}
        ${ABE_LIBEVENT_LIBRARY_DIRS}
)

target_link_libraries(abe_backend_redis
    PUBLIC
        abe_base_error
        abe_base_memory
        abe_base_time
        ${ABE_HIREDIS_LIBRARIES}
        ${ABE_HIREDIS_LIBRARY}
        ${ABE_LIBEVENT_LIBRARIES}
        ${ABE_LIBEVENT_LIBRARY}
)

target_link_options(abe_backend_redis
    PUBLIC
        ${ABE_HIREDIS_LDFLAGS_OTHER}
        ${ABE_LIBEVENT_LDFLAGS_OTHER}
)

target_compile_options(abe_backend_redis
    PRIVATE
        ${ABE_HIREDIS_CFLAGS_OTHER}
        ${ABE_LIBEVENT_CFLAGS_OTHER}
)
