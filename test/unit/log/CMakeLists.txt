add_executable(abe_engine_log_test
    abe_log_test.cpp
)

set_target_properties(abe_engine_log_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_engine_log_test
    PRIVATE
        abe_base_time
        abe_engine_log
)

add_test(NAME abe_engine_log_test COMMAND abe_engine_log_test)
