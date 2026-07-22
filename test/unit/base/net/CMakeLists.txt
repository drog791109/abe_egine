add_executable(abe_base_net_test
    abe_base_net_test.c
)

set_target_properties(abe_base_net_test
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_link_libraries(abe_base_net_test
    PRIVATE
        abe_base_net
)

add_test(NAME abe_base_net_test COMMAND abe_base_net_test)
set_tests_properties(abe_base_net_test PROPERTIES TIMEOUT 10)
