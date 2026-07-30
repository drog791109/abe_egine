add_executable(abe_login_process_test
    abe_login_process_test.cpp
)

set_target_properties(abe_login_process_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_login_process_test
    PRIVATE
        abe_login_server
        abe_common_id
)

add_test(NAME abe_login_process_test COMMAND abe_login_process_test)

add_executable(abe_login_server_test
    abe_login_server_test.cpp
)

set_target_properties(abe_login_server_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_login_server_test
    PRIVATE
        abe_login_server
        abe_common_id
)

add_test(NAME abe_login_server_test COMMAND abe_login_server_test)
