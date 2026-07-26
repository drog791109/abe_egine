add_executable(abe_gateway_session_test
    abe_gateway_session_test.cpp
)

set_target_properties(abe_gateway_session_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_gateway_session_test
    PRIVATE
        abe_gateway_server
)

add_test(NAME abe_gateway_session_test COMMAND abe_gateway_session_test)
