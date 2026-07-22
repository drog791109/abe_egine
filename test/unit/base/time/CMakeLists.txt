add_executable(abe_base_time_test
    abe_time_wheel_test.c
)

set_target_properties(abe_base_time_test
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_link_libraries(abe_base_time_test
    PRIVATE
        abe_base_time
)

add_test(NAME abe_base_time_test COMMAND abe_base_time_test)
set_tests_properties(abe_base_time_test PROPERTIES TIMEOUT 10)

add_executable(abe_base_time_api_test
    abe_time_test.c
)

set_target_properties(abe_base_time_api_test
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_link_libraries(abe_base_time_api_test
    PRIVATE
        abe_base_time
)

add_test(NAME abe_base_time_api_test COMMAND abe_base_time_api_test)
set_tests_properties(abe_base_time_api_test PROPERTIES TIMEOUT 10)
