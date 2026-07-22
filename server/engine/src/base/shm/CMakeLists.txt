add_library(abe_base_shm STATIC
    abe_shm_pool.c
)

set_target_properties(abe_base_shm
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_base_shm
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Threads REQUIRED)

target_link_libraries(abe_base_shm
    PUBLIC
        Threads::Threads
)
