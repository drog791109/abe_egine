add_executable(abe_service_gateway_test
    abe_gateway_service_test.cpp
)

set_target_properties(abe_service_gateway_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_service_gateway_test
    PRIVATE
        abe_service_gateway
)

add_test(NAME abe_service_gateway_test COMMAND abe_service_gateway_test)

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
        abe_service_gateway
)

add_test(NAME abe_gateway_session_test COMMAND abe_gateway_session_test)

add_executable(abe_gateway_message_test
    abe_gateway_message_test.cpp
)

set_target_properties(abe_gateway_message_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_gateway_message_test
    PRIVATE
        abe_service_gateway
)

add_test(NAME abe_gateway_message_test COMMAND abe_gateway_message_test)
