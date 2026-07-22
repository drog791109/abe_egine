#ifndef ABE_DB_MYSQL_H
#define ABE_DB_MYSQL_H

#include "abe_db.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct abe_db_mysql_config {
    const char* host;
    uint16_t port;
    const char* database;
    const char* user;
    const char* password;
    const char* unix_socket;
    const char* charset;
    uint32_t connect_timeout_seconds;
    uint32_t read_timeout_seconds;
    uint32_t write_timeout_seconds;
    uint64_t memory_pool_capacity;
    unsigned long client_flags;
    int reconnect;
} abe_db_mysql_config_t;

int abe_db_mysql_library_init(void);
void abe_db_mysql_library_end(void);

/*
 * Creates a MySQL-backed abe_db_t. The returned handle owns the connection and
 * must be destroyed with abe_db_destroy.
 */
int abe_db_mysql_create(const abe_db_mysql_config_t* config, abe_db_t** out_db);

#ifdef __cplusplus
}
#endif

#endif /* ABE_DB_MYSQL_H */
