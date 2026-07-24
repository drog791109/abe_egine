#include "abe_protocol.h"

#include <stddef.h>
#include <string.h>

static void abe_protocol_write_u16(unsigned char* out, uint16_t value)
{
    out[0] = (unsigned char)((value >> 8) & 0xffu);
    out[1] = (unsigned char)(value & 0xffu);
}

static void abe_protocol_write_u32(unsigned char* out, uint32_t value)
{
    out[0] = (unsigned char)((value >> 24) & 0xffu);
    out[1] = (unsigned char)((value >> 16) & 0xffu);
    out[2] = (unsigned char)((value >> 8) & 0xffu);
    out[3] = (unsigned char)(value & 0xffu);
}

static void abe_protocol_write_u64(unsigned char* out, uint64_t value)
{
    out[0] = (unsigned char)((value >> 56) & 0xffu);
    out[1] = (unsigned char)((value >> 48) & 0xffu);
    out[2] = (unsigned char)((value >> 40) & 0xffu);
    out[3] = (unsigned char)((value >> 32) & 0xffu);
    out[4] = (unsigned char)((value >> 24) & 0xffu);
    out[5] = (unsigned char)((value >> 16) & 0xffu);
    out[6] = (unsigned char)((value >> 8) & 0xffu);
    out[7] = (unsigned char)(value & 0xffu);
}

static uint16_t abe_protocol_read_u16(const unsigned char* in)
{
    return (uint16_t)(((uint16_t)in[0] << 8) | (uint16_t)in[1]);
}

static uint32_t abe_protocol_read_u32(const unsigned char* in)
{
    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
}

static uint64_t abe_protocol_read_u64(const unsigned char* in)
{
    return ((uint64_t)in[0] << 56) |
           ((uint64_t)in[1] << 48) |
           ((uint64_t)in[2] << 40) |
           ((uint64_t)in[3] << 32) |
           ((uint64_t)in[4] << 24) |
           ((uint64_t)in[5] << 16) |
           ((uint64_t)in[6] << 8) |
           (uint64_t)in[7];
}

void abe_msg_header_init(abe_msg_header_t* header)
{
    if (header == NULL) {
        return;
    }

    memset(header, 0, sizeof(*header));
    header->magic = (uint16_t)ABE_MSG_MAGIC;
    header->version = (uint16_t)ABE_MSG_VERSION;
    header->packet_length = ABE_MSG_HEADER_SIZE;
}

uint32_t abe_msg_header_size(void)
{
    return ABE_MSG_HEADER_SIZE;
}

int abe_msg_packet_get_size(uint32_t body_size, uint32_t* out_packet_size)
{
    if (out_packet_size == NULL) {
        return ABE_PROTOCOL_INVALID_ARG;
    }
    if (body_size > 0xffffffffu - ABE_MSG_HEADER_SIZE) {
        return ABE_PROTOCOL_INVALID_LENGTH;
    }

    *out_packet_size = ABE_MSG_HEADER_SIZE + body_size;
    return ABE_PROTOCOL_OK;
}

int abe_msg_header_encode(
    const abe_msg_header_t* header,
    void* buffer,
    uint32_t buffer_size)
{
    unsigned char* out;
    uint32_t offset;

    if (header == NULL || buffer == NULL) {
        return ABE_PROTOCOL_INVALID_ARG;
    }
    if (buffer_size < ABE_MSG_HEADER_SIZE) {
        return ABE_PROTOCOL_BUFFER_TOO_SMALL;
    }

    out = (unsigned char*)buffer;
    offset = 0u;

    abe_protocol_write_u16(out + offset, header->magic);
    offset += 2u;
    abe_protocol_write_u16(out + offset, header->version);
    offset += 2u;
    abe_protocol_write_u32(out + offset, header->packet_length);
    offset += 4u;
    abe_protocol_write_u32(out + offset, header->msg_id);
    offset += 4u;
    abe_protocol_write_u64(out + offset, header->session_id);
    offset += 8u;
    abe_protocol_write_u64(out + offset, header->role_id);
    offset += 8u;
    abe_protocol_write_u64(out + offset, header->player_id);
    offset += 8u;
    abe_protocol_write_u32(out + offset, header->seq);
    offset += 4u;
    abe_protocol_write_u32(out + offset, header->rpc_id);
    offset += 4u;
    abe_protocol_write_u64(out + offset, header->trace_id);
    offset += 8u;
    abe_protocol_write_u32(out + offset, header->source_server);
    offset += 4u;
    abe_protocol_write_u32(out + offset, header->target_server);
    offset += 4u;
    abe_protocol_write_u32(out + offset, header->route_type);
    offset += 4u;
    abe_protocol_write_u32(out + offset, header->flags);
    offset += 4u;
    abe_protocol_write_u64(out + offset, header->timestamp);
    offset += 8u;
    abe_protocol_write_u32(out + offset, header->body_length);

    return ABE_PROTOCOL_OK;
}

int abe_msg_header_decode(
    const void* buffer,
    uint32_t buffer_size,
    abe_msg_header_t* out_header)
{
    const unsigned char* in;
    uint32_t offset;

    if (buffer == NULL || out_header == NULL) {
        return ABE_PROTOCOL_INVALID_ARG;
    }
    if (buffer_size < ABE_MSG_HEADER_SIZE) {
        return ABE_PROTOCOL_BUFFER_TOO_SMALL;
    }

    in = (const unsigned char*)buffer;
    offset = 0u;
    memset(out_header, 0, sizeof(*out_header));

    out_header->magic = abe_protocol_read_u16(in + offset);
    offset += 2u;
    out_header->version = abe_protocol_read_u16(in + offset);
    offset += 2u;
    out_header->packet_length = abe_protocol_read_u32(in + offset);
    offset += 4u;
    out_header->msg_id = abe_protocol_read_u32(in + offset);
    offset += 4u;
    out_header->session_id = abe_protocol_read_u64(in + offset);
    offset += 8u;
    out_header->role_id = abe_protocol_read_u64(in + offset);
    offset += 8u;
    out_header->player_id = abe_protocol_read_u64(in + offset);
    offset += 8u;
    out_header->seq = abe_protocol_read_u32(in + offset);
    offset += 4u;
    out_header->rpc_id = abe_protocol_read_u32(in + offset);
    offset += 4u;
    out_header->trace_id = abe_protocol_read_u64(in + offset);
    offset += 8u;
    out_header->source_server = abe_protocol_read_u32(in + offset);
    offset += 4u;
    out_header->target_server = abe_protocol_read_u32(in + offset);
    offset += 4u;
    out_header->route_type = abe_protocol_read_u32(in + offset);
    offset += 4u;
    out_header->flags = abe_protocol_read_u32(in + offset);
    offset += 4u;
    out_header->timestamp = abe_protocol_read_u64(in + offset);
    offset += 8u;
    out_header->body_length = abe_protocol_read_u32(in + offset);

    return ABE_PROTOCOL_OK;
}

int abe_msg_packet_encode(
    const abe_msg_header_t* header,
    const void* body,
    uint32_t body_size,
    void* out_packet,
    uint32_t out_packet_size,
    uint32_t* out_written_size)
{
    abe_msg_header_t encoded_header;
    uint32_t packet_size;
    int rc;

    if (header == NULL || out_packet == NULL || out_written_size == NULL) {
        return ABE_PROTOCOL_INVALID_ARG;
    }
    if (body_size != 0u && body == NULL) {
        return ABE_PROTOCOL_INVALID_ARG;
    }
    if (header->magic != ABE_MSG_MAGIC) {
        return ABE_PROTOCOL_INVALID_MAGIC;
    }
    if (header->version != ABE_MSG_VERSION) {
        return ABE_PROTOCOL_INVALID_VERSION;
    }

    rc = abe_msg_packet_get_size(body_size, &packet_size);
    if (rc != ABE_PROTOCOL_OK) {
        return rc;
    }
    if (out_packet_size < packet_size) {
        return ABE_PROTOCOL_BUFFER_TOO_SMALL;
    }

    encoded_header = *header;
    encoded_header.packet_length = packet_size;
    encoded_header.body_length = body_size;

    rc = abe_msg_header_encode(&encoded_header, out_packet, out_packet_size);
    if (rc != ABE_PROTOCOL_OK) {
        return rc;
    }
    if (body_size != 0u) {
        memcpy((unsigned char*)out_packet + ABE_MSG_HEADER_SIZE, body, body_size);
    }

    *out_written_size = packet_size;
    return ABE_PROTOCOL_OK;
}

int abe_msg_packet_decode(
    const void* packet,
    uint32_t packet_size,
    abe_msg_packet_view_t* out_packet)
{
    const unsigned char* bytes;
    int rc;

    if (packet == NULL || out_packet == NULL) {
        return ABE_PROTOCOL_INVALID_ARG;
    }
    if (packet_size < ABE_MSG_HEADER_SIZE) {
        return ABE_PROTOCOL_BUFFER_TOO_SMALL;
    }

    memset(out_packet, 0, sizeof(*out_packet));
    rc = abe_msg_header_decode(packet, packet_size, &out_packet->header);
    if (rc != ABE_PROTOCOL_OK) {
        return rc;
    }
    if (out_packet->header.magic != ABE_MSG_MAGIC) {
        return ABE_PROTOCOL_INVALID_MAGIC;
    }
    if (out_packet->header.version != ABE_MSG_VERSION) {
        return ABE_PROTOCOL_INVALID_VERSION;
    }
    if (out_packet->header.packet_length != packet_size ||
        out_packet->header.packet_length < ABE_MSG_HEADER_SIZE ||
        out_packet->header.body_length != packet_size - ABE_MSG_HEADER_SIZE) {
        return ABE_PROTOCOL_INVALID_LENGTH;
    }

    bytes = (const unsigned char*)packet;
    out_packet->body = bytes + ABE_MSG_HEADER_SIZE;
    out_packet->body_size = out_packet->header.body_length;
    return ABE_PROTOCOL_OK;
}
