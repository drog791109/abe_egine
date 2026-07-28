add_executable(abe_adapter_net_test
    abe_net_link_test.cpp
)

set_target_properties(abe_adapter_net_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_adapter_net_test
    PRIVATE
        abe_adapter_net
        abe_engine_log
)

add_test(NAME abe_adapter_net_test COMMAND abe_adapter_net_test)
set_tests_properties(abe_adapter_net_test PROPERTIES TIMEOUT 10)
