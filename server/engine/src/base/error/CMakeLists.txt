add_library(abe_base_error STATIC
    abe_error.c
)

set_target_properties(abe_base_error
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_base_error
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
