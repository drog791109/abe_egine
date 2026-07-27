#include "abe_db_mysql_async.h"

#include <mysql.h>

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ABE_DB_MYSQL_ASYNC_DEFAULT_WORKERS 1u
#define ABE_DB_MYSQL_ASYNC_DEFAULT_QUEUE_CAPACITY 1024u
#define ABE_DB_MYSQL_ASYNC_MAX_WORKERS 32u
#define ABE_DB_MYSQL_ASYNC_ERROR_SIZE 512u

enum abe_db_mysql_async_task_type {
    ABE_DB_MYSQL_ASYNC_TASK_QUERY = 1,
    ABE_DB_MYSQL_ASYNC_TASK_EXECUTE = 2
};

struct abe_db_mysql_async_cell {
    void* data;
    uint64_t size;
    int is_null;
};

struct abe_db_mysql_async_result {
    uint32_t column_count;
    uint64_t row_count;
    char** column_names;
    struct abe_db_mysql_async_cell* cells;
};

struct abe_db_mysql_async_task {
    int type;
    char* sql;
    abe_db_mysql_async_query_callback_fn query_callback;
    abe_db_mysql_async_execute_callback_fn execute_callback;
    void* user_data;
    int status;
    uint64_t affected_rows;
    struct abe_db_mysql_async_result* result;
    char error_message[ABE_DB_MYSQL_ASYNC_ERROR_SIZE];
    struct abe_db_mysql_async_task* next;
};

struct abe_db_mysql_async_worker {
    pthread_t thread;
    struct abe_db_mysql_async* owner;
    int created;
};

struct abe_db_mysql_async {
    abe_db_mysql_async_config_t config;
    char* host;
    char* database;
    char* user;
    char* password;
    char* unix_socket;
    char* charset;
    struct abe_db_mysql_async_worker* workers;
    uint32_t worker_count;
    uint32_t queue_capacity;
    uint32_t started_workers;
    uint32_t connected_workers;
    uint32_t pending_count;
    int stopping;
    int library_ready;
    pthread_mutex_t mutex;
    pthread_cond_t request_cond;
    pthread_cond_t start_cond;
    struct abe_db_mysql_async_task* request_head;
    struct abe_db_mysql_async_task* request_tail;
    struct abe_db_mysql_async_task* complete_head;
    struct abe_db_mysql_async_task* complete_tail;
    char last_error[ABE_DB_MYSQL_ASYNC_ERROR_SIZE];
};

static char* abe_db_mysql_async_copy_text(const char* value)
{
    char* copy;
    size_t size;

    if (value == NULL) {
        return NULL;
    }
    size = strlen(value) + 1u;
    copy = (char*)malloc(size);
    if (copy != NULL) {
        memcpy(copy, value, size);
    }
    return copy;
}

static void abe_db_mysql_async_copy_error(char* output, const char* value)
{
    const char* text;

    if (output == NULL) {
        return;
    }
    text = value == NULL || value[0] == '\0' ? "mysql async operation failed" : value;
    (void)strncpy(output, text, ABE_DB_MYSQL_ASYNC_ERROR_SIZE - 1u);
    output[ABE_DB_MYSQL_ASYNC_ERROR_SIZE - 1u] = '\0';
}

static void abe_db_mysql_async_set_last_error(
    abe_db_mysql_async_t* mysql,
    const char* value)
{
    if (mysql == NULL) {
        return;
    }
    (void)pthread_mutex_lock(&mysql->mutex);
    abe_db_mysql_async_copy_error(mysql->last_error, value);
    (void)pthread_mutex_unlock(&mysql->mutex);
}

static int abe_db_mysql_async_copy_config(
    abe_db_mysql_async_t* mysql,
    const abe_db_mysql_async_config_t* config)
{
    if (mysql == NULL || config == NULL || config->mysql.user == NULL ||
        config->mysql.user[0] == '\0') {
        return ABE_DB_INVALID_ARG;
    }

    mysql->host = abe_db_mysql_async_copy_text(config->mysql.host);
    mysql->database = abe_db_mysql_async_copy_text(config->mysql.database);
    mysql->user = abe_db_mysql_async_copy_text(config->mysql.user);
    mysql->password = abe_db_mysql_async_copy_text(config->mysql.password);
    mysql->unix_socket = abe_db_mysql_async_copy_text(config->mysql.unix_socket);
    mysql->charset = abe_db_mysql_async_copy_text(config->mysql.charset);
    if (mysql->user == NULL ||
        (config->mysql.host != NULL && mysql->host == NULL) ||
        (config->mysql.database != NULL && mysql->database == NULL) ||
        (config->mysql.password != NULL && mysql->password == NULL) ||
        (config->mysql.unix_socket != NULL && mysql->unix_socket == NULL) ||
        (config->mysql.charset != NULL && mysql->charset == NULL)) {
        return ABE_DB_NO_MEMORY;
    }

    mysql->config = *config;
    mysql->config.mysql.host = mysql->host;
    mysql->config.mysql.database = mysql->database;
    mysql->config.mysql.user = mysql->user;
    mysql->config.mysql.password = mysql->password;
    mysql->config.mysql.unix_socket = mysql->unix_socket;
    mysql->config.mysql.charset = mysql->charset;
    return ABE_DB_OK;
}

static void abe_db_mysql_async_free_config(abe_db_mysql_async_t* mysql)
{
    if (mysql == NULL) {
        return;
    }
    free(mysql->host);
    free(mysql->database);
    free(mysql->user);
    free(mysql->password);
    free(mysql->unix_socket);
    free(mysql->charset);
    mysql->host = NULL;
    mysql->database = NULL;
    mysql->user = NULL;
    mysql->password = NULL;
    mysql->unix_socket = NULL;
    mysql->charset = NULL;
}

static void abe_db_mysql_async_result_destroy(struct abe_db_mysql_async_result* result)
{
    uint64_t cell_count;
    uint64_t index;
    uint32_t column;

    if (result == NULL) {
        return;
    }
    cell_count = result->row_count * (uint64_t)result->column_count;
    for (index = 0u; index < cell_count; ++index) {
        free(result->cells[index].data);
    }
    for (column = 0u; column < result->column_count; ++column) {
        free(result->column_names[column]);
    }
    free(result->cells);
    free(result->column_names);
    free(result);
}

static int abe_db_mysql_async_result_cell_index(
    const struct abe_db_mysql_async_result* result,
    uint64_t row,
    uint32_t column,
    uint64_t* out_index)
{
    if (result == NULL || out_index == NULL || row >= result->row_count ||
        column >= result->column_count) {
        return ABE_DB_OUT_OF_RANGE;
    }
    *out_index = row * (uint64_t)result->column_count + (uint64_t)column;
    return ABE_DB_OK;
}

static int abe_db_mysql_async_clone_result(
    abe_db_result_t* source,
    struct abe_db_mysql_async_result** out_result)
{
    struct abe_db_mysql_async_result* result;
    uint64_t cell_count;
    uint64_t row;
    uint32_t column;
    int has_row;
    int status;

    if (source == NULL || out_result == NULL) {
        return ABE_DB_INVALID_ARG;
    }
    *out_result = NULL;

    result = (struct abe_db_mysql_async_result*)calloc(1u, sizeof(*result));
    if (result == NULL) {
        return ABE_DB_NO_MEMORY;
    }
    result->column_count = abe_db_result_column_count(source);
    result->row_count = abe_db_result_row_count(source);
    if (result->column_count != 0u &&
        result->row_count > (uint64_t)((size_t)-1) / (uint64_t)result->column_count) {
        abe_db_mysql_async_result_destroy(result);
        return ABE_DB_NO_MEMORY;
    }
    cell_count = result->row_count * (uint64_t)result->column_count;
    if (result->column_count != 0u) {
        result->column_names = (char**)calloc(result->column_count, sizeof(*result->column_names));
        if (result->column_names == NULL) {
            abe_db_mysql_async_result_destroy(result);
            return ABE_DB_NO_MEMORY;
        }
    }
    if (cell_count != 0u) {
        result->cells = (struct abe_db_mysql_async_cell*)calloc(
            (size_t)cell_count,
            sizeof(*result->cells));
        if (result->cells == NULL) {
            abe_db_mysql_async_result_destroy(result);
            return ABE_DB_NO_MEMORY;
        }
    }

    for (column = 0u; column < result->column_count; ++column) {
        const char* name;

        name = abe_db_result_column_name(source, column);
        result->column_names[column] = abe_db_mysql_async_copy_text(name == NULL ? "" : name);
        if (result->column_names[column] == NULL) {
            abe_db_mysql_async_result_destroy(result);
            return ABE_DB_NO_MEMORY;
        }
    }

    row = 0u;
    for (;;) {
        has_row = 0;
        status = abe_db_result_next(source, &has_row);
        if (status != ABE_DB_OK) {
            abe_db_mysql_async_result_destroy(result);
            return status;
        }
        if (!has_row) {
            break;
        }
        if (row >= result->row_count) {
            abe_db_mysql_async_result_destroy(result);
            return ABE_DB_QUERY_FAILED;
        }

        for (column = 0u; column < result->column_count; ++column) {
            struct abe_db_mysql_async_cell* cell;
            const void* data;
            uint64_t size;
            int is_null;

            cell = &result->cells[row * (uint64_t)result->column_count + (uint64_t)column];
            is_null = 0;
            status = abe_db_result_is_null(source, column, &is_null);
            if (status != ABE_DB_OK) {
                abe_db_mysql_async_result_destroy(result);
                return status;
            }
            cell->is_null = is_null;
            if (is_null) {
                continue;
            }

            size = 0u;
            data = abe_db_result_get_blob(source, column, &size);
            if (data == NULL && size != 0u) {
                abe_db_mysql_async_result_destroy(result);
                return ABE_DB_QUERY_FAILED;
            }
            cell->size = size;
            if (size != 0u) {
                cell->data = malloc((size_t)size);
                if (cell->data == NULL) {
                    abe_db_mysql_async_result_destroy(result);
                    return ABE_DB_NO_MEMORY;
                }
                memcpy(cell->data, data, (size_t)size);
            }
        }
        ++row;
    }

    if (row != result->row_count) {
        abe_db_mysql_async_result_destroy(result);
        return ABE_DB_QUERY_FAILED;
    }
    *out_result = result;
    return ABE_DB_OK;
}

static void abe_db_mysql_async_task_destroy(struct abe_db_mysql_async_task* task)
{
    if (task == NULL) {
        return;
    }
    free(task->sql);
    abe_db_mysql_async_result_destroy(task->result);
    free(task);
}

static void abe_db_mysql_async_task_list_destroy(struct abe_db_mysql_async_task* task)
{
    while (task != NULL) {
        struct abe_db_mysql_async_task* next;

        next = task->next;
        abe_db_mysql_async_task_destroy(task);
        task = next;
    }
}

static struct abe_db_mysql_async_task* abe_db_mysql_async_pop_request(
    abe_db_mysql_async_t* mysql)
{
    struct abe_db_mysql_async_task* task;

    task = mysql->request_head;
    if (task != NULL) {
        mysql->request_head = task->next;
        if (mysql->request_head == NULL) {
            mysql->request_tail = NULL;
        }
        task->next = NULL;
    }
    return task;
}

static void abe_db_mysql_async_push_complete(
    abe_db_mysql_async_t* mysql,
    struct abe_db_mysql_async_task* task)
{
    (void)pthread_mutex_lock(&mysql->mutex);
    if (mysql->complete_tail == NULL) {
        mysql->complete_head = task;
    } else {
        mysql->complete_tail->next = task;
    }
    mysql->complete_tail = task;
    (void)pthread_mutex_unlock(&mysql->mutex);
}

static void abe_db_mysql_async_run_task(
    abe_db_t* db,
    struct abe_db_mysql_async_task* task)
{
    abe_db_result_t* source;
    int status;

    task->status = ABE_DB_ERROR;
    task->affected_rows = 0u;
    task->result = NULL;
    task->error_message[0] = '\0';
    if (db == NULL) {
        task->status = ABE_DB_NOT_CONNECTED;
        abe_db_mysql_async_copy_error(task->error_message, "mysql worker is not connected");
        return;
    }

    if (task->type == ABE_DB_MYSQL_ASYNC_TASK_EXECUTE) {
        status = abe_db_execute(db, task->sql, &task->affected_rows);
        task->status = status;
        if (status != ABE_DB_OK) {
            abe_db_mysql_async_copy_error(task->error_message, abe_db_last_error(db));
        }
        return;
    }

    source = NULL;
    status = abe_db_query(db, task->sql, &source);
    if (status == ABE_DB_OK) {
        status = abe_db_mysql_async_clone_result(source, &task->result);
    }
    if (source != NULL) {
        abe_db_result_destroy(source);
    }
    task->status = status;
    if (status != ABE_DB_OK) {
        abe_db_mysql_async_copy_error(task->error_message, abe_db_last_error(db));
        if (task->error_message[0] == '\0') {
            abe_db_mysql_async_copy_error(task->error_message, "mysql result copy failed");
        }
    }
}

static void* abe_db_mysql_async_worker_main(void* user_data)
{
    struct abe_db_mysql_async_worker* worker;
    abe_db_mysql_async_t* mysql;
    abe_db_t* db;
    int thread_ready;
    int connection_status;

    worker = (struct abe_db_mysql_async_worker*)user_data;
    mysql = worker->owner;
    db = NULL;
    thread_ready = mysql_thread_init() == 0 ? 1 : 0;
    connection_status = thread_ready ? abe_db_mysql_create(&mysql->config.mysql, &db) : ABE_DB_ERROR;

    (void)pthread_mutex_lock(&mysql->mutex);
    ++mysql->started_workers;
    if (connection_status == ABE_DB_OK) {
        ++mysql->connected_workers;
    }
    (void)pthread_cond_broadcast(&mysql->start_cond);
    (void)pthread_mutex_unlock(&mysql->mutex);

    if (connection_status != ABE_DB_OK) {
        if (thread_ready) {
            mysql_thread_end();
        }
        return NULL;
    }

    for (;;) {
        struct abe_db_mysql_async_task* task;

        (void)pthread_mutex_lock(&mysql->mutex);
        while (!mysql->stopping && mysql->request_head == NULL) {
            (void)pthread_cond_wait(&mysql->request_cond, &mysql->mutex);
        }
        if (mysql->stopping) {
            (void)pthread_mutex_unlock(&mysql->mutex);
            break;
        }
        task = abe_db_mysql_async_pop_request(mysql);
        (void)pthread_mutex_unlock(&mysql->mutex);

        abe_db_mysql_async_run_task(db, task);
        free(task->sql);
        task->sql = NULL;
        abe_db_mysql_async_push_complete(mysql, task);
    }

    abe_db_destroy(db);
    mysql_thread_end();
    return NULL;
}

int abe_db_mysql_async_create(
    const abe_db_mysql_async_config_t* config,
    abe_db_mysql_async_t** out_mysql)
{
    abe_db_mysql_async_t* mysql;
    uint32_t index;
    int status;

    if (out_mysql == NULL || config == NULL) {
        return ABE_DB_INVALID_ARG;
    }
    *out_mysql = NULL;
    if (config->worker_count > ABE_DB_MYSQL_ASYNC_MAX_WORKERS) {
        return ABE_DB_INVALID_ARG;
    }

    mysql = (abe_db_mysql_async_t*)calloc(1u, sizeof(*mysql));
    if (mysql == NULL) {
        return ABE_DB_NO_MEMORY;
    }
    if (pthread_mutex_init(&mysql->mutex, NULL) != 0) {
        free(mysql);
        return ABE_DB_ERROR;
    }
    if (pthread_cond_init(&mysql->request_cond, NULL) != 0) {
        (void)pthread_mutex_destroy(&mysql->mutex);
        free(mysql);
        return ABE_DB_ERROR;
    }
    if (pthread_cond_init(&mysql->start_cond, NULL) != 0) {
        (void)pthread_cond_destroy(&mysql->request_cond);
        (void)pthread_mutex_destroy(&mysql->mutex);
        free(mysql);
        return ABE_DB_ERROR;
    }

    status = abe_db_mysql_async_copy_config(mysql, config);
    if (status != ABE_DB_OK) {
        abe_db_mysql_async_free_config(mysql);
        (void)pthread_cond_destroy(&mysql->start_cond);
        (void)pthread_cond_destroy(&mysql->request_cond);
        (void)pthread_mutex_destroy(&mysql->mutex);
        free(mysql);
        return status;
    }
    mysql->worker_count = config->worker_count == 0u ?
        ABE_DB_MYSQL_ASYNC_DEFAULT_WORKERS : config->worker_count;
    mysql->queue_capacity = config->queue_capacity == 0u ?
        ABE_DB_MYSQL_ASYNC_DEFAULT_QUEUE_CAPACITY : config->queue_capacity;
    if (mysql->queue_capacity == 0u) {
        abe_db_mysql_async_destroy(mysql);
        return ABE_DB_INVALID_ARG;
    }

    status = abe_db_mysql_library_init();
    if (status != ABE_DB_OK) {
        abe_db_mysql_async_destroy(mysql);
        return status;
    }
    mysql->library_ready = 1;

    mysql->workers = (struct abe_db_mysql_async_worker*)calloc(
        mysql->worker_count,
        sizeof(*mysql->workers));
    if (mysql->workers == NULL) {
        abe_db_mysql_async_destroy(mysql);
        return ABE_DB_NO_MEMORY;
    }

    for (index = 0u; index < mysql->worker_count; ++index) {
        mysql->workers[index].owner = mysql;
        if (pthread_create(
                &mysql->workers[index].thread,
                NULL,
                abe_db_mysql_async_worker_main,
                &mysql->workers[index]) != 0) {
            abe_db_mysql_async_set_last_error(mysql, "mysql worker creation failed");
            abe_db_mysql_async_destroy(mysql);
            return ABE_DB_ERROR;
        }
        mysql->workers[index].created = 1;
    }

    (void)pthread_mutex_lock(&mysql->mutex);
    while (mysql->started_workers < mysql->worker_count) {
        (void)pthread_cond_wait(&mysql->start_cond, &mysql->mutex);
    }
    status = mysql->connected_workers == 0u ? ABE_DB_NOT_CONNECTED : ABE_DB_OK;
    (void)pthread_mutex_unlock(&mysql->mutex);
    if (status != ABE_DB_OK) {
        abe_db_mysql_async_set_last_error(mysql, "mysql workers could not connect");
        abe_db_mysql_async_destroy(mysql);
        return status;
    }

    *out_mysql = mysql;
    return ABE_DB_OK;
}

void abe_db_mysql_async_destroy(abe_db_mysql_async_t* mysql)
{
    uint32_t index;
    struct abe_db_mysql_async_task* requests;
    struct abe_db_mysql_async_task* completions;

    if (mysql == NULL) {
        return;
    }
    (void)pthread_mutex_lock(&mysql->mutex);
    mysql->stopping = 1;
    (void)pthread_cond_broadcast(&mysql->request_cond);
    (void)pthread_mutex_unlock(&mysql->mutex);

    if (mysql->workers != NULL) {
        for (index = 0u; index < mysql->worker_count; ++index) {
            if (mysql->workers[index].created) {
                (void)pthread_join(mysql->workers[index].thread, NULL);
            }
        }
    }

    (void)pthread_mutex_lock(&mysql->mutex);
    requests = mysql->request_head;
    completions = mysql->complete_head;
    mysql->request_head = NULL;
    mysql->request_tail = NULL;
    mysql->complete_head = NULL;
    mysql->complete_tail = NULL;
    (void)pthread_mutex_unlock(&mysql->mutex);
    abe_db_mysql_async_task_list_destroy(requests);
    abe_db_mysql_async_task_list_destroy(completions);

    free(mysql->workers);
    if (mysql->library_ready) {
        abe_db_mysql_library_end();
    }
    abe_db_mysql_async_free_config(mysql);
    (void)pthread_cond_destroy(&mysql->start_cond);
    (void)pthread_cond_destroy(&mysql->request_cond);
    (void)pthread_mutex_destroy(&mysql->mutex);
    free(mysql);
}

static int abe_db_mysql_async_submit(
    abe_db_mysql_async_t* mysql,
    int type,
    const char* sql,
    abe_db_mysql_async_query_callback_fn query_callback,
    abe_db_mysql_async_execute_callback_fn execute_callback,
    void* user_data)
{
    struct abe_db_mysql_async_task* task;

    if (mysql == NULL || sql == NULL || sql[0] == '\0' ||
        (type == ABE_DB_MYSQL_ASYNC_TASK_QUERY && query_callback == NULL) ||
        (type == ABE_DB_MYSQL_ASYNC_TASK_EXECUTE && execute_callback == NULL)) {
        return ABE_DB_INVALID_ARG;
    }

    task = (struct abe_db_mysql_async_task*)calloc(1u, sizeof(*task));
    if (task == NULL) {
        return ABE_DB_NO_MEMORY;
    }
    task->sql = abe_db_mysql_async_copy_text(sql);
    if (task->sql == NULL) {
        free(task);
        return ABE_DB_NO_MEMORY;
    }
    task->type = type;
    task->query_callback = query_callback;
    task->execute_callback = execute_callback;
    task->user_data = user_data;

    (void)pthread_mutex_lock(&mysql->mutex);
    if (mysql->stopping) {
        (void)pthread_mutex_unlock(&mysql->mutex);
        abe_db_mysql_async_task_destroy(task);
        return ABE_CLOSED;
    }
    if (mysql->pending_count >= mysql->queue_capacity) {
        (void)pthread_mutex_unlock(&mysql->mutex);
        abe_db_mysql_async_task_destroy(task);
        return ABE_WOULD_BLOCK;
    }
    if (mysql->request_tail == NULL) {
        mysql->request_head = task;
    } else {
        mysql->request_tail->next = task;
    }
    mysql->request_tail = task;
    ++mysql->pending_count;
    (void)pthread_cond_signal(&mysql->request_cond);
    (void)pthread_mutex_unlock(&mysql->mutex);
    return ABE_DB_OK;
}

int abe_db_mysql_async_query(
    abe_db_mysql_async_t* mysql,
    const char* sql,
    abe_db_mysql_async_query_callback_fn callback,
    void* user_data)
{
    return abe_db_mysql_async_submit(
        mysql,
        ABE_DB_MYSQL_ASYNC_TASK_QUERY,
        sql,
        callback,
        NULL,
        user_data);
}

int abe_db_mysql_async_execute(
    abe_db_mysql_async_t* mysql,
    const char* sql,
    abe_db_mysql_async_execute_callback_fn callback,
    void* user_data)
{
    return abe_db_mysql_async_submit(
        mysql,
        ABE_DB_MYSQL_ASYNC_TASK_EXECUTE,
        sql,
        NULL,
        callback,
        user_data);
}

int abe_db_mysql_async_update(
    abe_db_mysql_async_t* mysql,
    uint32_t max_count,
    uint32_t* out_count)
{
    uint32_t count;

    if (mysql == NULL) {
        return ABE_DB_INVALID_ARG;
    }
    if (out_count != NULL) {
        *out_count = 0u;
    }

    count = 0u;
    for (;;) {
        struct abe_db_mysql_async_task* task;

        if (max_count != 0u && count >= max_count) {
            break;
        }
        (void)pthread_mutex_lock(&mysql->mutex);
        task = mysql->complete_head;
        if (task != NULL) {
            mysql->complete_head = task->next;
            if (mysql->complete_head == NULL) {
                mysql->complete_tail = NULL;
            }
            task->next = NULL;
        }
        (void)pthread_mutex_unlock(&mysql->mutex);
        if (task == NULL) {
            break;
        }

        (void)pthread_mutex_lock(&mysql->mutex);
        if (mysql->pending_count > 0u) {
            --mysql->pending_count;
        }
        (void)pthread_mutex_unlock(&mysql->mutex);

        if (task->status != ABE_DB_OK) {
            abe_db_mysql_async_set_last_error(mysql, task->error_message);
        }
        if (task->type == ABE_DB_MYSQL_ASYNC_TASK_QUERY) {
            abe_db_mysql_async_query_result_t result;

            result.status = task->status;
            result.result = task->result;
            result.error_message = task->status == ABE_DB_OK ? "" : task->error_message;
            task->query_callback(mysql, &result, task->user_data);
        } else {
            abe_db_mysql_async_execute_result_t result;

            result.status = task->status;
            result.affected_rows = task->affected_rows;
            result.error_message = task->status == ABE_DB_OK ? "" : task->error_message;
            task->execute_callback(mysql, &result, task->user_data);
        }

        abe_db_mysql_async_task_destroy(task);
        ++count;
    }

    if (out_count != NULL) {
        *out_count = count;
    }
    return ABE_DB_OK;
}

uint32_t abe_db_mysql_async_pending_count(const abe_db_mysql_async_t* mysql)
{
    uint32_t count;
    abe_db_mysql_async_t* mutable_mysql;

    if (mysql == NULL) {
        return 0u;
    }
    mutable_mysql = (abe_db_mysql_async_t*)mysql;
    (void)pthread_mutex_lock(&mutable_mysql->mutex);
    count = mutable_mysql->pending_count;
    (void)pthread_mutex_unlock(&mutable_mysql->mutex);
    return count;
}

const char* abe_db_mysql_async_last_error(const abe_db_mysql_async_t* mysql)
{
    return mysql == NULL ? "" : mysql->last_error;
}

uint32_t abe_db_mysql_async_result_column_count(const abe_db_mysql_async_result_t* result)
{
    return result == NULL ? 0u : result->column_count;
}

uint64_t abe_db_mysql_async_result_row_count(const abe_db_mysql_async_result_t* result)
{
    return result == NULL ? 0u : result->row_count;
}

const char* abe_db_mysql_async_result_column_name(
    const abe_db_mysql_async_result_t* result,
    uint32_t column)
{
    if (result == NULL || column >= result->column_count) {
        return NULL;
    }
    return result->column_names[column];
}

int abe_db_mysql_async_result_is_null(
    const abe_db_mysql_async_result_t* result,
    uint64_t row,
    uint32_t column,
    int* out_is_null)
{
    uint64_t index;
    int status;

    if (out_is_null == NULL) {
        return ABE_DB_INVALID_ARG;
    }
    status = abe_db_mysql_async_result_cell_index(result, row, column, &index);
    if (status != ABE_DB_OK) {
        return status;
    }
    *out_is_null = result->cells[index].is_null;
    return ABE_DB_OK;
}

const void* abe_db_mysql_async_result_get_blob(
    const abe_db_mysql_async_result_t* result,
    uint64_t row,
    uint32_t column,
    uint64_t* out_size)
{
    uint64_t index;

    if (out_size != NULL) {
        *out_size = 0u;
    }
    if (abe_db_mysql_async_result_cell_index(result, row, column, &index) != ABE_DB_OK ||
        result->cells[index].is_null) {
        return NULL;
    }
    if (out_size != NULL) {
        *out_size = result->cells[index].size;
    }
    return result->cells[index].data;
}
