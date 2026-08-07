add_library(abe_login_server STATIC
    abe_login_process.cpp
    abe_login_server.cpp
)

set_target_properties(abe_login_server
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_login_server
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_login_server
    PUBLIC
        abe_gatehub_server
        abe_proto_client
        abe_service_common
)

add_executable(abe_login
    abe_login_main.cpp
)

get_filename_component(ABE_REPO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    ABSOLUTE
)

set_target_properties(abe_login
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        RUNTIME_OUTPUT_DIRECTORY "${ABE_REPO_ROOT}/server/bin"
)

target_link_libraries(abe_login
    PRIVATE
        abe_login_server
)
