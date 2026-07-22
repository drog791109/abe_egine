#include "abe_config.h"
#include "abe_mem_pool.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ABE_CONFIG_DEFAULT_EXTRA_BYTES 65536u
#define ABE_CONFIG_PATH_TOKEN_SIZE 128u

typedef struct abe_config_node abe_config_node_t;

struct abe_config_node {
    char* key;
    char* value;
    abe_config_value_type_t type;
    abe_config_node_t* parent;
    abe_config_node_t* first_child;
    abe_config_node_t* last_child;
    abe_config_node_t* next;
};

struct abe_config {
    abe_mem_pool_t* mem_pool;
    abe_config_format_t format;
    char* text;
    abe_config_node_t* root;
};

struct abe_config_parser {
    abe_config_t* config;
    const char* cur;
};

static uint64_t abe_config_strlen_u64(const char* text)
{
    return text == NULL ? 0u : (uint64_t)strlen(text);
}

static void* abe_config_alloc(abe_config_t* config, uint64_t size)
{
    if (config == NULL || config->mem_pool == NULL || size == 0u) {
        return NULL;
    }
    return abe_mem_pool_alloc(config->mem_pool, size);
}

static char* abe_config_copy_range(abe_config_t* config, const char* begin, const char* end)
{
    char* copy;
    uint64_t size;

    if (config == NULL || begin == NULL || end == NULL || end < begin) {
        return NULL;
    }

    size = (uint64_t)(end - begin);
    copy = (char*)abe_config_alloc(config, size + 1u);
    if (copy == NULL) {
        return NULL;
    }
    if (size > 0u) {
        (void)memcpy(copy, begin, (size_t)size);
    }
    copy[size] = '\0';
    return copy;
}

static char* abe_config_copy_cstr(abe_config_t* config, const char* text)
{
    if (text == NULL) {
        return NULL;
    }
    return abe_config_copy_range(config, text, text + strlen(text));
}

static abe_config_node_t* abe_config_node_create(
    abe_config_t* config,
    const char* key,
    abe_config_value_type_t type)
{
    abe_config_node_t* node;

    node = (abe_config_node_t*)abe_mem_pool_calloc(config->mem_pool, 1u, sizeof(*node));
    if (node == NULL) {
        return NULL;
    }
    node->type = type;
    if (key != NULL) {
        node->key = abe_config_copy_cstr(config, key);
        if (node->key == NULL) {
            return NULL;
        }
    }
    return node;
}

static void abe_config_add_child(abe_config_node_t* parent, abe_config_node_t* child)
{
    if (parent == NULL || child == NULL) {
        return;
    }
    child->parent = parent;
    if (parent->last_child == NULL) {
        parent->first_child = child;
        parent->last_child = child;
    } else {
        parent->last_child->next = child;
        parent->last_child = child;
    }
}

static void abe_config_skip_ws(struct abe_config_parser* parser)
{
    while (parser->cur[0] != '\0' && isspace((unsigned char)parser->cur[0])) {
        ++parser->cur;
    }
}

static int abe_config_create_empty(
    abe_config_format_t format,
    uint64_t source_size,
    abe_config_t** out_config)
{
    abe_mem_pool_config_t pool_config;
    abe_mem_pool_t* mem_pool;
    abe_config_t* config;
    uint64_t capacity;

    if (out_config == NULL) {
        return ABE_CONFIG_INVALID_ARG;
    }
    *out_config = NULL;

    if (source_size > (UINT64_MAX - ABE_CONFIG_DEFAULT_EXTRA_BYTES) / 8u) {
        return ABE_CONFIG_INVALID_ARG;
    }
    capacity = ABE_CONFIG_DEFAULT_EXTRA_BYTES + (source_size * 8u);

    memset(&pool_config, 0, sizeof(pool_config));
    pool_config.capacity = capacity;
    pool_config.name = "abe_config";
    mem_pool = NULL;
    if (abe_mem_pool_create(&pool_config, &mem_pool) != ABE_MEM_POOL_OK) {
        return ABE_CONFIG_NO_MEMORY;
    }

    config = (abe_config_t*)abe_mem_pool_calloc(mem_pool, 1u, sizeof(*config));
    if (config == NULL) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_CONFIG_NO_MEMORY;
    }
    config->mem_pool = mem_pool;
    config->format = format;

    *out_config = config;
    return ABE_CONFIG_OK;
}

static char* abe_config_copy_source(abe_config_t* config, const char* text)
{
    uint64_t size;
    char* copy;

    size = abe_config_strlen_u64(text);
    copy = (char*)abe_config_alloc(config, size + 1u);
    if (copy == NULL) {
        return NULL;
    }
    if (size > 0u) {
        (void)memcpy(copy, text, (size_t)size);
    }
    copy[size] = '\0';
    return copy;
}

static char* abe_config_parse_json_string(struct abe_config_parser* parser)
{
    const char* start;
    char* out;
    char* dst;
    uint64_t max_size;

    if (parser->cur[0] != '"') {
        return NULL;
    }
    ++parser->cur;
    start = parser->cur;
    while (parser->cur[0] != '\0' && parser->cur[0] != '"') {
        if (parser->cur[0] == '\\' && parser->cur[1] != '\0') {
            parser->cur += 2;
        } else {
            ++parser->cur;
        }
    }
    if (parser->cur[0] != '"') {
        return NULL;
    }

    max_size = (uint64_t)(parser->cur - start);
    out = (char*)abe_config_alloc(parser->config, max_size + 1u);
    if (out == NULL) {
        return NULL;
    }

    dst = out;
    parser->cur = start;
    while (parser->cur[0] != '\0' && parser->cur[0] != '"') {
        if (parser->cur[0] == '\\') {
            ++parser->cur;
            switch (parser->cur[0]) {
            case '"':
            case '\\':
            case '/':
                *dst = parser->cur[0];
                break;
            case 'b':
                *dst = '\b';
                break;
            case 'f':
                *dst = '\f';
                break;
            case 'n':
                *dst = '\n';
                break;
            case 'r':
                *dst = '\r';
                break;
            case 't':
                *dst = '\t';
                break;
            case 'u':
                *dst = '?';
                parser->cur += 4;
                break;
            default:
                *dst = parser->cur[0];
                break;
            }
            ++dst;
            if (parser->cur[0] != '\0') {
                ++parser->cur;
            }
        } else {
            *dst = parser->cur[0];
            ++dst;
            ++parser->cur;
        }
    }
    *dst = '\0';
    if (parser->cur[0] == '"') {
        ++parser->cur;
    }
    return out;
}

static int abe_config_parse_json_value(
    struct abe_config_parser* parser,
    const char* key,
    abe_config_node_t** out_node);

static int abe_config_parse_json_object(
    struct abe_config_parser* parser,
    const char* key,
    abe_config_node_t** out_node)
{
    abe_config_node_t* object_node;

    if (parser->cur[0] != '{') {
        return ABE_CONFIG_PARSE_ERROR;
    }
    ++parser->cur;

    object_node = abe_config_node_create(parser->config, key, ABE_CONFIG_VALUE_OBJECT);
    if (object_node == NULL) {
        return ABE_CONFIG_NO_MEMORY;
    }

    abe_config_skip_ws(parser);
    if (parser->cur[0] == '}') {
        ++parser->cur;
        *out_node = object_node;
        return ABE_CONFIG_OK;
    }

    for (;;) {
        char* child_key;
        abe_config_node_t* child;
        int rc;

        abe_config_skip_ws(parser);
        child_key = abe_config_parse_json_string(parser);
        if (child_key == NULL) {
            return ABE_CONFIG_PARSE_ERROR;
        }
        abe_config_skip_ws(parser);
        if (parser->cur[0] != ':') {
            return ABE_CONFIG_PARSE_ERROR;
        }
        ++parser->cur;
        rc = abe_config_parse_json_value(parser, child_key, &child);
        if (rc != ABE_CONFIG_OK) {
            return rc;
        }
        abe_config_add_child(object_node, child);
        abe_config_skip_ws(parser);
        if (parser->cur[0] == '}') {
            ++parser->cur;
            *out_node = object_node;
            return ABE_CONFIG_OK;
        }
        if (parser->cur[0] != ',') {
            return ABE_CONFIG_PARSE_ERROR;
        }
        ++parser->cur;
    }
}

static int abe_config_parse_json_array(
    struct abe_config_parser* parser,
    const char* key,
    abe_config_node_t** out_node)
{
    abe_config_node_t* array_node;

    if (parser->cur[0] != '[') {
        return ABE_CONFIG_PARSE_ERROR;
    }
    ++parser->cur;

    array_node = abe_config_node_create(parser->config, key, ABE_CONFIG_VALUE_ARRAY);
    if (array_node == NULL) {
        return ABE_CONFIG_NO_MEMORY;
    }

    abe_config_skip_ws(parser);
    if (parser->cur[0] == ']') {
        ++parser->cur;
        *out_node = array_node;
        return ABE_CONFIG_OK;
    }

    for (;;) {
        abe_config_node_t* child;
        int rc;

        rc = abe_config_parse_json_value(parser, NULL, &child);
        if (rc != ABE_CONFIG_OK) {
            return rc;
        }
        abe_config_add_child(array_node, child);
        abe_config_skip_ws(parser);
        if (parser->cur[0] == ']') {
            ++parser->cur;
            *out_node = array_node;
            return ABE_CONFIG_OK;
        }
        if (parser->cur[0] != ',') {
            return ABE_CONFIG_PARSE_ERROR;
        }
        ++parser->cur;
    }
}

static int abe_config_parse_json_number(
    struct abe_config_parser* parser,
    const char* key,
    abe_config_node_t** out_node)
{
    const char* begin;
    abe_config_node_t* node;

    begin = parser->cur;
    if (parser->cur[0] == '-') {
        ++parser->cur;
    }
    while (isdigit((unsigned char)parser->cur[0])) {
        ++parser->cur;
    }
    if (parser->cur[0] == '.') {
        ++parser->cur;
        while (isdigit((unsigned char)parser->cur[0])) {
            ++parser->cur;
        }
    }
    if (parser->cur[0] == 'e' || parser->cur[0] == 'E') {
        ++parser->cur;
        if (parser->cur[0] == '+' || parser->cur[0] == '-') {
            ++parser->cur;
        }
        while (isdigit((unsigned char)parser->cur[0])) {
            ++parser->cur;
        }
    }
    if (begin == parser->cur) {
        return ABE_CONFIG_PARSE_ERROR;
    }

    node = abe_config_node_create(parser->config, key, ABE_CONFIG_VALUE_NUMBER);
    if (node == NULL) {
        return ABE_CONFIG_NO_MEMORY;
    }
    node->value = abe_config_copy_range(parser->config, begin, parser->cur);
    if (node->value == NULL) {
        return ABE_CONFIG_NO_MEMORY;
    }
    *out_node = node;
    return ABE_CONFIG_OK;
}

static int abe_config_parse_json_literal(
    struct abe_config_parser* parser,
    const char* key,
    const char* literal,
    abe_config_value_type_t type,
    const char* value,
    abe_config_node_t** out_node)
{
    abe_config_node_t* node;
    uint64_t size;

    size = abe_config_strlen_u64(literal);
    if (strncmp(parser->cur, literal, (size_t)size) != 0) {
        return ABE_CONFIG_PARSE_ERROR;
    }
    parser->cur += size;
    node = abe_config_node_create(parser->config, key, type);
    if (node == NULL) {
        return ABE_CONFIG_NO_MEMORY;
    }
    if (value != NULL) {
        node->value = abe_config_copy_cstr(parser->config, value);
        if (node->value == NULL) {
            return ABE_CONFIG_NO_MEMORY;
        }
    }
    *out_node = node;
    return ABE_CONFIG_OK;
}

static int abe_config_parse_json_value(
    struct abe_config_parser* parser,
    const char* key,
    abe_config_node_t** out_node)
{
    abe_config_node_t* node;

    abe_config_skip_ws(parser);
    switch (parser->cur[0]) {
    case '{':
        return abe_config_parse_json_object(parser, key, out_node);
    case '[':
        return abe_config_parse_json_array(parser, key, out_node);
    case '"':
        node = abe_config_node_create(parser->config, key, ABE_CONFIG_VALUE_STRING);
        if (node == NULL) {
            return ABE_CONFIG_NO_MEMORY;
        }
        node->value = abe_config_parse_json_string(parser);
        if (node->value == NULL) {
            return ABE_CONFIG_PARSE_ERROR;
        }
        *out_node = node;
        return ABE_CONFIG_OK;
    case 't':
        return abe_config_parse_json_literal(parser, key, "true", ABE_CONFIG_VALUE_BOOL, "true", out_node);
    case 'f':
        return abe_config_parse_json_literal(parser, key, "false", ABE_CONFIG_VALUE_BOOL, "false", out_node);
    case 'n':
        return abe_config_parse_json_literal(parser, key, "null", ABE_CONFIG_VALUE_NULL, NULL, out_node);
    default:
        return abe_config_parse_json_number(parser, key, out_node);
    }
}

static int abe_config_parse_json(abe_config_t* config)
{
    struct abe_config_parser parser;
    int rc;

    memset(&parser, 0, sizeof(parser));
    parser.config = config;
    parser.cur = config->text;
    rc = abe_config_parse_json_value(&parser, NULL, &config->root);
    if (rc != ABE_CONFIG_OK) {
        return rc;
    }
    abe_config_skip_ws(&parser);
    return parser.cur[0] == '\0' ? ABE_CONFIG_OK : ABE_CONFIG_PARSE_ERROR;
}

static int abe_config_is_name_char(int ch)
{
    return isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.';
}

static int abe_config_skip_xml_misc(struct abe_config_parser* parser)
{
    int progressed;

    do {
        progressed = 0;
        abe_config_skip_ws(parser);
        if (strncmp(parser->cur, "<?", 2u) == 0) {
            const char* end;

            end = strstr(parser->cur, "?>");
            if (end == NULL) {
                return ABE_CONFIG_PARSE_ERROR;
            }
            parser->cur = end + 2;
            progressed = 1;
        } else if (strncmp(parser->cur, "<!--", 4u) == 0) {
            const char* end;

            end = strstr(parser->cur, "-->");
            if (end == NULL) {
                return ABE_CONFIG_PARSE_ERROR;
            }
            parser->cur = end + 3;
            progressed = 1;
        }
    } while (progressed);

    return ABE_CONFIG_OK;
}

static char* abe_config_parse_xml_name(struct abe_config_parser* parser)
{
    const char* begin;

    begin = parser->cur;
    while (abe_config_is_name_char((unsigned char)parser->cur[0])) {
        ++parser->cur;
    }
    if (begin == parser->cur) {
        return NULL;
    }
    return abe_config_copy_range(parser->config, begin, parser->cur);
}

static char* abe_config_parse_xml_quoted(struct abe_config_parser* parser)
{
    char quote;
    const char* begin;

    if (parser->cur[0] != '"' && parser->cur[0] != '\'') {
        return NULL;
    }
    quote = parser->cur[0];
    ++parser->cur;
    begin = parser->cur;
    while (parser->cur[0] != '\0' && parser->cur[0] != quote) {
        ++parser->cur;
    }
    if (parser->cur[0] != quote) {
        return NULL;
    }
    ++parser->cur;
    return abe_config_copy_range(parser->config, begin, parser->cur - 1);
}

static char* abe_config_make_xml_attr_key(abe_config_t* config, const char* name)
{
    uint64_t size;
    char* key;

    size = abe_config_strlen_u64(name);
    key = (char*)abe_config_alloc(config, size + 2u);
    if (key == NULL) {
        return NULL;
    }
    key[0] = '@';
    (void)memcpy(key + 1, name, (size_t)size + 1u);
    return key;
}

static char* abe_config_copy_trimmed(abe_config_t* config, const char* begin, const char* end)
{
    while (begin < end && isspace((unsigned char)begin[0])) {
        ++begin;
    }
    while (end > begin && isspace((unsigned char)end[-1])) {
        --end;
    }
    if (begin == end) {
        return NULL;
    }
    return abe_config_copy_range(config, begin, end);
}

static int abe_config_parse_xml_element(
    struct abe_config_parser* parser,
    abe_config_node_t* parent)
{
    abe_config_node_t* element;
    char* element_name;

    if (parser->cur[0] != '<' || parser->cur[1] == '/') {
        return ABE_CONFIG_PARSE_ERROR;
    }
    ++parser->cur;
    element_name = abe_config_parse_xml_name(parser);
    if (element_name == NULL) {
        return ABE_CONFIG_PARSE_ERROR;
    }

    element = abe_config_node_create(parser->config, element_name, ABE_CONFIG_VALUE_OBJECT);
    if (element == NULL) {
        return ABE_CONFIG_NO_MEMORY;
    }
    abe_config_add_child(parent, element);

    for (;;) {
        abe_config_skip_ws(parser);
        if (parser->cur[0] == '/' && parser->cur[1] == '>') {
            parser->cur += 2;
            return ABE_CONFIG_OK;
        }
        if (parser->cur[0] == '>') {
            ++parser->cur;
            break;
        }
        if (parser->cur[0] == '\0') {
            return ABE_CONFIG_PARSE_ERROR;
        }
        {
            char* attr_name;
            char* attr_key;
            char* attr_value;
            abe_config_node_t* attr_node;

            attr_name = abe_config_parse_xml_name(parser);
            if (attr_name == NULL) {
                return ABE_CONFIG_PARSE_ERROR;
            }
            abe_config_skip_ws(parser);
            if (parser->cur[0] != '=') {
                return ABE_CONFIG_PARSE_ERROR;
            }
            ++parser->cur;
            abe_config_skip_ws(parser);
            attr_value = abe_config_parse_xml_quoted(parser);
            if (attr_value == NULL) {
                return ABE_CONFIG_PARSE_ERROR;
            }
            attr_key = abe_config_make_xml_attr_key(parser->config, attr_name);
            if (attr_key == NULL) {
                return ABE_CONFIG_NO_MEMORY;
            }
            attr_node = abe_config_node_create(parser->config, attr_key, ABE_CONFIG_VALUE_STRING);
            if (attr_node == NULL) {
                return ABE_CONFIG_NO_MEMORY;
            }
            attr_node->value = attr_value;
            abe_config_add_child(element, attr_node);
        }
    }

    for (;;) {
        int rc;

        rc = abe_config_skip_xml_misc(parser);
        if (rc != ABE_CONFIG_OK) {
            return rc;
        }
        if (parser->cur[0] == '<' && parser->cur[1] == '/') {
            char* close_name;

            parser->cur += 2;
            close_name = abe_config_parse_xml_name(parser);
            if (close_name == NULL || strcmp(close_name, element_name) != 0) {
                return ABE_CONFIG_PARSE_ERROR;
            }
            abe_config_skip_ws(parser);
            if (parser->cur[0] != '>') {
                return ABE_CONFIG_PARSE_ERROR;
            }
            ++parser->cur;
            return ABE_CONFIG_OK;
        }
        if (parser->cur[0] == '<') {
            rc = abe_config_parse_xml_element(parser, element);
            if (rc != ABE_CONFIG_OK) {
                return rc;
            }
        } else if (parser->cur[0] != '\0') {
            const char* begin;

            begin = parser->cur;
            while (parser->cur[0] != '\0' && parser->cur[0] != '<') {
                ++parser->cur;
            }
            element->value = abe_config_copy_trimmed(parser->config, begin, parser->cur);
        } else {
            return ABE_CONFIG_PARSE_ERROR;
        }
    }
}

static int abe_config_parse_xml(abe_config_t* config)
{
    struct abe_config_parser parser;
    int rc;

    config->root = abe_config_node_create(config, NULL, ABE_CONFIG_VALUE_OBJECT);
    if (config->root == NULL) {
        return ABE_CONFIG_NO_MEMORY;
    }

    memset(&parser, 0, sizeof(parser));
    parser.config = config;
    parser.cur = config->text;
    rc = abe_config_skip_xml_misc(&parser);
    if (rc != ABE_CONFIG_OK) {
        return rc;
    }
    while (parser.cur[0] != '\0') {
        rc = abe_config_parse_xml_element(&parser, config->root);
        if (rc != ABE_CONFIG_OK) {
            return rc;
        }
        rc = abe_config_skip_xml_misc(&parser);
        if (rc != ABE_CONFIG_OK) {
            return rc;
        }
    }
    return ABE_CONFIG_OK;
}

static int abe_config_load_text(
    const char* text,
    abe_config_format_t format,
    abe_config_t** out_config)
{
    abe_config_t* config;
    uint64_t source_size;
    int rc;

    if (text == NULL || out_config == NULL) {
        return ABE_CONFIG_INVALID_ARG;
    }
    source_size = abe_config_strlen_u64(text);
    rc = abe_config_create_empty(format, source_size, &config);
    if (rc != ABE_CONFIG_OK) {
        return rc;
    }

    config->text = abe_config_copy_source(config, text);
    if (config->text == NULL) {
        abe_config_destroy(config);
        return ABE_CONFIG_NO_MEMORY;
    }

    rc = format == ABE_CONFIG_FORMAT_JSON ? abe_config_parse_json(config) : abe_config_parse_xml(config);
    if (rc != ABE_CONFIG_OK) {
        abe_config_destroy(config);
        return rc;
    }

    *out_config = config;
    return ABE_CONFIG_OK;
}

static int abe_config_load_file(
    const char* path,
    abe_config_format_t format,
    abe_config_t** out_config)
{
    FILE* file;
    long file_size;
    abe_config_t* config;
    int rc;

    if (path == NULL || out_config == NULL) {
        return ABE_CONFIG_INVALID_ARG;
    }
    *out_config = NULL;

    file = fopen(path, "rb");
    if (file == NULL) {
        return ABE_CONFIG_ERROR;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        return ABE_CONFIG_ERROR;
    }
    file_size = ftell(file);
    if (file_size < 0) {
        (void)fclose(file);
        return ABE_CONFIG_ERROR;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return ABE_CONFIG_ERROR;
    }

    rc = abe_config_create_empty(format, (uint64_t)file_size, &config);
    if (rc != ABE_CONFIG_OK) {
        (void)fclose(file);
        return rc;
    }
    config->text = (char*)abe_config_alloc(config, (uint64_t)file_size + 1u);
    if (config->text == NULL) {
        (void)fclose(file);
        abe_config_destroy(config);
        return ABE_CONFIG_NO_MEMORY;
    }
    if (file_size > 0 &&
        fread(config->text, 1u, (size_t)file_size, file) != (size_t)file_size) {
        (void)fclose(file);
        abe_config_destroy(config);
        return ABE_CONFIG_ERROR;
    }
    (void)fclose(file);
    config->text[file_size] = '\0';

    rc = format == ABE_CONFIG_FORMAT_JSON ? abe_config_parse_json(config) : abe_config_parse_xml(config);
    if (rc != ABE_CONFIG_OK) {
        abe_config_destroy(config);
        return rc;
    }

    *out_config = config;
    return ABE_CONFIG_OK;
}

static abe_config_node_t* abe_config_child_by_key(
    const abe_config_node_t* parent,
    const char* key,
    uint32_t index)
{
    abe_config_node_t* child;
    uint32_t seen;

    if (parent == NULL || key == NULL) {
        return NULL;
    }

    child = parent->first_child;
    seen = 0u;
    while (child != NULL) {
        if (child->key != NULL && strcmp(child->key, key) == 0) {
            if (seen == index) {
                return child;
            }
            ++seen;
        }
        child = child->next;
    }
    return NULL;
}

static abe_config_node_t* abe_config_array_item(const abe_config_node_t* array_node, uint32_t index)
{
    abe_config_node_t* child;
    uint32_t current;

    if (array_node == NULL || array_node->type != ABE_CONFIG_VALUE_ARRAY) {
        return NULL;
    }
    child = array_node->first_child;
    current = 0u;
    while (child != NULL) {
        if (current == index) {
            return child;
        }
        ++current;
        child = child->next;
    }
    return NULL;
}

static const char* abe_config_parse_path_segment(
    const char* path,
    char token[ABE_CONFIG_PATH_TOKEN_SIZE],
    uint32_t* out_index,
    int* out_has_index)
{
    uint32_t pos;

    pos = 0u;
    *out_index = 0u;
    *out_has_index = 0;
    while (path[0] != '\0' && path[0] != '.' && path[0] != '[') {
        if (pos + 1u >= ABE_CONFIG_PATH_TOKEN_SIZE) {
            return NULL;
        }
        token[pos] = path[0];
        ++pos;
        ++path;
    }
    token[pos] = '\0';

    if (path[0] == '[') {
        ++path;
        *out_has_index = 1;
        while (isdigit((unsigned char)path[0])) {
            *out_index = (*out_index * 10u) + (uint32_t)(path[0] - '0');
            ++path;
        }
        if (path[0] != ']') {
            return NULL;
        }
        ++path;
    }
    if (path[0] == '.') {
        ++path;
    }
    return path;
}

static abe_config_node_t* abe_config_find_path(const abe_config_t* config, const char* path)
{
    abe_config_node_t* node;
    const char* cursor;

    if (config == NULL || config->root == NULL || path == NULL || path[0] == '\0') {
        return NULL;
    }

    node = config->root;
    cursor = path;
    while (cursor[0] != '\0') {
        char token[ABE_CONFIG_PATH_TOKEN_SIZE];
        uint32_t index;
        int has_index;

        cursor = abe_config_parse_path_segment(cursor, token, &index, &has_index);
        if (cursor == NULL) {
            return NULL;
        }

        if (token[0] != '\0') {
            abe_config_node_t* parent;
            abe_config_node_t* first_match;

            parent = node;
            first_match = abe_config_child_by_key(parent, token, 0u);
            if (first_match == NULL) {
                return NULL;
            }
            if (has_index) {
                if (first_match->type == ABE_CONFIG_VALUE_ARRAY) {
                    node = abe_config_array_item(first_match, index);
                } else {
                    node = abe_config_child_by_key(parent, token, index);
                }
            } else {
                node = first_match;
            }
            if (node == NULL) {
                return NULL;
            }
        } else if (has_index) {
            node = abe_config_array_item(node, index);
            if (node == NULL) {
                return NULL;
            }
        }
    }

    return node;
}

int abe_config_load_json_text(const char* text, abe_config_t** out_config)
{
    return abe_config_load_text(text, ABE_CONFIG_FORMAT_JSON, out_config);
}

int abe_config_load_xml_text(const char* text, abe_config_t** out_config)
{
    return abe_config_load_text(text, ABE_CONFIG_FORMAT_XML, out_config);
}

int abe_config_load_json_file(const char* path, abe_config_t** out_config)
{
    return abe_config_load_file(path, ABE_CONFIG_FORMAT_JSON, out_config);
}

int abe_config_load_xml_file(const char* path, abe_config_t** out_config)
{
    return abe_config_load_file(path, ABE_CONFIG_FORMAT_XML, out_config);
}

void abe_config_destroy(abe_config_t* config)
{
    abe_mem_pool_t* mem_pool;

    if (config == NULL) {
        return;
    }
    mem_pool = config->mem_pool;
    abe_mem_pool_destroy(mem_pool);
}

int abe_config_exists(const abe_config_t* config, const char* path)
{
    return abe_config_find_path(config, path) != NULL;
}

int abe_config_get_type(
    const abe_config_t* config,
    const char* path,
    abe_config_value_type_t* out_type)
{
    abe_config_node_t* node;

    if (out_type == NULL) {
        return ABE_CONFIG_INVALID_ARG;
    }
    node = abe_config_find_path(config, path);
    if (node == NULL) {
        return ABE_CONFIG_NOT_FOUND;
    }
    *out_type = node->type;
    return ABE_CONFIG_OK;
}

int abe_config_get_string(const abe_config_t* config, const char* path, const char** out_value)
{
    abe_config_node_t* node;

    if (out_value == NULL) {
        return ABE_CONFIG_INVALID_ARG;
    }
    *out_value = NULL;
    node = abe_config_find_path(config, path);
    if (node == NULL) {
        return ABE_CONFIG_NOT_FOUND;
    }
    if (node->value == NULL) {
        return ABE_CONFIG_TYPE_MISMATCH;
    }
    *out_value = node->value;
    return ABE_CONFIG_OK;
}

int abe_config_get_i64(const abe_config_t* config, const char* path, int64_t* out_value)
{
    abe_config_node_t* node;
    char* endptr;
    long long value;

    if (out_value == NULL) {
        return ABE_CONFIG_INVALID_ARG;
    }
    node = abe_config_find_path(config, path);
    if (node == NULL) {
        return ABE_CONFIG_NOT_FOUND;
    }
    if (node->value == NULL) {
        return ABE_CONFIG_TYPE_MISMATCH;
    }
    errno = 0;
    value = strtoll(node->value, &endptr, 10);
    if (errno != 0 || endptr == node->value || endptr[0] != '\0') {
        return ABE_CONFIG_TYPE_MISMATCH;
    }
    *out_value = (int64_t)value;
    return ABE_CONFIG_OK;
}

int abe_config_get_u64(const abe_config_t* config, const char* path, uint64_t* out_value)
{
    abe_config_node_t* node;
    char* endptr;
    unsigned long long value;

    if (out_value == NULL) {
        return ABE_CONFIG_INVALID_ARG;
    }
    node = abe_config_find_path(config, path);
    if (node == NULL) {
        return ABE_CONFIG_NOT_FOUND;
    }
    if (node->value == NULL) {
        return ABE_CONFIG_TYPE_MISMATCH;
    }
    errno = 0;
    value = strtoull(node->value, &endptr, 10);
    if (errno != 0 || endptr == node->value || endptr[0] != '\0') {
        return ABE_CONFIG_TYPE_MISMATCH;
    }
    *out_value = (uint64_t)value;
    return ABE_CONFIG_OK;
}

int abe_config_get_double(const abe_config_t* config, const char* path, double* out_value)
{
    abe_config_node_t* node;
    char* endptr;
    double value;

    if (out_value == NULL) {
        return ABE_CONFIG_INVALID_ARG;
    }
    node = abe_config_find_path(config, path);
    if (node == NULL) {
        return ABE_CONFIG_NOT_FOUND;
    }
    if (node->value == NULL) {
        return ABE_CONFIG_TYPE_MISMATCH;
    }
    errno = 0;
    value = strtod(node->value, &endptr);
    if (errno != 0 || endptr == node->value || endptr[0] != '\0') {
        return ABE_CONFIG_TYPE_MISMATCH;
    }
    *out_value = value;
    return ABE_CONFIG_OK;
}

int abe_config_get_bool(const abe_config_t* config, const char* path, int* out_value)
{
    abe_config_node_t* node;

    if (out_value == NULL) {
        return ABE_CONFIG_INVALID_ARG;
    }
    node = abe_config_find_path(config, path);
    if (node == NULL) {
        return ABE_CONFIG_NOT_FOUND;
    }
    if (node->value == NULL) {
        return ABE_CONFIG_TYPE_MISMATCH;
    }
    if (strcmp(node->value, "true") == 0 || strcmp(node->value, "1") == 0) {
        *out_value = 1;
        return ABE_CONFIG_OK;
    }
    if (strcmp(node->value, "false") == 0 || strcmp(node->value, "0") == 0) {
        *out_value = 0;
        return ABE_CONFIG_OK;
    }
    return ABE_CONFIG_TYPE_MISMATCH;
}
