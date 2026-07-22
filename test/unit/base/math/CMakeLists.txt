add_executable(abe_base_math_test
    abe_math_test.c
)

set_target_properties(abe_base_math_test
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_link_libraries(abe_base_math_test
    PRIVATE
        abe_base_math
)

add_test(NAME abe_base_math_test COMMAND abe_base_math_test)
