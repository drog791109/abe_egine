add_executable(abe_match_server_test
    abe_match_server_test.cpp
)

set_target_properties(abe_match_server_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_match_server_test
    PRIVATE
        abe_match_server
)

add_test(NAME abe_match_server_test COMMAND abe_match_server_test)
