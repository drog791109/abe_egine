add_executable(abe_adapter_math_test
    abe_math_adapter_test.cpp
)

set_target_properties(abe_adapter_math_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_adapter_math_test
    PRIVATE
        abe_adapter_math
        abe_engine_log
)

add_test(NAME abe_adapter_math_test COMMAND abe_adapter_math_test)
