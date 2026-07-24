#ifndef ABE_SERVICE_GATEWAY_PROTOCOL_H
#define ABE_SERVICE_GATEWAY_PROTOCOL_H

#include "abe_protocol.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace gateway {

int decode_client_msg_packet(
    const void* packet,
    uint32_t packet_size,
    uint32_t* out_msg_id,
    const void** out_payload,
    uint32_t* out_payload_size,
    void* user_data);

int encode_client_msg_packet(
    const abe_msg_header_t* header,
    const void* body,
    uint32_t body_size,
    void* out_packet,
    uint32_t out_packet_size,
    uint32_t* out_written_size);

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GATEWAY_PROTOCOL_H */
