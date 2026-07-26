add_library(abe_base_time STATIC
    abe_time.c
    abe_time_wheel.c
)

set_target_properties(abe_base_time
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_base_time
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Threads REQUIRED)

target_link_libraries(abe_base_time
    PUBLIC
        abe_base_error
        abe_base_memory
        Threads::Threads
)
