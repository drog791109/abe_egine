#ifndef ABE_PROTOCOL_H
#define ABE_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ABE_MSG_MAGIC = 0xabe1u,
    ABE_MSG_VERSION = 1u,
    ABE_MSG_HEADER_SIZE = 80u
};

enum abe_protocol_status {
    ABE_PROTOCOL_OK = 0,
    ABE_PROTOCOL_INVALID_ARG = -1,
    ABE_PROTOCOL_BUFFER_TOO_SMALL = -2,
    ABE_PROTOCOL_INVALID_MAGIC = -3,
    ABE_PROTOCOL_INVALID_VERSION = -4,
    ABE_PROTOCOL_INVALID_LENGTH = -5
};

typedef struct abe_msg_header {
    uint16_t magic;
    uint16_t version;
    uint32_t packet_length;
    uint32_t msg_id;
    uint64_t session_id;
    uint64_t role_id;
    uint64_t player_id;
    uint32_t seq;
    uint32_t rpc_id;
    uint64_t trace_id;
    uint32_t source_server;
    uint32_t target_server;
    uint32_t route_type;
    uint32_t flags;
    uint64_t timestamp;
    uint32_t body_length;
} abe_msg_header_t;

typedef struct abe_msg_packet_view {
    abe_msg_header_t header;
    const void* body;
    uint32_t body_size;
} abe_msg_packet_view_t;

void abe_msg_header_init(abe_msg_header_t* header);
uint32_t abe_msg_header_size(void);
int abe_msg_packet_get_size(uint32_t body_size, uint32_t* out_packet_size);

int abe_msg_header_encode(
    const abe_msg_header_t* header,
    void* buffer,
    uint32_t buffer_size);

int abe_msg_header_decode(
    const void* buffer,
    uint32_t buffer_size,
    abe_msg_header_t* out_header);

int abe_msg_packet_encode(
    const abe_msg_header_t* header,
    const void* body,
    uint32_t body_size,
    void* out_packet,
    uint32_t out_packet_size,
    uint32_t* out_written_size);

int abe_msg_packet_decode(
    const void* packet,
    uint32_t packet_size,
    abe_msg_packet_view_t* out_packet);

#ifdef __cplusplus
}
#endif

#endif /* ABE_PROTOCOL_H */
