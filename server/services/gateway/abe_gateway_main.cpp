#include "abe_gateway_app.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace gateway = abe::service::gateway;

static int parse_u32(
    const char* text,
    uint32_t min_value,
    uint32_t max_value,
    uint32_t* out_value)
{
    char* end;
    unsigned long value;

    if (text == NULL || out_value == NULL) {
        return -1;
    }

    errno = 0;
    end = NULL;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0' ||
        value < (unsigned long)min_value ||
        value > (unsigned long)max_value) {
        return -1;
    }

    *out_value = (uint32_t)value;
    return 0;
}

static int parse_u64(
    const char* text,
    uint64_t min_value,
    uint64_t max_value,
    uint64_t* out_value)
{
    char* end;
    unsigned long long value;

    if (text == NULL || out_value == NULL) {
        return -1;
    }

    errno = 0;
    end = NULL;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0' ||
        value < (unsigned long long)min_value ||
        value > (unsigned long long)max_value) {
        return -1;
    }

    *out_value = (uint64_t)value;
    return 0;
}

static void print_usage(const char* program)
{
    printf(
        "Usage: %s [options]\n"
        "  --host <ip>              listen host, default 0.0.0.0\n"
        "  --port <port>            listen port, default 7000\n"
        "  --max-clients <count>    max client links, default 1024\n"
        "  --backlog <count>        tcp listen backlog, default 128\n"
        "  --max-packet-size <size> max payload size, default engine value\n"
        "  --server-id <id>         gateway server id, default 1\n"
        "  --idle-ms <ms>           session idle timeout, default 60000\n"
        "  --tick-ms <ms>           main loop sleep, default 10\n"
        "  --help                   show this help\n",
        program == NULL ? "abe_gateway" : program);
}

static int parse_args(int argc, char** argv, gateway::GatewayMainConfig* config)
{
    int index;

    if (config == NULL) {
        return -1;
    }

    index = 1;
    while (index < argc) {
        const char* name;

        name = argv[index];
        if (strcmp(name, "--help") == 0) {
            print_usage(argv[0]);
            return 1;
        }
        if (strcmp(name, "--host") == 0 && index + 1 < argc) {
            config->host = argv[index + 1];
            index += 2;
            continue;
        }
        if (strcmp(name, "--port") == 0 && index + 1 < argc) {
            uint32_t value;

            if (parse_u32(argv[index + 1], 1u, 65535u, &value) != 0) {
                return -1;
            }
            config->port = (uint16_t)value;
            index += 2;
            continue;
        }
        if (strcmp(name, "--max-clients") == 0 && index + 1 < argc) {
            if (parse_u32(
                    argv[index + 1],
                    1u,
                    gateway::ABE_GATEWAY_MAX_CLIENTS,
                    &config->max_clients) != 0) {
                return -1;
            }
            index += 2;
            continue;
        }
        if (strcmp(name, "--backlog") == 0 && index + 1 < argc) {
            uint32_t value;

            if (parse_u32(argv[index + 1], 1u, 65535u, &value) != 0) {
                return -1;
            }
            config->backlog = (int)value;
            index += 2;
            continue;
        }
        if (strcmp(name, "--max-packet-size") == 0 && index + 1 < argc) {
            if (parse_u32(argv[index + 1], 0u, 16777216u, &config->max_packet_size) != 0) {
                return -1;
            }
            index += 2;
            continue;
        }
        if (strcmp(name, "--server-id") == 0 && index + 1 < argc) {
            if (parse_u64(argv[index + 1], 1u, 0xffffffffffffffffull, &config->server_id) != 0) {
                return -1;
            }
            index += 2;
            continue;
        }
        if (strcmp(name, "--idle-ms") == 0 && index + 1 < argc) {
            if (parse_u64(argv[index + 1], 0u, 0xffffffffffffffffull, &config->idle_timeout_ms) != 0) {
                return -1;
            }
            index += 2;
            continue;
        }
        if (strcmp(name, "--tick-ms") == 0 && index + 1 < argc) {
            if (parse_u32(argv[index + 1], 0u, 1000u, &config->tick_ms) != 0) {
                return -1;
            }
            index += 2;
            continue;
        }

        fprintf(stderr, "unknown or invalid option: %s\n", name);
        return -1;
    }

    return 0;
}

int main(int argc, char** argv)
{
    gateway::GatewayMainConfig config;
    gateway::GatewayApp app;
    int rc;

    gateway::gateway_main_config_set_defaults(&config);
    rc = parse_args(argc, argv, &config);
    if (rc > 0) {
        return 0;
    }
    if (rc < 0) {
        print_usage(argv[0]);
        return 1;
    }

    rc = app.init(config);
    if (rc != 0) {
        return rc;
    }
    return app.run();
}
