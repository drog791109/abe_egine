add_executable(abe_backend_async_integration_test
    abe_backend_async_integration_test.c
)

set_target_properties(abe_backend_async_integration_test
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_link_libraries(abe_backend_async_integration_test
    PRIVATE
        abe_backend_db_mysql
        abe_backend_redis
)

add_test(NAME abe_backend_async_integration_test COMMAND abe_backend_async_integration_test)
