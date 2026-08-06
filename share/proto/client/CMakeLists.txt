find_package(Protobuf REQUIRED)

set(ABE_CLIENT_PROTO_FILES
    protocol.proto
    login.proto
    game.proto
)

protobuf_generate_cpp(ABE_CLIENT_PROTO_SRCS ABE_CLIENT_PROTO_HDRS
    ${ABE_CLIENT_PROTO_FILES}
)

add_library(abe_proto_client STATIC
    ${ABE_CLIENT_PROTO_SRCS}
    ${ABE_CLIENT_PROTO_HDRS}
)

set_target_properties(abe_proto_client
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_proto_client
    PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
)

if(TARGET protobuf::libprotobuf)
    target_link_libraries(abe_proto_client
        PUBLIC
            protobuf::libprotobuf
    )
else()
    target_include_directories(abe_proto_client
        PUBLIC
            ${Protobuf_INCLUDE_DIRS}
    )
    target_link_libraries(abe_proto_client
        PUBLIC
            ${Protobuf_LIBRARIES}
    )
endif()
