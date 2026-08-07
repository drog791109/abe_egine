get_filename_component(ABE_REPO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    ABSOLUTE
)

add_library(abe_game_server STATIC
    abe_game_session.cpp
    abe_game_session_impl.cpp
    abe_game_server.cpp
)

set_target_properties(abe_game_server
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_game_server
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GAME_HANDLERS_DIR}
)

target_link_libraries(abe_game_server
    PUBLIC
        abe_service_session
        abe_service_common
        abe_common_protocol
        abe_proto_client
)

add_executable(abe_game
    abe_game_main.cpp
)

set_target_properties(abe_game
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        RUNTIME_OUTPUT_DIRECTORY "${ABE_REPO_ROOT}/server/bin"
)

target_link_libraries(abe_game
    PRIVATE
        abe_game_server
)
