#ifndef ABE_ERROR_H
#define ABE_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum abe_status {
    ABE_OK = 0,
    ABE_ERROR = -1,
    ABE_INVALID_ARG = -2,
    ABE_NO_MEMORY = -3,
    ABE_NOT_FOUND = -4,
    ABE_ALREADY_EXISTS = -5,
    ABE_NO_SLOT = -6,
    ABE_CLOSED = -7,
    ABE_TIMEOUT = -8,
    ABE_PARSE_ERROR = -9,
    ABE_TYPE_MISMATCH = -10,
    ABE_CONNECT_FAILED = -11,
    ABE_COMMAND_FAILED = -12,
    ABE_OUT_OF_RANGE = -13,
    ABE_BUFFER_TOO_SMALL = -14,
    ABE_INVALID_MAGIC = -15,
    ABE_INVALID_VERSION = -16,
    ABE_INVALID_LENGTH = -17,
    ABE_NOT_CONNECTED = -18,
    ABE_QUERY_FAILED = -19,
    ABE_NO_ROW = -20,
    ABE_BAD_VALUE = -21,
    ABE_UNSUPPORTED = -22,
    ABE_SYSTEM_ERROR = -23,
    ABE_OVERFLOW = -24,
    ABE_LIMIT = -25,
    ABE_WOULD_BLOCK = -26,
    ABE_PACKET_TOO_LARGE = -27,
    ABE_DOUBLE_FREE = -28,
    ABE_SEND_FAILED = -29
} abe_status_t;

const char* abe_status_name(int status);

#ifdef __cplusplus
}
#endif

#endif /* ABE_ERROR_H */
