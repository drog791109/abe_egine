add_library(abe_service_session STATIC
    abe_session.cpp
    abe_session_server.cpp
)

set_target_properties(abe_service_session
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_service_session
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_service_session
    PUBLIC
        abe_proto_client
)
