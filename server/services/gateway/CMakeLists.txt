add_library(abe_gateway_server STATIC
    abe_gateway_server.cpp
    abe_gateway_session.cpp
)

set_target_properties(abe_gateway_server
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_gateway_server
    PUBLIC
        abe_adapter_net
        abe_logic_session
        abe_common_protocol
        abe_base_time
        abe_service_common
)

target_include_directories(abe_gateway_server
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(abe_gateway
    abe_gateway_main.cpp
)

get_filename_component(ABE_REPO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    ABSOLUTE
)

set_target_properties(abe_gateway
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        RUNTIME_OUTPUT_DIRECTORY "${ABE_REPO_ROOT}/bin"
)

target_link_libraries(abe_gateway
    PRIVATE
        abe_gateway_server
        abe_service_common
)
