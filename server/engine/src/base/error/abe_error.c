#include "abe_error.h"

const char* abe_status_name(int status)
{
    switch (status) {
    case ABE_OK:
        return "ABE_OK";
    case ABE_ERROR:
        return "ABE_ERROR";
    case ABE_INVALID_ARG:
        return "ABE_INVALID_ARG";
    case ABE_NO_MEMORY:
        return "ABE_NO_MEMORY";
    case ABE_NOT_FOUND:
        return "ABE_NOT_FOUND";
    case ABE_ALREADY_EXISTS:
        return "ABE_ALREADY_EXISTS";
    case ABE_NO_SLOT:
        return "ABE_NO_SLOT";
    case ABE_CLOSED:
        return "ABE_CLOSED";
    case ABE_TIMEOUT:
        return "ABE_TIMEOUT";
    case ABE_PARSE_ERROR:
        return "ABE_PARSE_ERROR";
    case ABE_TYPE_MISMATCH:
        return "ABE_TYPE_MISMATCH";
    case ABE_CONNECT_FAILED:
        return "ABE_CONNECT_FAILED";
    case ABE_COMMAND_FAILED:
        return "ABE_COMMAND_FAILED";
    case ABE_OUT_OF_RANGE:
        return "ABE_OUT_OF_RANGE";
    case ABE_BUFFER_TOO_SMALL:
        return "ABE_BUFFER_TOO_SMALL";
    case ABE_INVALID_MAGIC:
        return "ABE_INVALID_MAGIC";
    case ABE_INVALID_VERSION:
        return "ABE_INVALID_VERSION";
    case ABE_INVALID_LENGTH:
        return "ABE_INVALID_LENGTH";
    case ABE_NOT_CONNECTED:
        return "ABE_NOT_CONNECTED";
    case ABE_QUERY_FAILED:
        return "ABE_QUERY_FAILED";
    case ABE_NO_ROW:
        return "ABE_NO_ROW";
    case ABE_BAD_VALUE:
        return "ABE_BAD_VALUE";
    case ABE_UNSUPPORTED:
        return "ABE_UNSUPPORTED";
    case ABE_SYSTEM_ERROR:
        return "ABE_SYSTEM_ERROR";
    case ABE_OVERFLOW:
        return "ABE_OVERFLOW";
    case ABE_LIMIT:
        return "ABE_LIMIT";
    case ABE_WOULD_BLOCK:
        return "ABE_WOULD_BLOCK";
    case ABE_PACKET_TOO_LARGE:
        return "ABE_PACKET_TOO_LARGE";
    case ABE_DOUBLE_FREE:
        return "ABE_DOUBLE_FREE";
    case ABE_SEND_FAILED:
        return "ABE_SEND_FAILED";
    default:
        return "ABE_UNKNOWN_STATUS";
    }
}
