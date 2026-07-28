add_executable(abe_logic_rpc_test
    abe_rpc_test.cpp
)

set_target_properties(abe_logic_rpc_test
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_logic_rpc_test
    PRIVATE
        abe_logic_rpc
)

add_test(NAME abe_logic_rpc_test COMMAND abe_logic_rpc_test)
