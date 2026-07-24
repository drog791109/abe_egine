add_library(abe_logic_session STATIC
    abe_session.cpp
    abe_session_server.cpp
)

set_target_properties(abe_logic_session
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_logic_session
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
