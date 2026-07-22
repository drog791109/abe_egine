option(ABE_ENGINE_ENABLE_RABBITMQ "Build RabbitMQ backend when rabbitmq-c exists" ON)

if(NOT ABE_ENGINE_ENABLE_RABBITMQ)
    return()
endif()

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(ABE_RABBITMQ_C QUIET librabbitmq)
endif()

if(NOT ABE_RABBITMQ_C_FOUND)
    find_path(ABE_RABBITMQ_C_INCLUDE_DIR
        NAMES amqp.h
    )

    find_library(ABE_RABBITMQ_C_LIBRARY
        NAMES rabbitmq
    )

    if(NOT ABE_RABBITMQ_C_INCLUDE_DIR OR NOT ABE_RABBITMQ_C_LIBRARY)
        message(WARNING "rabbitmq-c development files were not found; abe_backend_rabbitmq will not be built")
        return()
    endif()
endif()

add_library(abe_backend_rabbitmq STATIC
    abe_rabbitmq.c
)

set_target_properties(abe_backend_rabbitmq
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_backend_rabbitmq
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${ABE_RABBITMQ_C_INCLUDE_DIRS}
        ${ABE_RABBITMQ_C_INCLUDE_DIR}
)

target_link_directories(abe_backend_rabbitmq
    PUBLIC
        ${ABE_RABBITMQ_C_LIBRARY_DIRS}
)

target_link_libraries(abe_backend_rabbitmq
    PUBLIC
        abe_base_memory
        ${ABE_RABBITMQ_C_LIBRARIES}
        ${ABE_RABBITMQ_C_LIBRARY}
)

target_link_options(abe_backend_rabbitmq
    PUBLIC
        ${ABE_RABBITMQ_C_LDFLAGS_OTHER}
)

target_compile_options(abe_backend_rabbitmq
    PRIVATE
        ${ABE_RABBITMQ_C_CFLAGS_OTHER}
)
