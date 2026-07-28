#include "abe_service_args.h"

#include "abe_log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

namespace abe {
namespace service {
namespace common {

static int parse_unsigned_long_long(
    const char* text,
    uint64_t min_value,
    uint64_t max_value,
    uint64_t* out_value)
{
    char* end;
    unsigned long long value;

    if (text == NULL || out_value == NULL || min_value > max_value) {
        return SERVICE_ARG_INVALID_ARG;
    }

    errno = 0;
    end = NULL;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0') {
        return SERVICE_ARG_INVALID_VALUE;
    }
    if ((uint64_t)value < min_value || (uint64_t)value > max_value) {
        return SERVICE_ARG_INVALID_VALUE;
    }

    *out_value = (uint64_t)value;
    return SERVICE_ARG_OK;
}

static int parse_signed_long_long(
    const char* text,
    int64_t min_value,
    int64_t max_value,
    int64_t* out_value)
{
    char* end;
    long long value;

    if (text == NULL || out_value == NULL || min_value > max_value) {
        return SERVICE_ARG_INVALID_ARG;
    }

    errno = 0;
    end = NULL;
    value = strtoll(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0') {
        return SERVICE_ARG_INVALID_VALUE;
    }
    if ((int64_t)value < min_value || (int64_t)value > max_value) {
        return SERVICE_ARG_INVALID_VALUE;
    }

    *out_value = (int64_t)value;
    return SERVICE_ARG_OK;
}

static const ServiceOption* find_option(
    const ServiceOption* options,
    uint32_t option_count,
    const char* name)
{
    uint32_t index;

    if (options == NULL || name == NULL) {
        return NULL;
    }

    index = 0u;
    while (index < option_count) {
        if (options[index].name != NULL && strcmp(options[index].name, name) == 0) {
            return &options[index];
        }
        ++index;
    }
    return NULL;
}

static int parse_option_value(const ServiceOption* option, const char* text)
{
    uint64_t value;
    int64_t signed_value;
    int rc;

    if (option == NULL || text == NULL || option->out_value == NULL) {
        return SERVICE_ARG_INVALID_ARG;
    }

    if (option->type == SERVICE_OPTION_STRING) {
        *(const char**)option->out_value = text;
        return SERVICE_ARG_OK;
    }

    if (option->type == SERVICE_OPTION_I32) {
        rc = parse_signed_long_long(
            text,
            option->signed_min_value,
            option->signed_max_value,
            &signed_value);
        if (rc != SERVICE_ARG_OK) {
            return rc;
        }
        *(int32_t*)option->out_value = (int32_t)signed_value;
        return SERVICE_ARG_OK;
    }

    rc = parse_unsigned_long_long(text, option->min_value, option->max_value, &value);
    if (rc != SERVICE_ARG_OK) {
        return rc;
    }

    if (option->type == SERVICE_OPTION_U32) {
        *(uint32_t*)option->out_value = (uint32_t)value;
        return SERVICE_ARG_OK;
    }
    if (option->type == SERVICE_OPTION_U64) {
        *(uint64_t*)option->out_value = value;
        return SERVICE_ARG_OK;
    }
    return SERVICE_ARG_INVALID_ARG;
}

int service_parse_u32(
    const char* text,
    uint32_t min_value,
    uint32_t max_value,
    uint32_t* out_value)
{
    uint64_t value;
    int rc;

    rc = parse_unsigned_long_long(text, min_value, max_value, &value);
    if (rc != SERVICE_ARG_OK) {
        return rc;
    }

    *out_value = (uint32_t)value;
    return SERVICE_ARG_OK;
}

int service_parse_u64(
    const char* text,
    uint64_t min_value,
    uint64_t max_value,
    uint64_t* out_value)
{
    return parse_unsigned_long_long(text, min_value, max_value, out_value);
}

int service_parse_i32(
    const char* text,
    int32_t min_value,
    int32_t max_value,
    int32_t* out_value)
{
    int64_t value;
    int rc;

    rc = parse_signed_long_long(text, min_value, max_value, &value);
    if (rc != SERVICE_ARG_OK) {
        return rc;
    }

    *out_value = (int32_t)value;
    return SERVICE_ARG_OK;
}

int service_parse_options(
    int argc,
    char** argv,
    const ServiceOption* options,
    uint32_t option_count)
{
    int index;

    if (argc < 0 || argv == NULL || (option_count != 0u && options == NULL)) {
        return SERVICE_ARG_INVALID_ARG;
    }

    index = 1;
    while (index < argc) {
        const ServiceOption* option;
        const char* name;
        int rc;

        name = argv[index];
        if (name == NULL) {
            return SERVICE_ARG_INVALID_ARG;
        }
        if (strcmp(name, "--help") == 0) {
            return SERVICE_ARG_HELP;
        }

        option = find_option(options, option_count, name);
        if (option == NULL) {
            ABE_LOG_ERROR(
                "unknown service option name=%s status=%s",
                name,
                abe_status_name(SERVICE_ARG_UNKNOWN_OPTION));
            return SERVICE_ARG_UNKNOWN_OPTION;
        }
        if (index + 1 >= argc) {
            ABE_LOG_ERROR(
                "missing service option value name=%s status=%s",
                name,
                abe_status_name(SERVICE_ARG_MISSING_VALUE));
            return SERVICE_ARG_MISSING_VALUE;
        }

        rc = parse_option_value(option, argv[index + 1]);
        if (rc != SERVICE_ARG_OK) {
            ABE_LOG_ERROR(
                "invalid service option value name=%s status=%s",
                name,
                abe_status_name(rc));
            return rc;
        }
        index += 2;
    }

    return SERVICE_ARG_OK;
}

void service_log_usage(
    const char* program,
    const ServiceOption* options,
    uint32_t option_count)
{
    uint32_t index;

    ABE_LOG_INFO("usage: %s [options]", program == NULL ? "service" : program);
    index = 0u;
    while (index < option_count) {
        const ServiceOption* option;

        option = &options[index];
        if (option->name != NULL) {
            ABE_LOG_INFO(
                "option: %s <%s> %s",
                option->name,
                option->value_name == NULL ? "value" : option->value_name,
                option->description == NULL ? "" : option->description);
        }
        ++index;
    }
    ABE_LOG_INFO("option: --help show this help");
}

} /* namespace common */
} /* namespace service */
} /* namespace abe */
