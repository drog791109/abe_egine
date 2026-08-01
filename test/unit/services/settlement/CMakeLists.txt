add_executable(abe_settlement_server_test
    abe_settlement_server_test.cpp
)

set_target_properties(abe_settlement_server_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_settlement_server_test
    PRIVATE
        abe_settlement_server
)

add_test(NAME abe_settlement_server_test COMMAND abe_settlement_server_test)
