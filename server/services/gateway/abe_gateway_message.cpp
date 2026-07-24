#include "abe_gateway_message.h"

#include "protocol.pb.h"

#include <google/protobuf/arena.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <string>

namespace abe {
namespace service {
namespace gateway {

enum {
    GATEWAY_MESSAGE_NAME_MAX = 128u
};

static int build_message_full_name(uint32_t msg_id, char* out_name, uint32_t out_name_size)
{
    abe::proto::client::ProtocolId protocol_id;
    const std::string* enum_name;
    int required_size;

    if (out_name == NULL || out_name_size == 0u) {
        return -1;
    }
    if (!abe::proto::client::ProtocolId_IsValid((int)msg_id)) {
        return -1;
    }

    protocol_id = (abe::proto::client::ProtocolId)msg_id;
    enum_name = &abe::proto::client::ProtocolId_Name(protocol_id);
    if (enum_name->empty()) {
        return -1;
    }

    required_size = snprintf(
        out_name,
        out_name_size,
        "abe.proto.client.PB_%s",
        enum_name->c_str());
    if (required_size < 0 || (uint32_t)required_size >= out_name_size) {
        return -1;
    }
    return 0;
}

static const google::protobuf::Descriptor* find_message_descriptor(uint32_t msg_id)
{
    char full_name[GATEWAY_MESSAGE_NAME_MAX];

    if (build_message_full_name(msg_id, full_name, sizeof(full_name)) != 0) {
        return NULL;
    }
    return google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(full_name);
}

int get_client_msg_id_from_name(const char* enum_name, uint32_t* out_msg_id)
{
    abe::proto::client::ProtocolId protocol_id;

    if (enum_name == NULL || out_msg_id == NULL) {
        return -1;
    }
    if (!abe::proto::client::ProtocolId_Parse(enum_name, &protocol_id)) {
        return -1;
    }
    if (protocol_id == abe::proto::client::MSG_ID_INVALID) {
        return -1;
    }

    *out_msg_id = (uint32_t)protocol_id;
    return 0;
}

const char* get_client_message_name(uint32_t msg_id)
{
    const google::protobuf::Descriptor* descriptor;

    descriptor = find_message_descriptor(msg_id);
    if (descriptor == NULL) {
        return NULL;
    }
    return descriptor->full_name().c_str();
}

int get_client_msg_id_from_message(
    const google::protobuf::Message& message,
    uint32_t* out_msg_id)
{
    const google::protobuf::Descriptor* descriptor;
    const char* name;

    if (out_msg_id == NULL) {
        return -1;
    }

    descriptor = message.GetDescriptor();
    if (descriptor == NULL) {
        return -1;
    }

    name = descriptor->name().c_str();
    if (strncmp(name, "PB_", 3u) != 0) {
        return -1;
    }

    return get_client_msg_id_from_name(name + 3, out_msg_id);
}

google::protobuf::Message* create_client_message(
    uint32_t msg_id,
    google::protobuf::Arena* arena)
{
    const google::protobuf::Descriptor* descriptor;
    const google::protobuf::Message* prototype;

    if (arena == NULL) {
        return NULL;
    }

    descriptor = find_message_descriptor(msg_id);
    if (descriptor == NULL) {
        return NULL;
    }

    prototype = google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor);
    if (prototype == NULL) {
        return NULL;
    }
    return prototype->New(arena);
}

int parse_client_message_body(
    uint32_t msg_id,
    const void* body,
    uint32_t body_size,
    google::protobuf::Arena* arena,
    google::protobuf::Message** out_message)
{
    google::protobuf::Message* message;

    if (out_message == NULL || arena == NULL) {
        return -1;
    }
    if (body == NULL && body_size != 0u) {
        return -1;
    }

    message = create_client_message(msg_id, arena);
    if (message == NULL) {
        return -1;
    }
    if (!message->ParseFromArray(body, (int)body_size)) {
        *out_message = NULL;
        return -1;
    }

    *out_message = message;
    return 0;
}

int decode_client_message_packet(
    const void* packet,
    uint32_t packet_size,
    google::protobuf::Arena* arena,
    abe_msg_header_t* out_header,
    google::protobuf::Message** out_message)
{
    abe_msg_packet_view_t view;
    int rc;

    if (arena == NULL || out_header == NULL || out_message == NULL) {
        return -1;
    }

    rc = abe_msg_packet_decode(packet, packet_size, &view);
    if (rc != ABE_PROTOCOL_OK) {
        *out_message = NULL;
        return -1;
    }

    *out_header = view.header;
    return parse_client_message_body(
        view.header.msg_id,
        view.body,
        view.body_size,
        arena,
        out_message);
}

int encode_client_message_packet(
    const abe_msg_header_t* header,
    const google::protobuf::Message& message,
    void* out_packet,
    uint32_t out_packet_size,
    uint32_t* out_written_size)
{
    abe_msg_header_t encoded_header;
    uint32_t msg_id;
    uint32_t packet_size;
    uint64_t body_size_long;
    uint32_t body_size;
    unsigned char* bytes;
    int rc;

    if (header == NULL || out_packet == NULL || out_written_size == NULL) {
        return -1;
    }
    if (get_client_msg_id_from_message(message, &msg_id) != 0) {
        return -1;
    }
    if (header->msg_id != 0u && header->msg_id != msg_id) {
        return -1;
    }
    if (header->magic != ABE_MSG_MAGIC || header->version != ABE_MSG_VERSION) {
        return -1;
    }

    body_size_long = (uint64_t)message.ByteSizeLong();
    if (body_size_long > 0xffffffffu - ABE_MSG_HEADER_SIZE) {
        return -1;
    }
    body_size = (uint32_t)body_size_long;

    rc = abe_msg_packet_get_size(body_size, &packet_size);
    if (rc != ABE_PROTOCOL_OK || out_packet_size < packet_size) {
        return -1;
    }

    encoded_header = *header;
    encoded_header.msg_id = msg_id;
    encoded_header.packet_length = packet_size;
    encoded_header.body_length = body_size;

    rc = abe_msg_header_encode(&encoded_header, out_packet, out_packet_size);
    if (rc != ABE_PROTOCOL_OK) {
        return -1;
    }

    bytes = (unsigned char*)out_packet;
    if (!message.SerializeToArray(bytes + ABE_MSG_HEADER_SIZE, (int)body_size)) {
        return -1;
    }

    *out_written_size = packet_size;
    return 0;
}

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */
