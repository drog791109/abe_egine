#ifndef ABE_SERVICE_GATEWAY_MESSAGE_H
#define ABE_SERVICE_GATEWAY_MESSAGE_H

#include "abe_protocol.h"

#include <stdint.h>

namespace google {
namespace protobuf {
class Arena;
class Message;
}
}

namespace abe {
namespace service {
namespace gateway {

int get_client_msg_id_from_name(const char* enum_name, uint32_t* out_msg_id);
const char* get_client_message_name(uint32_t msg_id);

int get_client_msg_id_from_message(
    const google::protobuf::Message& message,
    uint32_t* out_msg_id);

google::protobuf::Message* create_client_message(
    uint32_t msg_id,
    google::protobuf::Arena* arena);

int parse_client_message_body(
    uint32_t msg_id,
    const void* body,
    uint32_t body_size,
    google::protobuf::Arena* arena,
    google::protobuf::Message** out_message);

int decode_client_message_packet(
    const void* packet,
    uint32_t packet_size,
    google::protobuf::Arena* arena,
    abe_msg_header_t* out_header,
    google::protobuf::Message** out_message);

int encode_client_message_packet(
    const abe_msg_header_t* header,
    const google::protobuf::Message& message,
    void* out_packet,
    uint32_t out_packet_size,
    uint32_t* out_written_size);

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GATEWAY_MESSAGE_H */
