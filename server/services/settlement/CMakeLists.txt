get_filename_component(ABE_REPO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    ABSOLUTE
)

add_library(abe_settlement_server STATIC
    abe_settlement_server.cpp
)

set_target_properties(abe_settlement_server
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_settlement_server
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_settlement_server
    PUBLIC
        abe_service_common
)

add_executable(abe_settlement
    abe_settlement_main.cpp
)

set_target_properties(abe_settlement
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        RUNTIME_OUTPUT_DIRECTORY "${ABE_REPO_ROOT}/server/bin"
)

target_link_libraries(abe_settlement
    PRIVATE
        abe_settlement_server
)
