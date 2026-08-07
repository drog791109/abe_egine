add_library(abe_gatehub_server STATIC
    abe_gatehub_server.cpp
)

set_target_properties(abe_gatehub_server
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_gatehub_server
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_gatehub_server
    PUBLIC
        abe_proto_client
        abe_service_common
)

add_executable(abe_gatehub
    abe_gatehub_main.cpp
)

get_filename_component(ABE_REPO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    ABSOLUTE
)

set_target_properties(abe_gatehub
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        RUNTIME_OUTPUT_DIRECTORY "${ABE_REPO_ROOT}/server/bin"
)

target_link_libraries(abe_gatehub
    PRIVATE
        abe_gatehub_server
)
