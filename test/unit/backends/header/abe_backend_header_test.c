#include "abe_db_mysql.h"
#include "abe_db_mysql_async.h"
#include "abe_error.h"
#include "abe_kafka.h"
#include "abe_rabbitmq.h"
#include "abe_redis.h"
#include "abe_redis_async.h"

#include <stddef.h>
#include <stdint.h>

int main(void)
{
    abe_db_mysql_config_t mysql_config;
    abe_db_mysql_async_config_t mysql_async_config;
    abe_redis_config_t redis_config;
    abe_kafka_producer_config_t kafka_producer_config;
    abe_kafka_consumer_config_t kafka_consumer_config;
    abe_rabbitmq_config_t rabbitmq_config;

    (void)sizeof(abe_db_t*);
    (void)sizeof(abe_db_mysql_async_t*);
    (void)sizeof(abe_db_mysql_async_result_t*);
    (void)sizeof(abe_redis_t*);
    (void)sizeof(abe_redis_reply_t*);
    (void)sizeof(abe_redis_async_t*);
    (void)sizeof(abe_kafka_producer_t*);
    (void)sizeof(abe_kafka_consumer_t*);
    (void)sizeof(abe_kafka_message_t*);
    (void)sizeof(abe_rabbitmq_t*);
    (void)sizeof(abe_rabbitmq_message_t*);

    if (ABE_REDIS_CONNECT_FAILED != ABE_CONNECT_FAILED) {
        return 1;
    }
    if (ABE_KAFKA_SEND_FAILED != ABE_SEND_FAILED) {
        return 1;
    }
    if (ABE_RABBITMQ_PROTOCOL_ERROR != ABE_PARSE_ERROR) {
        return 1;
    }

    mysql_config.host = NULL;
    mysql_config.port = 0u;
    mysql_config.database = NULL;
    mysql_config.user = NULL;
    mysql_config.password = NULL;
    mysql_config.unix_socket = NULL;
    mysql_config.charset = NULL;
    mysql_config.connect_timeout_seconds = 0u;
    mysql_config.read_timeout_seconds = 0u;
    mysql_config.write_timeout_seconds = 0u;
    mysql_config.memory_pool_capacity = 0u;
    mysql_config.client_flags = 0ul;
    mysql_config.reconnect = 0;

    mysql_async_config.mysql = mysql_config;
    mysql_async_config.worker_count = 1u;
    mysql_async_config.queue_capacity = 1u;

    redis_config.host = NULL;
    redis_config.port = 0u;
    redis_config.password = NULL;
    redis_config.database = -1;
    redis_config.connect_timeout_ms = 0u;
    redis_config.command_timeout_ms = 0u;
    redis_config.memory_pool_capacity = 0u;

    kafka_producer_config.brokers = NULL;
    kafka_producer_config.client_id = NULL;
    kafka_producer_config.properties = NULL;
    kafka_producer_config.property_count = 0u;
    kafka_producer_config.memory_pool_capacity = 0u;

    kafka_consumer_config.brokers = NULL;
    kafka_consumer_config.group_id = NULL;
    kafka_consumer_config.client_id = NULL;
    kafka_consumer_config.auto_offset_reset = NULL;
    kafka_consumer_config.properties = NULL;
    kafka_consumer_config.property_count = 0u;
    kafka_consumer_config.memory_pool_capacity = 0u;

    rabbitmq_config.host = NULL;
    rabbitmq_config.port = 0u;
    rabbitmq_config.virtual_host = NULL;
    rabbitmq_config.user = NULL;
    rabbitmq_config.password = NULL;
    rabbitmq_config.channel = 0u;
    rabbitmq_config.frame_max = 0u;
    rabbitmq_config.heartbeat_seconds = 0u;
    rabbitmq_config.memory_pool_capacity = 0u;

    return ABE_KAFKA_PARTITION_ANY == -1 ? 0 : 1;
}
