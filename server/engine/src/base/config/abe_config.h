#ifndef ABE_CONFIG_H
#define ABE_CONFIG_H

#include "abe_error.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abe_config abe_config_t;

typedef enum abe_config_status {
    ABE_CONFIG_OK = ABE_OK,
    ABE_CONFIG_ERROR = ABE_ERROR,
    ABE_CONFIG_INVALID_ARG = ABE_INVALID_ARG,
    ABE_CONFIG_NO_MEMORY = ABE_NO_MEMORY,
    ABE_CONFIG_PARSE_ERROR = ABE_PARSE_ERROR,
    ABE_CONFIG_NOT_FOUND = ABE_NOT_FOUND,
    ABE_CONFIG_TYPE_MISMATCH = ABE_TYPE_MISMATCH
} abe_config_status_t;

typedef enum abe_config_format {
    ABE_CONFIG_FORMAT_JSON = 1,
    ABE_CONFIG_FORMAT_XML = 2
} abe_config_format_t;

typedef enum abe_config_value_type {
    ABE_CONFIG_VALUE_OBJECT = 1,
    ABE_CONFIG_VALUE_ARRAY = 2,
    ABE_CONFIG_VALUE_STRING = 3,
    ABE_CONFIG_VALUE_NUMBER = 4,
    ABE_CONFIG_VALUE_BOOL = 5,
    ABE_CONFIG_VALUE_NULL = 6
} abe_config_value_type_t;

int abe_config_load_json_text(const char* text, abe_config_t** out_config);
int abe_config_load_xml_text(const char* text, abe_config_t** out_config);
int abe_config_load_json_file(const char* path, abe_config_t** out_config);
int abe_config_load_xml_file(const char* path, abe_config_t** out_config);
void abe_config_destroy(abe_config_t* config);

int abe_config_exists(const abe_config_t* config, const char* path);
int abe_config_get_type(
    const abe_config_t* config,
    const char* path,
    abe_config_value_type_t* out_type);
int abe_config_get_string(const abe_config_t* config, const char* path, const char** out_value);
int abe_config_get_i64(const abe_config_t* config, const char* path, int64_t* out_value);
int abe_config_get_u64(const abe_config_t* config, const char* path, uint64_t* out_value);
int abe_config_get_double(const abe_config_t* config, const char* path, double* out_value);
int abe_config_get_bool(const abe_config_t* config, const char* path, int* out_value);

#ifdef __cplusplus
}
#endif

#endif /* ABE_CONFIG_H */
