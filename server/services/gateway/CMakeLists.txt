find_package(Protobuf REQUIRED)

set(ABE_GATEWAY_CLIENT_PROTO_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/../../share/proto/client)
set(ABE_GATEWAY_CLIENT_PROTO_FILE
    ${ABE_GATEWAY_CLIENT_PROTO_DIR}/protocol.proto)
set(ABE_GATEWAY_PROTO_OUT_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(ABE_GATEWAY_PROTO_SRCS
    ${ABE_GATEWAY_PROTO_OUT_DIR}/protocol.pb.cc)
set(ABE_GATEWAY_PROTO_HDRS
    ${ABE_GATEWAY_PROTO_OUT_DIR}/protocol.pb.h)

file(MAKE_DIRECTORY ${ABE_GATEWAY_PROTO_OUT_DIR})

add_custom_command(
    OUTPUT
        ${ABE_GATEWAY_PROTO_SRCS}
        ${ABE_GATEWAY_PROTO_HDRS}
    COMMAND
        ${Protobuf_PROTOC_EXECUTABLE}
    ARGS
        --cpp_out=${ABE_GATEWAY_PROTO_OUT_DIR}
        -I ${ABE_GATEWAY_CLIENT_PROTO_DIR}
        ${ABE_GATEWAY_CLIENT_PROTO_FILE}
    DEPENDS
        ${ABE_GATEWAY_CLIENT_PROTO_FILE}
    COMMENT
        "Generating client protocol protobuf sources"
    VERBATIM
)

add_library(abe_service_gateway_proto STATIC
    ${ABE_GATEWAY_PROTO_SRCS}
)

set_target_properties(abe_service_gateway_proto
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_service_gateway_proto
    PUBLIC
        ${ABE_GATEWAY_PROTO_OUT_DIR}
        ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(abe_service_gateway_proto
    PUBLIC
        ${Protobuf_LIBRARIES}
)

add_library(abe_service_gateway STATIC
    abe_gateway_service.cpp
    abe_gateway_session.cpp
    abe_gateway_protocol.cpp
    abe_gateway_message.cpp
)

set_target_properties(abe_service_gateway
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_service_gateway
    PUBLIC
        abe_adapter_net
        abe_logic_session
        abe_common_protocol
        abe_base_time
        abe_service_gateway_proto
)

target_include_directories(abe_service_gateway
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(abe_gateway_app STATIC
    abe_gateway_app.cpp
)

set_target_properties(abe_gateway_app
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_gateway_app
    PUBLIC
        abe_service_gateway
)

target_include_directories(abe_gateway_app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(abe_gateway
    abe_gateway_main.cpp
)

set_target_properties(abe_gateway
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_gateway
    PRIVATE
        abe_gateway_app
)
