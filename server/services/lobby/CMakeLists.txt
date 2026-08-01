get_filename_component(ABE_REPO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    ABSOLUTE
)

add_library(abe_lobby_server STATIC
    abe_lobby_session.cpp
    abe_lobby_session_impl.cpp
    abe_lobby_server.cpp
)

set_target_properties(abe_lobby_server
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_lobby_server
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${LOBBY_HANDLERS_DIR}
)

target_link_libraries(abe_lobby_server
    PUBLIC
        abe_service_session
        abe_service_common
        abe_common_protocol
        abe_proto_client
)

add_executable(abe_lobby
    abe_lobby_main.cpp
)

set_target_properties(abe_lobby
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        RUNTIME_OUTPUT_DIRECTORY "${ABE_REPO_ROOT}/bin"
)

target_link_libraries(abe_lobby
    PRIVATE
        abe_lobby_server
)
