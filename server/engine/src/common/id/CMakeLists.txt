add_library(abe_common_id STATIC
    abe_snowflake.c
)

set_target_properties(abe_common_id
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_common_id
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_common_id
    PUBLIC
        abe_base_error
        abe_base_time
)
