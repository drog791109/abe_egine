#include "abe_kafka.h"
#include "abe_mem_pool.h"

#include <librdkafka/rdkafka.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ABE_KAFKA_ERROR_SIZE 512u
#define ABE_KAFKA_DEFAULT_POOL_CAPACITY (64u * 1024u)

struct abe_kafka_producer {
    rd_kafka_t* rk;
    abe_mem_pool_t* mem_pool;
    int owns_mem_pool;
    char last_error[ABE_KAFKA_ERROR_SIZE];
};

struct abe_kafka_consumer {
    rd_kafka_t* rk;
    abe_mem_pool_t* mem_pool;
    int owns_mem_pool;
    char last_error[ABE_KAFKA_ERROR_SIZE];
};

struct abe_kafka_message {
    abe_kafka_consumer_t* owner;
    rd_kafka_message_t* message;
};

static uint64_t abe_kafka_pool_capacity(uint64_t configured_capacity)
{
    return configured_capacity == 0u ? ABE_KAFKA_DEFAULT_POOL_CAPACITY : configured_capacity;
}

static void abe_kafka_copy_error(char out[ABE_KAFKA_ERROR_SIZE], const char* message)
{
    if (out == NULL) {
        return;
    }
    if (message == NULL || message[0] == '\0') {
        out[0] = '\0';
        return;
    }
    (void)strncpy(out, message, ABE_KAFKA_ERROR_SIZE - 1u);
    out[ABE_KAFKA_ERROR_SIZE - 1u] = '\0';
}

static int abe_kafka_size_valid(uint64_t size)
{
    return size <= (uint64_t)((size_t)-1);
}

static int abe_kafka_pool_create(uint64_t capacity, const char* name, abe_mem_pool_t** out_pool)
{
    abe_mem_pool_config_t pool_config;

    if (out_pool == NULL) {
        return ABE_KAFKA_INVALID_ARG;
    }
    memset(&pool_config, 0, sizeof(pool_config));
    pool_config.capacity = abe_kafka_pool_capacity(capacity);
    pool_config.name = name;
    return abe_mem_pool_create(&pool_config, out_pool) == ABE_MEM_POOL_OK ?
        ABE_KAFKA_OK : ABE_KAFKA_NO_MEMORY;
}

static int abe_kafka_conf_set(
    rd_kafka_conf_t* conf,
    const char* name,
    const char* value,
    char errbuf[ABE_KAFKA_ERROR_SIZE])
{
    if (conf == NULL || name == NULL || value == NULL) {
        return ABE_KAFKA_INVALID_ARG;
    }
    if (rd_kafka_conf_set(conf, name, value, errbuf, ABE_KAFKA_ERROR_SIZE) !=
        RD_KAFKA_CONF_OK) {
        return ABE_KAFKA_CONFIG_ERROR;
    }
    return ABE_KAFKA_OK;
}

static int abe_kafka_conf_set_entries(
    rd_kafka_conf_t* conf,
    const abe_kafka_config_entry_t* entries,
    uint32_t entry_count,
    char errbuf[ABE_KAFKA_ERROR_SIZE])
{
    uint32_t index;
    int status;

    if (entry_count != 0u && entries == NULL) {
        return ABE_KAFKA_INVALID_ARG;
    }
    for (index = 0u; index < entry_count; ++index) {
        status = abe_kafka_conf_set(conf, entries[index].name, entries[index].value, errbuf);
        if (status != ABE_KAFKA_OK) {
            return status;
        }
    }
    return ABE_KAFKA_OK;
}

int abe_kafka_producer_create(
    const abe_kafka_producer_config_t* config,
    abe_kafka_producer_t** out_producer)
{
    abe_mem_pool_t* mem_pool;
    abe_kafka_producer_t* producer;
    rd_kafka_conf_t* conf;
    char errbuf[ABE_KAFKA_ERROR_SIZE];
    int status;

    if (config == NULL || config->brokers == NULL || out_producer == NULL) {
        return ABE_KAFKA_INVALID_ARG;
    }

    *out_producer = NULL;
    mem_pool = NULL;
    status = abe_kafka_pool_create(config->memory_pool_capacity, "abe_kafka_producer", &mem_pool);
    if (status != ABE_KAFKA_OK) {
        return status;
    }

    producer = (abe_kafka_producer_t*)abe_mem_pool_calloc(mem_pool, 1u, sizeof(*producer));
    if (producer == NULL) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_KAFKA_NO_MEMORY;
    }
    producer->mem_pool = mem_pool;
    producer->owns_mem_pool = 1;

    errbuf[0] = '\0';
    conf = rd_kafka_conf_new();
    if (conf == NULL) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_KAFKA_NO_MEMORY;
    }
    status = abe_kafka_conf_set(conf, "bootstrap.servers", config->brokers, errbuf);
    if (status == ABE_KAFKA_OK && config->client_id != NULL) {
        status = abe_kafka_conf_set(conf, "client.id", config->client_id, errbuf);
    }
    if (status == ABE_KAFKA_OK) {
        status = abe_kafka_conf_set_entries(
            conf,
            config->properties,
            config->property_count,
            errbuf);
    }
    if (status != ABE_KAFKA_OK) {
        abe_kafka_copy_error(producer->last_error, errbuf);
        rd_kafka_conf_destroy(conf);
        abe_mem_pool_destroy(mem_pool);
        return status;
    }

    producer->rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errbuf, sizeof(errbuf));
    if (producer->rk == NULL) {
        abe_kafka_copy_error(producer->last_error, errbuf);
        rd_kafka_conf_destroy(conf);
        abe_mem_pool_destroy(mem_pool);
        return ABE_KAFKA_CONNECT_FAILED;
    }

    *out_producer = producer;
    return ABE_KAFKA_OK;
}

int abe_kafka_producer_produce(
    abe_kafka_producer_t* producer,
    const char* topic,
    int32_t partition,
    const void* key,
    uint64_t key_size,
    const void* payload,
    uint64_t payload_size)
{
    rd_kafka_resp_err_t err;

    if (producer == NULL || producer->rk == NULL || topic == NULL ||
        (payload == NULL && payload_size != 0u) ||
        (key == NULL && key_size != 0u) ||
        !abe_kafka_size_valid(payload_size) ||
        !abe_kafka_size_valid(key_size)) {
        return ABE_KAFKA_INVALID_ARG;
    }

    err = rd_kafka_producev(
        producer->rk,
        RD_KAFKA_V_TOPIC(topic),
        RD_KAFKA_V_PARTITION(partition),
        RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
        RD_KAFKA_V_VALUE((void*)payload, (size_t)payload_size),
        RD_KAFKA_V_KEY((void*)key, (size_t)key_size),
        RD_KAFKA_V_END);
    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        abe_kafka_copy_error(producer->last_error, rd_kafka_err2str(err));
        return ABE_KAFKA_SEND_FAILED;
    }
    producer->last_error[0] = '\0';
    return ABE_KAFKA_OK;
}

int abe_kafka_producer_poll(
    abe_kafka_producer_t* producer,
    int timeout_ms,
    int* out_event_count)
{
    int event_count;

    if (producer == NULL || producer->rk == NULL) {
        return ABE_KAFKA_INVALID_ARG;
    }
    event_count = rd_kafka_poll(producer->rk, timeout_ms);
    if (out_event_count != NULL) {
        *out_event_count = event_count;
    }
    return ABE_KAFKA_OK;
}

int abe_kafka_producer_flush(abe_kafka_producer_t* producer, int timeout_ms)
{
    rd_kafka_resp_err_t err;

    if (producer == NULL || producer->rk == NULL) {
        return ABE_KAFKA_INVALID_ARG;
    }
    err = rd_kafka_flush(producer->rk, timeout_ms);
    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        abe_kafka_copy_error(producer->last_error, rd_kafka_err2str(err));
        return ABE_KAFKA_ERROR;
    }
    return ABE_KAFKA_OK;
}

const char* abe_kafka_producer_last_error(const abe_kafka_producer_t* producer)
{
    return producer == NULL ? "" : producer->last_error;
}

void abe_kafka_producer_destroy(abe_kafka_producer_t* producer)
{
    abe_mem_pool_t* mem_pool;
    int owns_mem_pool;

    if (producer == NULL) {
        return;
    }
    mem_pool = producer->mem_pool;
    owns_mem_pool = producer->owns_mem_pool;
    if (producer->rk != NULL) {
        rd_kafka_destroy(producer->rk);
        producer->rk = NULL;
    }
    if (mem_pool != NULL) {
        (void)abe_mem_pool_free(mem_pool, producer);
        if (owns_mem_pool != 0) {
            abe_mem_pool_destroy(mem_pool);
        }
    }
}

int abe_kafka_consumer_create(
    const abe_kafka_consumer_config_t* config,
    abe_kafka_consumer_t** out_consumer)
{
    abe_mem_pool_t* mem_pool;
    abe_kafka_consumer_t* consumer;
    rd_kafka_conf_t* conf;
    char errbuf[ABE_KAFKA_ERROR_SIZE];
    int status;

    if (config == NULL || config->brokers == NULL ||
        config->group_id == NULL || out_consumer == NULL) {
        return ABE_KAFKA_INVALID_ARG;
    }

    *out_consumer = NULL;
    mem_pool = NULL;
    status = abe_kafka_pool_create(config->memory_pool_capacity, "abe_kafka_consumer", &mem_pool);
    if (status != ABE_KAFKA_OK) {
        return status;
    }

    consumer = (abe_kafka_consumer_t*)abe_mem_pool_calloc(mem_pool, 1u, sizeof(*consumer));
    if (consumer == NULL) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_KAFKA_NO_MEMORY;
    }
    consumer->mem_pool = mem_pool;
    consumer->owns_mem_pool = 1;

    errbuf[0] = '\0';
    conf = rd_kafka_conf_new();
    if (conf == NULL) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_KAFKA_NO_MEMORY;
    }
    status = abe_kafka_conf_set(conf, "bootstrap.servers", config->brokers, errbuf);
    if (status == ABE_KAFKA_OK) {
        status = abe_kafka_conf_set(conf, "group.id", config->group_id, errbuf);
    }
    if (status == ABE_KAFKA_OK && config->client_id != NULL) {
        status = abe_kafka_conf_set(conf, "client.id", config->client_id, errbuf);
    }
    if (status == ABE_KAFKA_OK && config->auto_offset_reset != NULL) {
        status = abe_kafka_conf_set(
            conf,
            "auto.offset.reset",
            config->auto_offset_reset,
            errbuf);
    }
    if (status == ABE_KAFKA_OK) {
        status = abe_kafka_conf_set_entries(
            conf,
            config->properties,
            config->property_count,
            errbuf);
    }
    if (status != ABE_KAFKA_OK) {
        abe_kafka_copy_error(consumer->last_error, errbuf);
        rd_kafka_conf_destroy(conf);
        abe_mem_pool_destroy(mem_pool);
        return status;
    }

    consumer->rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errbuf, sizeof(errbuf));
    if (consumer->rk == NULL) {
        abe_kafka_copy_error(consumer->last_error, errbuf);
        rd_kafka_conf_destroy(conf);
        abe_mem_pool_destroy(mem_pool);
        return ABE_KAFKA_CONNECT_FAILED;
    }

    rd_kafka_poll_set_consumer(consumer->rk);
    *out_consumer = consumer;
    return ABE_KAFKA_OK;
}

int abe_kafka_consumer_subscribe(
    abe_kafka_consumer_t* consumer,
    const char** topics,
    uint32_t topic_count)
{
    rd_kafka_topic_partition_list_t* topic_list;
    rd_kafka_resp_err_t err;
    uint32_t index;

    if (consumer == NULL || consumer->rk == NULL ||
        topics == NULL || topic_count == 0u || topic_count > (uint32_t)INT_MAX) {
        return ABE_KAFKA_INVALID_ARG;
    }

    topic_list = rd_kafka_topic_partition_list_new((int)topic_count);
    if (topic_list == NULL) {
        return ABE_KAFKA_NO_MEMORY;
    }
    for (index = 0u; index < topic_count; ++index) {
        if (topics[index] == NULL) {
            rd_kafka_topic_partition_list_destroy(topic_list);
            return ABE_KAFKA_INVALID_ARG;
        }
        rd_kafka_topic_partition_list_add(topic_list, topics[index], RD_KAFKA_PARTITION_UA);
    }

    err = rd_kafka_subscribe(consumer->rk, topic_list);
    rd_kafka_topic_partition_list_destroy(topic_list);
    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        abe_kafka_copy_error(consumer->last_error, rd_kafka_err2str(err));
        return ABE_KAFKA_ERROR;
    }
    consumer->last_error[0] = '\0';
    return ABE_KAFKA_OK;
}

int abe_kafka_consumer_poll(
    abe_kafka_consumer_t* consumer,
    int timeout_ms,
    abe_kafka_message_t** out_message)
{
    rd_kafka_message_t* raw_message;
    abe_kafka_message_t* message;

    if (consumer == NULL || consumer->rk == NULL || out_message == NULL) {
        return ABE_KAFKA_INVALID_ARG;
    }

    *out_message = NULL;
    raw_message = rd_kafka_consumer_poll(consumer->rk, timeout_ms);
    if (raw_message == NULL) {
        return ABE_KAFKA_NO_MESSAGE;
    }

    message = (abe_kafka_message_t*)abe_mem_pool_calloc(
        consumer->mem_pool,
        1u,
        sizeof(*message));
    if (message == NULL) {
        rd_kafka_message_destroy(raw_message);
        return ABE_KAFKA_NO_MEMORY;
    }

    message->owner = consumer;
    message->message = raw_message;
    *out_message = message;
    return ABE_KAFKA_OK;
}

int abe_kafka_consumer_commit_message(
    abe_kafka_consumer_t* consumer,
    const abe_kafka_message_t* message,
    int async)
{
    rd_kafka_resp_err_t err;

    if (consumer == NULL || consumer->rk == NULL ||
        message == NULL || message->message == NULL) {
        return ABE_KAFKA_INVALID_ARG;
    }

    err = rd_kafka_commit_message(consumer->rk, message->message, async != 0);
    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        abe_kafka_copy_error(consumer->last_error, rd_kafka_err2str(err));
        return ABE_KAFKA_ERROR;
    }
    consumer->last_error[0] = '\0';
    return ABE_KAFKA_OK;
}

const char* abe_kafka_consumer_last_error(const abe_kafka_consumer_t* consumer)
{
    return consumer == NULL ? "" : consumer->last_error;
}

void abe_kafka_consumer_destroy(abe_kafka_consumer_t* consumer)
{
    abe_mem_pool_t* mem_pool;
    int owns_mem_pool;

    if (consumer == NULL) {
        return;
    }
    mem_pool = consumer->mem_pool;
    owns_mem_pool = consumer->owns_mem_pool;
    if (consumer->rk != NULL) {
        (void)rd_kafka_consumer_close(consumer->rk);
        rd_kafka_destroy(consumer->rk);
        consumer->rk = NULL;
    }
    if (mem_pool != NULL) {
        (void)abe_mem_pool_free(mem_pool, consumer);
        if (owns_mem_pool != 0) {
            abe_mem_pool_destroy(mem_pool);
        }
    }
}

int abe_kafka_message_view(
    const abe_kafka_message_t* message,
    abe_kafka_message_view_t* out_view)
{
    const rd_kafka_message_t* raw_message;

    if (message == NULL || message->message == NULL || out_view == NULL) {
        return ABE_KAFKA_INVALID_ARG;
    }

    raw_message = message->message;
    memset(out_view, 0, sizeof(*out_view));
    out_view->partition = raw_message->partition;
    out_view->offset = raw_message->offset;
    out_view->payload = raw_message->payload;
    out_view->payload_size = (uint64_t)raw_message->len;
    out_view->key = raw_message->key;
    out_view->key_size = (uint64_t)raw_message->key_len;
    out_view->error_code = (int)raw_message->err;
    out_view->error_message = raw_message->err == RD_KAFKA_RESP_ERR_NO_ERROR ?
        "" : rd_kafka_err2str(raw_message->err);
    out_view->topic = raw_message->rkt == NULL ? NULL : rd_kafka_topic_name(raw_message->rkt);
    return ABE_KAFKA_OK;
}

void abe_kafka_message_destroy(abe_kafka_message_t* message)
{
    abe_mem_pool_t* mem_pool;

    if (message == NULL) {
        return;
    }
    mem_pool = message->owner == NULL ? NULL : message->owner->mem_pool;
    if (message->message != NULL) {
        rd_kafka_message_destroy(message->message);
        message->message = NULL;
    }
    if (mem_pool != NULL) {
        (void)abe_mem_pool_free(mem_pool, message);
    }
}
