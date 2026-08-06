find_package(Protobuf REQUIRED)

set(ABE_STORE_PROTO_FILES
    player_store.proto
)

protobuf_generate_cpp(ABE_STORE_PROTO_SRCS ABE_STORE_PROTO_HDRS
    ${ABE_STORE_PROTO_FILES}
)

add_library(abe_proto_store STATIC
    ${ABE_STORE_PROTO_SRCS}
    ${ABE_STORE_PROTO_HDRS}
)

set_target_properties(abe_proto_store
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_proto_store
    PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
)

if(TARGET protobuf::libprotobuf)
    target_link_libraries(abe_proto_store
        PUBLIC
            protobuf::libprotobuf
    )
else()
    target_include_directories(abe_proto_store
        PUBLIC
            ${Protobuf_INCLUDE_DIRS}
    )
    target_link_libraries(abe_proto_store
        PUBLIC
            ${Protobuf_LIBRARIES}
    )
endif()
