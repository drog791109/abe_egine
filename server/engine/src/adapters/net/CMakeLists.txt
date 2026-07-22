add_library(abe_adapter_net STATIC
    abe_net_link.cpp
    abe_net_server.cpp
)

set_target_properties(abe_adapter_net
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_adapter_net
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_adapter_net
    PUBLIC
        abe_base_net
)
