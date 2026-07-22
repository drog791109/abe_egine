add_library(abe_base_memory STATIC
    abe_mem_pool.c
    abe_pool.c
)

set_target_properties(abe_base_memory
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_base_memory
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
