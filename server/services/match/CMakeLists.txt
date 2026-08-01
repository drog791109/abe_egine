get_filename_component(ABE_REPO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    ABSOLUTE
)

add_library(abe_match_server STATIC
    abe_match_server.cpp
)

set_target_properties(abe_match_server
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_match_server
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_match_server
    PUBLIC
        abe_service_common
)

add_executable(abe_match
    abe_match_main.cpp
)

set_target_properties(abe_match
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        RUNTIME_OUTPUT_DIRECTORY "${ABE_REPO_ROOT}/bin"
)

target_link_libraries(abe_match
    PRIVATE
        abe_match_server
)
