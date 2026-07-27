add_library(abe_service_store STATIC
    abe_player_store.cpp
)

set_target_properties(abe_service_store
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_service_store
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_service_store
    PUBLIC
        abe_common_db
        abe_proto_store
        abe_service_common
)
