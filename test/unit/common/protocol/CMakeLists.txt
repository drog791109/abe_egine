add_executable(abe_common_protocol_test
    abe_protocol_test.c
)

target_link_libraries(abe_common_protocol_test
    PRIVATE
        abe_common_protocol
)

add_test(NAME abe_common_protocol_test COMMAND abe_common_protocol_test)
