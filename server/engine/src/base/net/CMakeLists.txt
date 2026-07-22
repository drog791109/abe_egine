add_library(abe_base_net STATIC
    abe_net.c
)

set_target_properties(abe_base_net
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_base_net
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)

target_include_directories(abe_base_net
    PRIVATE
        ${LIBEVENT_INCLUDE_DIRS}
)

target_link_directories(abe_base_net
    PUBLIC
        ${LIBEVENT_LIBRARY_DIRS}
)

target_link_libraries(abe_base_net
    PUBLIC
        abe_base_memory
        ${LIBEVENT_LIBRARIES}
)

target_compile_options(abe_base_net
    PRIVATE
        ${LIBEVENT_CFLAGS_OTHER}
)
