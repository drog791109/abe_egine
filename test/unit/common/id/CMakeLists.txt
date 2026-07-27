add_executable(abe_snowflake_test
    abe_snowflake_test.c
)

set_target_properties(abe_snowflake_test
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_link_libraries(abe_snowflake_test
    PRIVATE
        abe_common_id
)

add_test(NAME abe_snowflake_test COMMAND abe_snowflake_test)
