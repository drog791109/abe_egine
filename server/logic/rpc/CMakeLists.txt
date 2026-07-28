add_library(abe_logic_rpc STATIC
    abe_rpc.cpp
)

set_target_properties(abe_logic_rpc
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_logic_rpc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_logic_rpc
    PUBLIC
        abe_common_protocol
        abe_engine_log
)
