#include "abe_config.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

static int test_json_text(void)
{
    const char* text;
    const char* value;
    abe_config_t* config;
    abe_config_value_type_t type;
    int64_t i64_value;
    double double_value;
    int bool_value;

    text =
        "{"
        "\"server\":{\"host\":\"127.0.0.1\",\"port\":7001,\"enabled\":true},"
        "\"rooms\":[{\"name\":\"alpha\"},{\"name\":\"beta\"}],"
        "\"escaped\":\"line\\nnext\","
        "\"unicode\":\"\\u0041\","
        "\"nothing\":null,"
        "\"ratio\":1.25"
        "}";

    config = NULL;
    TEST_REQUIRE(abe_config_load_json_text(text, &config) == ABE_CONFIG_OK);
    TEST_REQUIRE(config != NULL);
    TEST_REQUIRE(abe_config_get_string(config, "server.host", &value) == ABE_CONFIG_OK);
    TEST_REQUIRE(strcmp(value, "127.0.0.1") == 0);
    TEST_REQUIRE(abe_config_get_i64(config, "server.port", &i64_value) == ABE_CONFIG_OK);
    TEST_REQUIRE(i64_value == 7001);
    TEST_REQUIRE(abe_config_get_bool(config, "server.enabled", &bool_value) == ABE_CONFIG_OK);
    TEST_REQUIRE(bool_value == 1);
    TEST_REQUIRE(abe_config_get_string(config, "rooms[1].name", &value) == ABE_CONFIG_OK);
    TEST_REQUIRE(strcmp(value, "beta") == 0);
    TEST_REQUIRE(abe_config_get_string(config, "escaped", &value) == ABE_CONFIG_OK);
    TEST_REQUIRE(strcmp(value, "line\nnext") == 0);
    TEST_REQUIRE(abe_config_get_string(config, "unicode", &value) == ABE_CONFIG_OK);
    TEST_REQUIRE(strcmp(value, "A") == 0);
    TEST_REQUIRE(abe_config_get_type(config, "nothing", &type) == ABE_CONFIG_OK);
    TEST_REQUIRE(type == ABE_CONFIG_VALUE_NULL);
    TEST_REQUIRE(abe_config_get_string(config, "nothing", &value) ==
        ABE_CONFIG_TYPE_MISMATCH);
    TEST_REQUIRE(abe_config_get_double(config, "ratio", &double_value) == ABE_CONFIG_OK);
    TEST_REQUIRE(double_value > 1.24 && double_value < 1.26);
    TEST_REQUIRE(abe_config_exists(config, "server.host"));
    TEST_REQUIRE(!abe_config_exists(config, "server.missing"));
    abe_config_destroy(config);
    return 0;
}

static int test_xml_text(void)
{
    const char* text;
    const char* value;
    abe_config_t* config;
    abe_config_value_type_t type;
    int64_t i64_value;
    int bool_value;

    text =
        "<?xml version=\"1.0\"?>"
        "<server host=\"127.0.0.1\" port=\"7002\">"
        "<enabled>true</enabled>"
        "<room><name>alpha</name></room>"
        "<room><name>beta</name></room>"
        "<escaped>Tom &amp; Jerry</escaped>"
        "<raw><![CDATA[line <tag>]]></raw>"
        "<empty/>"
        "</server>";

    config = NULL;
    TEST_REQUIRE(abe_config_load_xml_text(text, &config) == ABE_CONFIG_OK);
    TEST_REQUIRE(config != NULL);
    TEST_REQUIRE(abe_config_get_string(config, "server.@host", &value) == ABE_CONFIG_OK);
    TEST_REQUIRE(strcmp(value, "127.0.0.1") == 0);
    TEST_REQUIRE(abe_config_get_i64(config, "server.@port", &i64_value) == ABE_CONFIG_OK);
    TEST_REQUIRE(i64_value == 7002);
    TEST_REQUIRE(abe_config_get_bool(config, "server.enabled", &bool_value) == ABE_CONFIG_OK);
    TEST_REQUIRE(bool_value == 1);
    TEST_REQUIRE(abe_config_get_string(config, "server.room[1].name", &value) == ABE_CONFIG_OK);
    TEST_REQUIRE(strcmp(value, "beta") == 0);
    TEST_REQUIRE(abe_config_get_string(config, "server.escaped", &value) == ABE_CONFIG_OK);
    TEST_REQUIRE(strcmp(value, "Tom & Jerry") == 0);
    TEST_REQUIRE(abe_config_get_string(config, "server.raw", &value) == ABE_CONFIG_OK);
    TEST_REQUIRE(strcmp(value, "line <tag>") == 0);
    TEST_REQUIRE(abe_config_get_type(config, "server.empty", &type) == ABE_CONFIG_OK);
    TEST_REQUIRE(type == ABE_CONFIG_VALUE_OBJECT);
    TEST_REQUIRE(abe_config_get_string(config, "server.empty", &value) ==
        ABE_CONFIG_TYPE_MISMATCH);
    abe_config_destroy(config);
    return 0;
}

static int test_file_load(void)
{
    char path[128];
    FILE* file;
    abe_config_t* config;
    int64_t value;

    (void)snprintf(path, sizeof(path), "/tmp/abe_config_test_%ld.json", (long)getpid());
    file = fopen(path, "wb");
    TEST_REQUIRE(file != NULL);
    TEST_REQUIRE(fputs("{\"value\":42}", file) >= 0);
    TEST_REQUIRE(fclose(file) == 0);

    config = NULL;
    TEST_REQUIRE(abe_config_load_json_file(path, &config) == ABE_CONFIG_OK);
    TEST_REQUIRE(abe_config_get_i64(config, "value", &value) == ABE_CONFIG_OK);
    TEST_REQUIRE(value == 42);
    abe_config_destroy(config);
    TEST_REQUIRE(remove(path) == 0);
    return 0;
}

static int test_json_rejects_invalid_text(void)
{
    abe_config_t* config;

    config = NULL;
    TEST_REQUIRE(abe_config_load_json_text("{\"value\":42} trailing", &config) ==
        ABE_CONFIG_PARSE_ERROR);
    TEST_REQUIRE(config == NULL);
    return 0;
}

static int test_xml_rejects_invalid_text(void)
{
    abe_config_t* config;

    config = NULL;
    TEST_REQUIRE(abe_config_load_xml_text("<server><value></server>", &config) ==
        ABE_CONFIG_PARSE_ERROR);
    TEST_REQUIRE(config == NULL);
    return 0;
}

int main(void)
{
    if (test_json_text() != 0) {
        return 1;
    }
    if (test_xml_text() != 0) {
        return 1;
    }
    if (test_file_load() != 0) {
        return 1;
    }
    if (test_json_rejects_invalid_text() != 0) {
        return 1;
    }
    if (test_xml_rejects_invalid_text() != 0) {
        return 1;
    }
    return 0;
}
