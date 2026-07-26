option(ABE_ENGINE_ENABLE_KAFKA "Build Kafka backend when librdkafka exists" ON)

if(NOT ABE_ENGINE_ENABLE_KAFKA)
    return()
endif()

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(ABE_RDKAFKA QUIET rdkafka)
endif()

if(NOT ABE_RDKAFKA_FOUND)
    find_path(ABE_RDKAFKA_INCLUDE_DIR
        NAMES librdkafka/rdkafka.h
    )

    find_library(ABE_RDKAFKA_LIBRARY
        NAMES rdkafka
    )

    if(NOT ABE_RDKAFKA_INCLUDE_DIR OR NOT ABE_RDKAFKA_LIBRARY)
        message(WARNING "librdkafka development files were not found; abe_backend_kafka will not be built")
        return()
    endif()
endif()

add_library(abe_backend_kafka STATIC
    abe_kafka.c
)

set_target_properties(abe_backend_kafka
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_backend_kafka
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${ABE_RDKAFKA_INCLUDE_DIRS}
        ${ABE_RDKAFKA_INCLUDE_DIR}
)

target_link_directories(abe_backend_kafka
    PUBLIC
        ${ABE_RDKAFKA_LIBRARY_DIRS}
)

target_link_libraries(abe_backend_kafka
    PUBLIC
        abe_base_error
        abe_base_memory
        ${ABE_RDKAFKA_LIBRARIES}
        ${ABE_RDKAFKA_LIBRARY}
)

target_link_options(abe_backend_kafka
    PUBLIC
        ${ABE_RDKAFKA_LDFLAGS_OTHER}
)

target_compile_options(abe_backend_kafka
    PRIVATE
        ${ABE_RDKAFKA_CFLAGS_OTHER}
)
