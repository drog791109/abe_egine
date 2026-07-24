#include "abe_gateway_protocol.h"

#include "abe_session.h"

#include <stddef.h>

namespace abe {
namespace service {
namespace gateway {

static int protocol_status_to_session_status(int status)
{
    if (status == ABE_PROTOCOL_OK) {
        return abe::logic::session::SESSION_STATUS_OK;
    }
    return abe::logic::session::SESSION_STATUS_INVALID_ARG;
}

int decode_client_msg_packet(
    const void* packet,
    uint32_t packet_size,
    uint32_t* out_msg_id,
    const void** out_payload,
    uint32_t* out_payload_size,
    void* user_data)
{
    abe_msg_packet_view_t view;
    int rc;

    (void)user_data;
    if (out_msg_id == NULL || out_payload == NULL || out_payload_size == NULL) {
        return abe::logic::session::SESSION_STATUS_INVALID_ARG;
    }

    rc = abe_msg_packet_decode(packet, packet_size, &view);
    if (rc != ABE_PROTOCOL_OK) {
        return protocol_status_to_session_status(rc);
    }
    if (view.header.msg_id == 0u) {
        return abe::logic::session::SESSION_STATUS_INVALID_ARG;
    }

    *out_msg_id = view.header.msg_id;
    *out_payload = view.body;
    *out_payload_size = view.body_size;
    return abe::logic::session::SESSION_STATUS_OK;
}

int encode_client_msg_packet(
    const abe_msg_header_t* header,
    const void* body,
    uint32_t body_size,
    void* out_packet,
    uint32_t out_packet_size,
    uint32_t* out_written_size)
{
    int rc;

    rc = abe_msg_packet_encode(
        header,
        body,
        body_size,
        out_packet,
        out_packet_size,
        out_written_size);
    return protocol_status_to_session_status(rc);
}

} /* namespace gateway */
} /* namespace service */
} /* namespace abe */
