add_executable(abe_game_session_test
    abe_game_session_test.cpp
)

set_target_properties(abe_game_session_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_game_session_test
    PRIVATE
        abe_game_server
)

add_test(NAME abe_game_session_test COMMAND abe_game_session_test)
