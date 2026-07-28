#ifndef ABE_SERVICE_ARGS_H
#define ABE_SERVICE_ARGS_H

#include "abe_error.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace common {

enum ServiceArgStatus {
    SERVICE_ARG_OK = ABE_OK,
    SERVICE_ARG_HELP = 1,
    SERVICE_ARG_INVALID_ARG = ABE_INVALID_ARG,
    SERVICE_ARG_UNKNOWN_OPTION = ABE_NOT_FOUND,
    SERVICE_ARG_MISSING_VALUE = ABE_PARSE_ERROR,
    SERVICE_ARG_INVALID_VALUE = ABE_BAD_VALUE
};

enum ServiceOptionType {
    SERVICE_OPTION_STRING = 1,
    SERVICE_OPTION_U32 = 2,
    SERVICE_OPTION_U64 = 3,
    SERVICE_OPTION_I32 = 4
};

struct ServiceOption {
    const char* name;
    const char* value_name;
    const char* description;
    ServiceOptionType type;
    void* out_value;
    uint64_t min_value;
    uint64_t max_value;
    int64_t signed_min_value;
    int64_t signed_max_value;
};

int service_parse_u32(
    const char* text,
    uint32_t min_value,
    uint32_t max_value,
    uint32_t* out_value);

int service_parse_u64(
    const char* text,
    uint64_t min_value,
    uint64_t max_value,
    uint64_t* out_value);

int service_parse_i32(
    const char* text,
    int32_t min_value,
    int32_t max_value,
    int32_t* out_value);

int service_parse_options(
    int argc,
    char** argv,
    const ServiceOption* options,
    uint32_t option_count);

void service_log_usage(
    const char* program,
    const ServiceOption* options,
    uint32_t option_count);

} /* namespace common */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_ARGS_H */
