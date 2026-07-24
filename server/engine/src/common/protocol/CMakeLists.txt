add_library(abe_common_protocol STATIC
    abe_protocol.c
)

target_include_directories(abe_common_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
