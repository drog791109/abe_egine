add_executable(abe_error_test
    abe_error_test.c
)

set_target_properties(abe_error_test
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_link_libraries(abe_error_test
    PRIVATE
        abe_base_error
        abe_base_config
        abe_base_memory
        abe_base_net
        abe_base_shm
        abe_base_time
)

add_test(NAME abe_error_test COMMAND abe_error_test)
