add_executable(abe_common_db_test
    abe_common_db_test.c
)

set_target_properties(abe_common_db_test
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_link_libraries(abe_common_db_test
    PRIVATE
        abe_common_db
)

add_test(NAME abe_common_db_test COMMAND abe_common_db_test)
