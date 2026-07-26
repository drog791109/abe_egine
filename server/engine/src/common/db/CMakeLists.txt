add_library(abe_common_db STATIC
    abe_db.c
)

set_target_properties(abe_common_db
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_common_db
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_common_db
    PUBLIC
        abe_base_error
        abe_base_memory
)
