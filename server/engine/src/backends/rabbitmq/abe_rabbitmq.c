#include "abe_rabbitmq.h"
#include "abe_mem_pool.h"

#include <amqp.h>
#include <amqp_tcp_socket.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#define ABE_RABBITMQ_ERROR_SIZE 512u
#define ABE_RABBITMQ_DEFAULT_POOL_CAPACITY (64u * 1024u)

struct abe_rabbitmq {
    amqp_connection_state_t conn;
    amqp_socket_t* socket;
    uint16_t channel;
    abe_mem_pool_t* mem_pool;
    int owns_mem_pool;
    char last_error[ABE_RABBITMQ_ERROR_SIZE];
};

struct abe_rabbitmq_message {
    abe_rabbitmq_t* owner;
    amqp_envelope_t envelope;
};

static uint64_t abe_rabbitmq_pool_capacity(uint64_t configured_capacity)
{
    return configured_capacity == 0u ? ABE_RABBITMQ_DEFAULT_POOL_CAPACITY : configured_capacity;
}

static void abe_rabbitmq_copy_error(abe_rabbitmq_t* rabbitmq, const char* message)
{
    if (rabbitmq == NULL) {
        return;
    }
    if (message == NULL || message[0] == '\0') {
        rabbitmq->last_error[0] = '\0';
        return;
    }
    (void)strncpy(rabbitmq->last_error, message, ABE_RABBITMQ_ERROR_SIZE - 1u);
    rabbitmq->last_error[ABE_RABBITMQ_ERROR_SIZE - 1u] = '\0';
}

static int abe_rabbitmq_body_size_valid(uint64_t size)
{
    return size <= (uint64_t)((size_t)-1);
}

static int abe_rabbitmq_check_rpc(
    abe_rabbitmq_t* rabbitmq,
    amqp_rpc_reply_t reply,
    const char* fallback)
{
    if (reply.reply_type == AMQP_RESPONSE_NORMAL) {
        abe_rabbitmq_copy_error(rabbitmq, "");
        return ABE_RABBITMQ_OK;
    }
    if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION) {
        abe_rabbitmq_copy_error(rabbitmq, amqp_error_string2(reply.library_error));
        return ABE_RABBITMQ_PROTOCOL_ERROR;
    }
    abe_rabbitmq_copy_error(rabbitmq, fallback);
    return ABE_RABBITMQ_PROTOCOL_ERROR;
}

static amqp_bytes_t abe_rabbitmq_cstring_bytes(const char* value)
{
    return value == NULL ? amqp_empty_bytes : amqp_cstring_bytes(value);
}

static void abe_rabbitmq_timeval_from_ms(int timeout_ms, struct timeval* out_timeval)
{
    if (out_timeval == NULL) {
        return;
    }
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    out_timeval->tv_sec = timeout_ms / 1000;
    out_timeval->tv_usec = (timeout_ms % 1000) * 1000;
}

int abe_rabbitmq_create(const abe_rabbitmq_config_t* config, abe_rabbitmq_t** out_rabbitmq)
{
    abe_mem_pool_config_t pool_config;
    abe_mem_pool_t* mem_pool;
    abe_rabbitmq_t* rabbitmq;
    amqp_rpc_reply_t reply;
    const char* host;
    const char* virtual_host;
    const char* user;
    const char* password;
    uint16_t port;
    uint32_t frame_max;
    uint16_t heartbeat;
    int status;

    if (config == NULL || out_rabbitmq == NULL) {
        return ABE_RABBITMQ_INVALID_ARG;
    }

    *out_rabbitmq = NULL;
    host = config->host == NULL ? "127.0.0.1" : config->host;
    port = config->port == 0u ? 5672u : config->port;
    virtual_host = config->virtual_host == NULL ? "/" : config->virtual_host;
    user = config->user == NULL ? "guest" : config->user;
    password = config->password == NULL ? "guest" : config->password;
    frame_max = config->frame_max == 0u ? 131072u : config->frame_max;
    heartbeat = config->heartbeat_seconds;

    memset(&pool_config, 0, sizeof(pool_config));
    pool_config.capacity = abe_rabbitmq_pool_capacity(config->memory_pool_capacity);
    pool_config.name = "abe_rabbitmq";
    mem_pool = NULL;
    status = abe_mem_pool_create(&pool_config, &mem_pool);
    if (status != ABE_MEM_POOL_OK) {
        return ABE_RABBITMQ_NO_MEMORY;
    }

    rabbitmq = (abe_rabbitmq_t*)abe_mem_pool_calloc(mem_pool, 1u, sizeof(*rabbitmq));
    if (rabbitmq == NULL) {
        abe_mem_pool_destroy(mem_pool);
        return ABE_RABBITMQ_NO_MEMORY;
    }
    rabbitmq->mem_pool = mem_pool;
    rabbitmq->owns_mem_pool = 1;
    rabbitmq->channel = config->channel == 0u ? 1u : config->channel;

    rabbitmq->conn = amqp_new_connection();
    if (rabbitmq->conn == NULL) {
        abe_rabbitmq_copy_error(rabbitmq, "rabbitmq connection allocation failed");
        abe_mem_pool_destroy(mem_pool);
        return ABE_RABBITMQ_NO_MEMORY;
    }
    rabbitmq->socket = amqp_tcp_socket_new(rabbitmq->conn);
    if (rabbitmq->socket == NULL) {
        abe_rabbitmq_copy_error(rabbitmq, "rabbitmq socket allocation failed");
        amqp_destroy_connection(rabbitmq->conn);
        rabbitmq->conn = NULL;
        abe_mem_pool_destroy(mem_pool);
        return ABE_RABBITMQ_NO_MEMORY;
    }

    status = amqp_socket_open(rabbitmq->socket, host, (int)port);
    if (status != AMQP_STATUS_OK) {
        abe_rabbitmq_copy_error(rabbitmq, amqp_error_string2(status));
        amqp_destroy_connection(rabbitmq->conn);
        rabbitmq->conn = NULL;
        abe_mem_pool_destroy(mem_pool);
        return ABE_RABBITMQ_CONNECT_FAILED;
    }

    reply = amqp_login(
        rabbitmq->conn,
        virtual_host,
        0,
        frame_max,
        heartbeat,
        AMQP_SASL_METHOD_PLAIN,
        user,
        password);
    status = abe_rabbitmq_check_rpc(rabbitmq, reply, "rabbitmq login failed");
    if (status != ABE_RABBITMQ_OK) {
        amqp_destroy_connection(rabbitmq->conn);
        rabbitmq->conn = NULL;
        abe_mem_pool_destroy(mem_pool);
        return status;
    }

    amqp_channel_open(rabbitmq->conn, rabbitmq->channel);
    status = abe_rabbitmq_check_rpc(
        rabbitmq,
        amqp_get_rpc_reply(rabbitmq->conn),
        "rabbitmq channel open failed");
    if (status != ABE_RABBITMQ_OK) {
        amqp_connection_close(rabbitmq->conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(rabbitmq->conn);
        rabbitmq->conn = NULL;
        abe_mem_pool_destroy(mem_pool);
        return status;
    }

    *out_rabbitmq = rabbitmq;
    return ABE_RABBITMQ_OK;
}

int abe_rabbitmq_declare_exchange(
    abe_rabbitmq_t* rabbitmq,
    const char* exchange,
    const char* type,
    int durable,
    int auto_delete)
{
    if (rabbitmq == NULL || rabbitmq->conn == NULL || exchange == NULL || type == NULL) {
        return ABE_RABBITMQ_INVALID_ARG;
    }

    (void)amqp_exchange_declare(
        rabbitmq->conn,
        rabbitmq->channel,
        amqp_cstring_bytes(exchange),
        amqp_cstring_bytes(type),
        0,
        durable != 0,
        auto_delete != 0,
        0,
        amqp_empty_table);
    return abe_rabbitmq_check_rpc(
        rabbitmq,
        amqp_get_rpc_reply(rabbitmq->conn),
        "rabbitmq exchange declare failed");
}

int abe_rabbitmq_declare_queue(
    abe_rabbitmq_t* rabbitmq,
    const char* queue,
    int durable,
    int exclusive,
    int auto_delete)
{
    if (rabbitmq == NULL || rabbitmq->conn == NULL || queue == NULL) {
        return ABE_RABBITMQ_INVALID_ARG;
    }

    (void)amqp_queue_declare(
        rabbitmq->conn,
        rabbitmq->channel,
        amqp_cstring_bytes(queue),
        0,
        durable != 0,
        exclusive != 0,
        auto_delete != 0,
        amqp_empty_table);
    return abe_rabbitmq_check_rpc(
        rabbitmq,
        amqp_get_rpc_reply(rabbitmq->conn),
        "rabbitmq queue declare failed");
}

int abe_rabbitmq_bind_queue(
    abe_rabbitmq_t* rabbitmq,
    const char* queue,
    const char* exchange,
    const char* routing_key)
{
    if (rabbitmq == NULL || rabbitmq->conn == NULL ||
        queue == NULL || exchange == NULL || routing_key == NULL) {
        return ABE_RABBITMQ_INVALID_ARG;
    }

    (void)amqp_queue_bind(
        rabbitmq->conn,
        rabbitmq->channel,
        amqp_cstring_bytes(queue),
        amqp_cstring_bytes(exchange),
        amqp_cstring_bytes(routing_key),
        amqp_empty_table);
    return abe_rabbitmq_check_rpc(
        rabbitmq,
        amqp_get_rpc_reply(rabbitmq->conn),
        "rabbitmq queue bind failed");
}

int abe_rabbitmq_publish(
    abe_rabbitmq_t* rabbitmq,
    const char* exchange,
    const char* routing_key,
    const void* body,
    uint64_t body_size,
    const char* content_type,
    int persistent)
{
    amqp_basic_properties_t props;
    amqp_bytes_t body_bytes;
    int status;

    if (rabbitmq == NULL || rabbitmq->conn == NULL ||
        routing_key == NULL || (body == NULL && body_size != 0u) ||
        !abe_rabbitmq_body_size_valid(body_size)) {
        return ABE_RABBITMQ_INVALID_ARG;
    }

    memset(&props, 0, sizeof(props));
    props._flags = AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.delivery_mode = persistent != 0 ? 2u : 1u;
    if (content_type != NULL) {
        props._flags |= AMQP_BASIC_CONTENT_TYPE_FLAG;
        props.content_type = amqp_cstring_bytes(content_type);
    }

    body_bytes.len = (size_t)body_size;
    body_bytes.bytes = (void*)body;
    status = amqp_basic_publish(
        rabbitmq->conn,
        rabbitmq->channel,
        abe_rabbitmq_cstring_bytes(exchange),
        amqp_cstring_bytes(routing_key),
        0,
        0,
        &props,
        body_bytes);
    if (status != AMQP_STATUS_OK) {
        abe_rabbitmq_copy_error(rabbitmq, amqp_error_string2(status));
        return ABE_RABBITMQ_SEND_FAILED;
    }
    abe_rabbitmq_copy_error(rabbitmq, "");
    return ABE_RABBITMQ_OK;
}

int abe_rabbitmq_consume(
    abe_rabbitmq_t* rabbitmq,
    const char* queue,
    const char* consumer_tag,
    int auto_ack)
{
    if (rabbitmq == NULL || rabbitmq->conn == NULL || queue == NULL) {
        return ABE_RABBITMQ_INVALID_ARG;
    }

    (void)amqp_basic_consume(
        rabbitmq->conn,
        rabbitmq->channel,
        amqp_cstring_bytes(queue),
        abe_rabbitmq_cstring_bytes(consumer_tag),
        0,
        auto_ack != 0,
        0,
        amqp_empty_table);
    return abe_rabbitmq_check_rpc(
        rabbitmq,
        amqp_get_rpc_reply(rabbitmq->conn),
        "rabbitmq consume setup failed");
}

int abe_rabbitmq_poll(
    abe_rabbitmq_t* rabbitmq,
    int timeout_ms,
    abe_rabbitmq_message_t** out_message)
{
    abe_rabbitmq_message_t* message;
    amqp_rpc_reply_t reply;
    struct timeval timeout;
    struct timeval* timeout_ptr;

    if (rabbitmq == NULL || rabbitmq->conn == NULL || out_message == NULL) {
        return ABE_RABBITMQ_INVALID_ARG;
    }

    *out_message = NULL;
    message = (abe_rabbitmq_message_t*)abe_mem_pool_calloc(
        rabbitmq->mem_pool,
        1u,
        sizeof(*message));
    if (message == NULL) {
        return ABE_RABBITMQ_NO_MEMORY;
    }

    timeout_ptr = NULL;
    if (timeout_ms >= 0) {
        abe_rabbitmq_timeval_from_ms(timeout_ms, &timeout);
        timeout_ptr = &timeout;
    }

    amqp_maybe_release_buffers(rabbitmq->conn);
    reply = amqp_consume_message(rabbitmq->conn, &message->envelope, timeout_ptr, 0);
    if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
        reply.library_error == AMQP_STATUS_TIMEOUT) {
        (void)abe_mem_pool_free(rabbitmq->mem_pool, message);
        return ABE_RABBITMQ_NO_MESSAGE;
    }
    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
        (void)abe_mem_pool_free(rabbitmq->mem_pool, message);
        return abe_rabbitmq_check_rpc(rabbitmq, reply, "rabbitmq consume failed");
    }

    message->owner = rabbitmq;
    *out_message = message;
    return ABE_RABBITMQ_OK;
}

int abe_rabbitmq_ack(abe_rabbitmq_t* rabbitmq, uint64_t delivery_tag)
{
    int status;

    if (rabbitmq == NULL || rabbitmq->conn == NULL) {
        return ABE_RABBITMQ_INVALID_ARG;
    }
    status = amqp_basic_ack(rabbitmq->conn, rabbitmq->channel, delivery_tag, 0);
    if (status != AMQP_STATUS_OK) {
        abe_rabbitmq_copy_error(rabbitmq, amqp_error_string2(status));
        return ABE_RABBITMQ_PROTOCOL_ERROR;
    }
    return ABE_RABBITMQ_OK;
}

int abe_rabbitmq_reject(abe_rabbitmq_t* rabbitmq, uint64_t delivery_tag, int requeue)
{
    int status;

    if (rabbitmq == NULL || rabbitmq->conn == NULL) {
        return ABE_RABBITMQ_INVALID_ARG;
    }
    status = amqp_basic_reject(rabbitmq->conn, rabbitmq->channel, delivery_tag, requeue != 0);
    if (status != AMQP_STATUS_OK) {
        abe_rabbitmq_copy_error(rabbitmq, amqp_error_string2(status));
        return ABE_RABBITMQ_PROTOCOL_ERROR;
    }
    return ABE_RABBITMQ_OK;
}

const char* abe_rabbitmq_last_error(const abe_rabbitmq_t* rabbitmq)
{
    return rabbitmq == NULL ? "" : rabbitmq->last_error;
}

void abe_rabbitmq_destroy(abe_rabbitmq_t* rabbitmq)
{
    abe_mem_pool_t* mem_pool;
    int owns_mem_pool;

    if (rabbitmq == NULL) {
        return;
    }
    mem_pool = rabbitmq->mem_pool;
    owns_mem_pool = rabbitmq->owns_mem_pool;
    if (rabbitmq->conn != NULL) {
        (void)amqp_channel_close(rabbitmq->conn, rabbitmq->channel, AMQP_REPLY_SUCCESS);
        (void)amqp_connection_close(rabbitmq->conn, AMQP_REPLY_SUCCESS);
        (void)amqp_destroy_connection(rabbitmq->conn);
        rabbitmq->conn = NULL;
    }
    if (mem_pool != NULL) {
        (void)abe_mem_pool_free(mem_pool, rabbitmq);
        if (owns_mem_pool != 0) {
            abe_mem_pool_destroy(mem_pool);
        }
    }
}

int abe_rabbitmq_message_view(
    const abe_rabbitmq_message_t* message,
    abe_rabbitmq_message_view_t* out_view)
{
    if (message == NULL || out_view == NULL) {
        return ABE_RABBITMQ_INVALID_ARG;
    }

    memset(out_view, 0, sizeof(*out_view));
    out_view->body = message->envelope.message.body.bytes;
    out_view->body_size = (uint64_t)message->envelope.message.body.len;
    out_view->exchange = (const char*)message->envelope.exchange.bytes;
    out_view->exchange_size = (uint64_t)message->envelope.exchange.len;
    out_view->routing_key = (const char*)message->envelope.routing_key.bytes;
    out_view->routing_key_size = (uint64_t)message->envelope.routing_key.len;
    out_view->delivery_tag = message->envelope.delivery_tag;
    out_view->redelivered = message->envelope.redelivered;
    return ABE_RABBITMQ_OK;
}

void abe_rabbitmq_message_destroy(abe_rabbitmq_message_t* message)
{
    abe_mem_pool_t* mem_pool;

    if (message == NULL) {
        return;
    }
    mem_pool = message->owner == NULL ? NULL : message->owner->mem_pool;
    amqp_destroy_envelope(&message->envelope);
    if (mem_pool != NULL) {
        (void)abe_mem_pool_free(mem_pool, message);
    }
}
