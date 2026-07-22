add_executable(abe_logic_session_test
    abe_session_manager_test.cpp
)

set_target_properties(abe_logic_session_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_logic_session_test
    PRIVATE
        abe_logic_session
)

add_test(NAME abe_logic_session_test COMMAND abe_logic_session_test)
