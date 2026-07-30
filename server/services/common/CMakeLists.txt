add_library(abe_service_common STATIC
    abe_service_runtime.cpp
)

set_target_properties(abe_service_common
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_service_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_service_common
    PUBLIC
        abe_adapter_net
        abe_base_config
        abe_base_time
        abe_common_db
        abe_common_id
        abe_engine_log
)

if(NOT TARGET abe_backend_db_mysql)
    message(FATAL_ERROR "abe_service_common requires abe_backend_db_mysql")
endif()

if(NOT TARGET abe_backend_redis)
    message(FATAL_ERROR "abe_service_common requires abe_backend_redis")
endif()

target_compile_definitions(abe_service_common
    PRIVATE
        ABE_SERVICE_HAS_MYSQL=1
        ABE_SERVICE_HAS_REDIS=1
)

target_link_libraries(abe_service_common
    PUBLIC
        abe_backend_db_mysql
        abe_backend_redis
)

add_subdirectory(store)
