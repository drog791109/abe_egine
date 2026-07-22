add_executable(abe_backend_header_test
    abe_backend_header_test.c
)

set_target_properties(abe_backend_header_test
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_backend_header_test
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common/db
        ${CMAKE_SOURCE_DIR}/src/backends/db_mysql
        ${CMAKE_SOURCE_DIR}/src/backends/redis
        ${CMAKE_SOURCE_DIR}/src/backends/kafka
        ${CMAKE_SOURCE_DIR}/src/backends/rabbitmq
)

add_test(NAME abe_backend_header_test COMMAND abe_backend_header_test)
