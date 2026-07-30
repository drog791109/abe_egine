add_executable(abe_service_session_test
    abe_session_test.cpp
)

set_target_properties(abe_service_session_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_service_session_test
    PRIVATE
        abe_service_session
        abe_engine_log
)

add_test(NAME abe_service_session_test COMMAND abe_service_session_test)
