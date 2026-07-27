#include "abe_player_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <google/protobuf/message.h>
#include <limits.h>
#include <string>

namespace abe {
namespace service {
namespace store {

namespace pb = ::abe::proto::store;
namespace svc = ::abe::service::common;

PlayerStore::~PlayerStore()
{
}

MysqlPlayerStore::MysqlPlayerStore()
    : db_(NULL)
{
}

int MysqlPlayerStore::init(abe_db_t* db)
{
    if (db == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    db_ = db;
    return svc::SERVICE_STATUS_OK;
}

static uint32_t meta_schema_version(const pb::PB_STORE_META& meta)
{
    return meta.schema_version();
}

static uint64_t meta_data_version(const pb::PB_STORE_META& meta)
{
    return meta.data_version();
}

static uint64_t meta_created_at_ms(const pb::PB_STORE_META& meta)
{
    return meta.created_at_ms();
}

static uint64_t meta_updated_at_ms(const pb::PB_STORE_META& meta)
{
    return meta.updated_at_ms();
}

static int append_u64(std::string* output, uint64_t value)
{
    char buffer[32];
    int written;

    if (output == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    written = snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        return svc::SERVICE_STATUS_FAILED;
    }

    output->append(buffer, (size_t)written);
    return svc::SERVICE_STATUS_OK;
}

static int append_i64(std::string* output, int64_t value)
{
    char buffer[32];
    int written;

    if (output == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    written = snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        return svc::SERVICE_STATUS_FAILED;
    }

    output->append(buffer, (size_t)written);
    return svc::SERVICE_STATUS_OK;
}

static int append_u32(std::string* output, uint32_t value)
{
    return append_u64(output, value);
}

static int append_text(std::string* output, const char* text)
{
    if (output == NULL || text == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    output->append(text);
    return svc::SERVICE_STATUS_OK;
}

static int append_hex_blob(std::string* output, const std::string& data)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (output == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    if (data.size() > ((size_t)-1 - output->size() - 3u) / 2u) {
        return svc::SERVICE_STATUS_FAILED;
    }

    output->reserve(output->size() + 3u + data.size() * 2u);
    output->append("X'");
    for (i = 0u; i < data.size(); ++i) {
        unsigned char value;

        value = (unsigned char)data[i];
        output->push_back(hex[(value >> 4) & 0x0f]);
        output->push_back(hex[value & 0x0f]);
    }
    output->push_back('\'');
    return svc::SERVICE_STATUS_OK;
}

static int append_quoted(abe_db_t* db, std::string* output, const char* value)
{
    std::string escaped;
    uint64_t value_length;
    uint64_t escaped_length;
    uint64_t escaped_capacity;
    int rc;

    if (db == NULL || output == NULL || value == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    value_length = (uint64_t)strlen(value);
    escaped_capacity = value_length * 2u + 1u;
    escaped.resize((size_t)escaped_capacity);

    escaped_length = 0u;
    rc = abe_db_escape_string(
        db,
        value,
        value_length,
        &escaped[0],
        escaped_capacity,
        &escaped_length);
    if (rc != ABE_DB_OK) {
        return svc::SERVICE_STATUS_FAILED;
    }

    output->push_back('\'');
    output->append(escaped.data(), (size_t)escaped_length);
    output->push_back('\'');
    return svc::SERVICE_STATUS_OK;
}

static int append_nullable_quoted(abe_db_t* db, std::string* output, const char* value)
{
    if (value == NULL || value[0] == '\0') {
        return append_text(output, "NULL");
    }
    return append_quoted(db, output, value);
}

static int append_comma(std::string* output)
{
    return append_text(output, ",");
}

static int execute_sql(abe_db_t* db, const std::string& sql)
{
    int rc;

    rc = abe_db_execute(db, sql.c_str(), NULL);
    return rc == ABE_DB_OK ? svc::SERVICE_STATUS_OK : svc::SERVICE_STATUS_FAILED;
}

static int query_blob(
    abe_db_t* db,
    const std::string& sql,
    std::string* out_blob)
{
    abe_db_result_t* result;
    const void* data;
    uint64_t data_size;
    int has_row;
    int rc;

    if (db == NULL || out_blob == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    result = NULL;
    rc = abe_db_query(db, sql.c_str(), &result);
    if (rc != ABE_DB_OK) {
        return svc::SERVICE_STATUS_FAILED;
    }

    has_row = 0;
    rc = abe_db_result_next(result, &has_row);
    if (rc != ABE_DB_OK) {
        abe_db_result_destroy(result);
        return svc::SERVICE_STATUS_FAILED;
    }
    if (!has_row) {
        abe_db_result_destroy(result);
        return svc::SERVICE_STATUS_FAILED;
    }

    data_size = 0u;
    data = abe_db_result_get_blob(result, 0u, &data_size);
    if (data == NULL && data_size != 0u) {
        abe_db_result_destroy(result);
        return svc::SERVICE_STATUS_FAILED;
    }

    out_blob->assign((const char*)data, (size_t)data_size);
    abe_db_result_destroy(result);
    return svc::SERVICE_STATUS_OK;
}

static int load_message_by_sql(
    abe_db_t* db,
    const std::string& sql,
    google::protobuf::Message* out_data)
{
    std::string blob;
    int rc;

    if (out_data == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = query_blob(db, sql, &blob);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return out_data->ParseFromString(blob) ? svc::SERVICE_STATUS_OK : svc::SERVICE_STATUS_FAILED;
}

static int load_mail_list_by_sql(
    abe_db_t* db,
    const std::string& sql,
    pb::PB_MAIL_LIST* out_data)
{
    abe_db_result_t* result;
    int rc;
    int has_row;

    if (db == NULL || out_data == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    out_data->Clear();
    result = NULL;
    rc = abe_db_query(db, sql.c_str(), &result);
    if (rc != ABE_DB_OK) {
        return svc::SERVICE_STATUS_FAILED;
    }

    for (;;) {
        const void* data;
        uint64_t data_size;

        has_row = 0;
        rc = abe_db_result_next(result, &has_row);
        if (rc != ABE_DB_OK) {
            abe_db_result_destroy(result);
            return svc::SERVICE_STATUS_FAILED;
        }
        if (!has_row) {
            break;
        }

        data_size = 0u;
        data = abe_db_result_get_blob(result, 0u, &data_size);
        if (data == NULL && data_size != 0u) {
            abe_db_result_destroy(result);
            return svc::SERVICE_STATUS_FAILED;
        }
        if (data_size > (uint64_t)INT_MAX) {
            abe_db_result_destroy(result);
            return svc::SERVICE_STATUS_FAILED;
        }
        if (!out_data->add_mail_list()->ParseFromArray(data, (int)data_size)) {
            abe_db_result_destroy(result);
            return svc::SERVICE_STATUS_FAILED;
        }
    }

    abe_db_result_destroy(result);
    return svc::SERVICE_STATUS_OK;
}

static int build_select_by_u64(
    std::string* sql,
    const char* table,
    const char* column,
    uint64_t value)
{
    int rc;

    if (sql == NULL || table == NULL || column == NULL || value == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    sql->clear();
    rc = append_text(sql, "SELECT data_blob FROM ");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_text(sql, table);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_text(sql, " WHERE ");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_text(sql, column);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_text(sql, "=");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, value);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return append_text(sql, " LIMIT 1");
}

static int build_select_by_string(
    abe_db_t* db,
    std::string* sql,
    const char* table,
    const char* column,
    const char* value)
{
    int rc;

    if (db == NULL || sql == NULL || table == NULL || column == NULL ||
        value == NULL || value[0] == '\0') {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    sql->clear();
    rc = append_text(sql, "SELECT data_blob FROM ");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_text(sql, table);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_text(sql, " WHERE ");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_text(sql, column);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_text(sql, "=");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_quoted(db, sql, value);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return append_text(sql, " LIMIT 1");
}

static int build_account_identity_by_u64(
    std::string* sql,
    const char* column,
    uint64_t value)
{
    int rc;

    if (sql == NULL || column == NULL || value == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    sql->clear();
    rc = append_text(sql, "SELECT account_id,uid FROM account_data WHERE ");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_text(sql, column);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_text(sql, "=");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, value);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return append_text(sql, " LIMIT 1");
}

static int build_account_identity_by_string(
    abe_db_t* db,
    std::string* sql,
    const char* column,
    const char* value)
{
    int rc;

    if (db == NULL || sql == NULL || column == NULL || value == NULL || value[0] == '\0') {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    sql->clear();
    rc = append_text(sql, "SELECT account_id,uid FROM account_data WHERE ");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_text(sql, column);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_text(sql, "=");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_quoted(db, sql, value);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return append_text(sql, " LIMIT 1");
}

static int query_account_identity(
    abe_db_t* db,
    const std::string& sql,
    uint64_t* out_account_id,
    uint64_t* out_uid,
    int* out_found)
{
    abe_db_result_t* result;
    int has_row;
    int rc;

    if (db == NULL || out_account_id == NULL || out_uid == NULL || out_found == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    *out_account_id = 0u;
    *out_uid = 0u;
    *out_found = 0;

    result = NULL;
    rc = abe_db_query(db, sql.c_str(), &result);
    if (rc != ABE_DB_OK) {
        return svc::SERVICE_STATUS_FAILED;
    }

    has_row = 0;
    rc = abe_db_result_next(result, &has_row);
    if (rc != ABE_DB_OK) {
        abe_db_result_destroy(result);
        return svc::SERVICE_STATUS_FAILED;
    }
    if (!has_row) {
        abe_db_result_destroy(result);
        return svc::SERVICE_STATUS_OK;
    }

    rc = abe_db_result_get_u64(result, 0u, out_account_id);
    if (rc == ABE_DB_OK) {
        rc = abe_db_result_get_u64(result, 1u, out_uid);
    }
    abe_db_result_destroy(result);
    if (rc != ABE_DB_OK) {
        return svc::SERVICE_STATUS_FAILED;
    }

    *out_found = 1;
    return svc::SERVICE_STATUS_OK;
}

static int check_account_identity(
    abe_db_t* db,
    const pb::PB_ACCOUNT_DATA& data,
    const std::string& sql)
{
    uint64_t account_id;
    uint64_t uid;
    int found;
    int rc;

    rc = query_account_identity(db, sql, &account_id, &uid, &found);
    if (rc != svc::SERVICE_STATUS_OK || !found) {
        return rc;
    }
    if (account_id != data.account_id() || uid != data.uid()) {
        return svc::SERVICE_STATUS_DUPLICATE;
    }
    return svc::SERVICE_STATUS_OK;
}

static int check_account_conflict(abe_db_t* db, const pb::PB_ACCOUNT_DATA& data)
{
    std::string sql;
    int rc;

    if (db == NULL || data.account_id() == 0u || data.uid() == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    rc = build_account_identity_by_u64(&sql, "account_id", data.account_id());
    if (rc == svc::SERVICE_STATUS_OK) {
        rc = check_account_identity(db, data, sql);
    }
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }

    rc = build_account_identity_by_u64(&sql, "uid", data.uid());
    if (rc == svc::SERVICE_STATUS_OK) {
        rc = check_account_identity(db, data, sql);
    }
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }

    if (!data.account_name().empty()) {
        rc = build_account_identity_by_string(
            db,
            &sql,
            "account_name",
            data.account_name().c_str());
        if (rc == svc::SERVICE_STATUS_OK) {
            rc = check_account_identity(db, data, sql);
        }
        if (rc != svc::SERVICE_STATUS_OK) {
            return rc;
        }
    }

    if (!data.open_id().empty()) {
        rc = build_account_identity_by_string(db, &sql, "open_id", data.open_id().c_str());
        if (rc == svc::SERVICE_STATUS_OK) {
            rc = check_account_identity(db, data, sql);
        }
        if (rc != svc::SERVICE_STATUS_OK) {
            return rc;
        }
    }

    return svc::SERVICE_STATUS_OK;
}

static int append_upsert_suffix(std::string* sql, const char* update_expr)
{
    int rc;

    rc = append_text(sql, ") ON DUPLICATE KEY UPDATE ");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return append_text(sql, update_expr);
}

static int account_to_sql(
    abe_db_t* db,
    const pb::PB_ACCOUNT_DATA& data,
    std::string* sql)
{
    std::string blob;
    int rc;

    if (db == NULL || sql == NULL || data.account_id() == 0u || data.uid() == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    if (!data.SerializeToString(&blob)) {
        return svc::SERVICE_STATUS_FAILED;
    }

    sql->clear();
    sql->reserve(512u + blob.size() * 2u);
    rc = append_text(sql,
        "INSERT INTO account_data "
        "(account_id,uid,account_name,open_id,state,data_version,schema_version,"
        "created_at_ms,last_login_time_ms,last_logout_time_ms,updated_at_ms,data_blob) VALUES (");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.account_id());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.uid());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_nullable_quoted(db, sql, data.account_name().c_str());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_nullable_quoted(db, sql, data.open_id().c_str());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, (uint32_t)data.state());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_data_version(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, meta_schema_version(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_created_at_ms(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.last_login_time_ms());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.last_logout_time_ms());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_updated_at_ms(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_hex_blob(sql, blob);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return append_upsert_suffix(sql,
        "uid=VALUES(uid),"
        "account_name=VALUES(account_name),"
        "open_id=VALUES(open_id),"
        "state=VALUES(state),"
        "data_version=VALUES(data_version),"
        "schema_version=VALUES(schema_version),"
        "created_at_ms=VALUES(created_at_ms),"
        "last_login_time_ms=VALUES(last_login_time_ms),"
        "last_logout_time_ms=VALUES(last_logout_time_ms),"
        "updated_at_ms=VALUES(updated_at_ms),"
        "data_blob=VALUES(data_blob)");
}

static int player_to_sql(
    abe_db_t* db,
    const pb::PB_PLAYER_DATA& data,
    std::string* sql)
{
    std::string blob;
    int rc;

    if (db == NULL || sql == NULL || data.uid() == 0u || data.account_id() == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    if (!data.SerializeToString(&blob)) {
        return svc::SERVICE_STATUS_FAILED;
    }

    sql->clear();
    sql->reserve(512u + blob.size() * 2u);
    rc = append_text(sql,
        "INSERT INTO player_data "
        "(uid,account_id,state,nickname,level,gold,diamond,room_id,data_version,"
        "schema_version,created_at_ms,last_login_time_ms,last_logout_time_ms,"
        "updated_at_ms,data_blob) VALUES (");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.uid());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.account_id());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, (uint32_t)data.state());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_quoted(db, sql, data.profile().nickname().c_str());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, data.profile().level());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_i64(sql, data.wallet().gold());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_i64(sql, data.wallet().diamond());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.room_id());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_data_version(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, meta_schema_version(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_created_at_ms(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.last_login_time_ms());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.last_logout_time_ms());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_updated_at_ms(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_hex_blob(sql, blob);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return append_upsert_suffix(sql,
        "account_id=VALUES(account_id),"
        "state=VALUES(state),"
        "nickname=VALUES(nickname),"
        "level=VALUES(level),"
        "gold=VALUES(gold),"
        "diamond=VALUES(diamond),"
        "room_id=VALUES(room_id),"
        "data_version=VALUES(data_version),"
        "schema_version=VALUES(schema_version),"
        "created_at_ms=VALUES(created_at_ms),"
        "last_login_time_ms=VALUES(last_login_time_ms),"
        "last_logout_time_ms=VALUES(last_logout_time_ms),"
        "updated_at_ms=VALUES(updated_at_ms),"
        "data_blob=VALUES(data_blob)");
}

static int bag_to_sql(const pb::PB_BAG_DATA& data, std::string* sql)
{
    std::string blob;
    int rc;

    if (sql == NULL || data.uid() == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    if (!data.SerializeToString(&blob)) {
        return svc::SERVICE_STATUS_FAILED;
    }

    sql->clear();
    sql->reserve(384u + blob.size() * 2u);
    rc = append_text(sql,
        "INSERT INTO bag_data "
        "(uid,item_count,equipment_count,appearance_count,capacity,data_version,"
        "schema_version,created_at_ms,updated_at_ms,data_blob) VALUES (");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.uid());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, (uint32_t)data.item_list_size());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, (uint32_t)data.equipment_list_size());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, (uint32_t)data.appearance_list_size());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, data.capacity());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_data_version(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, meta_schema_version(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_created_at_ms(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_updated_at_ms(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_hex_blob(sql, blob);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return append_upsert_suffix(sql,
        "item_count=VALUES(item_count),"
        "equipment_count=VALUES(equipment_count),"
        "appearance_count=VALUES(appearance_count),"
        "capacity=VALUES(capacity),"
        "data_version=VALUES(data_version),"
        "schema_version=VALUES(schema_version),"
        "created_at_ms=VALUES(created_at_ms),"
        "updated_at_ms=VALUES(updated_at_ms),"
        "data_blob=VALUES(data_blob)");
}

static int count_task_state(const pb::PB_TASK_DATA& data, pb::TaskState state)
{
    int count;
    int i;

    count = 0;
    for (i = 0; i < data.task_list_size(); ++i) {
        if (data.task_list(i).state() == state) {
            ++count;
        }
    }
    return count;
}

static int task_to_sql(const pb::PB_TASK_DATA& data, std::string* sql)
{
    std::string blob;
    int rc;

    if (sql == NULL || data.uid() == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    if (!data.SerializeToString(&blob)) {
        return svc::SERVICE_STATUS_FAILED;
    }

    sql->clear();
    sql->reserve(384u + blob.size() * 2u);
    rc = append_text(sql,
        "INSERT INTO task_data "
        "(uid,active_count,finished_count,claimable_count,data_version,"
        "schema_version,created_at_ms,updated_at_ms,data_blob) VALUES (");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.uid());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, (uint32_t)count_task_state(data, pb::TASK_STATE_ACTIVE));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, (uint32_t)count_task_state(data, pb::TASK_STATE_FINISHED));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, (uint32_t)count_task_state(data, pb::TASK_STATE_CLAIMABLE));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_data_version(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, meta_schema_version(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_created_at_ms(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_updated_at_ms(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_hex_blob(sql, blob);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return append_upsert_suffix(sql,
        "active_count=VALUES(active_count),"
        "finished_count=VALUES(finished_count),"
        "claimable_count=VALUES(claimable_count),"
        "data_version=VALUES(data_version),"
        "schema_version=VALUES(schema_version),"
        "created_at_ms=VALUES(created_at_ms),"
        "updated_at_ms=VALUES(updated_at_ms),"
        "data_blob=VALUES(data_blob)");
}

static int mail_to_sql(
    abe_db_t* db,
    const pb::PB_MAIL_DATA& data,
    std::string* sql)
{
    std::string blob;
    int rc;

    if (db == NULL || sql == NULL || data.mail_id() == 0u || data.uid() == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    if (!data.SerializeToString(&blob)) {
        return svc::SERVICE_STATUS_FAILED;
    }

    sql->clear();
    sql->reserve(512u + blob.size() * 2u);
    rc = append_text(sql,
        "INSERT INTO mail_data "
        "(mail_id,uid,state,title,sender,send_time_ms,expire_time_ms,data_version,"
        "schema_version,created_at_ms,updated_at_ms,data_blob) VALUES (");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.mail_id());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.uid());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, (uint32_t)data.state());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_quoted(db, sql, data.title().c_str());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_quoted(db, sql, data.sender().c_str());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.send_time_ms());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, data.expire_time_ms());
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_data_version(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u32(sql, meta_schema_version(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_created_at_ms(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, meta_updated_at_ms(data.meta()));
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_comma(sql);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_hex_blob(sql, blob);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return append_upsert_suffix(sql,
        "uid=VALUES(uid),"
        "state=VALUES(state),"
        "title=VALUES(title),"
        "sender=VALUES(sender),"
        "send_time_ms=VALUES(send_time_ms),"
        "expire_time_ms=VALUES(expire_time_ms),"
        "data_version=VALUES(data_version),"
        "schema_version=VALUES(schema_version),"
        "created_at_ms=VALUES(created_at_ms),"
        "updated_at_ms=VALUES(updated_at_ms),"
        "data_blob=VALUES(data_blob)");
}

static int build_mail_list_sql(
    std::string* sql,
    uint64_t uid,
    pb::MailState state,
    uint32_t limit,
    int use_state)
{
    int rc;

    if (sql == NULL || uid == 0u || limit == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }

    sql->clear();
    rc = append_text(sql, "SELECT data_blob FROM mail_data WHERE uid=");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = append_u64(sql, uid);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    if (use_state) {
        rc = append_text(sql, " AND state=");
        if (rc != svc::SERVICE_STATUS_OK) {
            return rc;
        }
        rc = append_u32(sql, (uint32_t)state);
        if (rc != svc::SERVICE_STATUS_OK) {
            return rc;
        }
    }
    rc = append_text(sql, " ORDER BY send_time_ms DESC LIMIT ");
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    return append_u32(sql, limit);
}

int MysqlPlayerStore::save_account(const pb::PB_ACCOUNT_DATA& data)
{
    std::string sql;
    int rc;

    if (db_ == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    rc = check_account_conflict(db_, data);
    if (rc != svc::SERVICE_STATUS_OK) {
        return rc;
    }
    rc = account_to_sql(db_, data, &sql);
    return rc == svc::SERVICE_STATUS_OK ? execute_sql(db_, sql) : rc;
}

int MysqlPlayerStore::load_account_by_id(uint64_t account_id, pb::PB_ACCOUNT_DATA* out_data)
{
    std::string sql;
    int rc;

    if (db_ == NULL || account_id == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    rc = build_select_by_u64(&sql, "account_data", "account_id", account_id);
    return rc == svc::SERVICE_STATUS_OK ? load_message_by_sql(db_, sql, out_data) : rc;
}

int MysqlPlayerStore::load_account_by_name(
    const char* account_name,
    pb::PB_ACCOUNT_DATA* out_data)
{
    std::string sql;
    int rc;

    rc = build_select_by_string(
        db_,
        &sql,
        "account_data",
        "account_name",
        account_name);
    return rc == svc::SERVICE_STATUS_OK ? load_message_by_sql(db_, sql, out_data) : rc;
}

int MysqlPlayerStore::load_account_by_open_id(
    const char* open_id,
    pb::PB_ACCOUNT_DATA* out_data)
{
    std::string sql;
    int rc;

    rc = build_select_by_string(db_, &sql, "account_data", "open_id", open_id);
    return rc == svc::SERVICE_STATUS_OK ? load_message_by_sql(db_, sql, out_data) : rc;
}

int MysqlPlayerStore::save_player(const pb::PB_PLAYER_DATA& data)
{
    std::string sql;
    int rc;

    if (db_ == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    rc = player_to_sql(db_, data, &sql);
    return rc == svc::SERVICE_STATUS_OK ? execute_sql(db_, sql) : rc;
}

int MysqlPlayerStore::load_player(uint64_t uid, pb::PB_PLAYER_DATA* out_data)
{
    std::string sql;
    int rc;

    if (db_ == NULL || uid == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    rc = build_select_by_u64(&sql, "player_data", "uid", uid);
    return rc == svc::SERVICE_STATUS_OK ? load_message_by_sql(db_, sql, out_data) : rc;
}

int MysqlPlayerStore::save_bag(const pb::PB_BAG_DATA& data)
{
    std::string sql;
    int rc;

    if (db_ == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    rc = bag_to_sql(data, &sql);
    return rc == svc::SERVICE_STATUS_OK ? execute_sql(db_, sql) : rc;
}

int MysqlPlayerStore::load_bag(uint64_t uid, pb::PB_BAG_DATA* out_data)
{
    std::string sql;
    int rc;

    if (db_ == NULL || uid == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    rc = build_select_by_u64(&sql, "bag_data", "uid", uid);
    return rc == svc::SERVICE_STATUS_OK ? load_message_by_sql(db_, sql, out_data) : rc;
}

int MysqlPlayerStore::save_task(const pb::PB_TASK_DATA& data)
{
    std::string sql;
    int rc;

    if (db_ == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    rc = task_to_sql(data, &sql);
    return rc == svc::SERVICE_STATUS_OK ? execute_sql(db_, sql) : rc;
}

int MysqlPlayerStore::load_task(uint64_t uid, pb::PB_TASK_DATA* out_data)
{
    std::string sql;
    int rc;

    if (db_ == NULL || uid == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    rc = build_select_by_u64(&sql, "task_data", "uid", uid);
    return rc == svc::SERVICE_STATUS_OK ? load_message_by_sql(db_, sql, out_data) : rc;
}

int MysqlPlayerStore::save_mail(const pb::PB_MAIL_DATA& data)
{
    std::string sql;
    int rc;

    if (db_ == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    rc = mail_to_sql(db_, data, &sql);
    return rc == svc::SERVICE_STATUS_OK ? execute_sql(db_, sql) : rc;
}

int MysqlPlayerStore::load_mail(uint64_t mail_id, pb::PB_MAIL_DATA* out_data)
{
    std::string sql;
    int rc;

    if (db_ == NULL || mail_id == 0u) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    rc = build_select_by_u64(&sql, "mail_data", "mail_id", mail_id);
    return rc == svc::SERVICE_STATUS_OK ? load_message_by_sql(db_, sql, out_data) : rc;
}

int MysqlPlayerStore::load_mail_list(
    uint64_t uid,
    uint32_t limit,
    pb::PB_MAIL_LIST* out_data)
{
    std::string sql;
    int rc;

    if (db_ == NULL) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    rc = build_mail_list_sql(&sql, uid, pb::MAIL_STATE_INVALID, limit, 0);
    return rc == svc::SERVICE_STATUS_OK ? load_mail_list_by_sql(db_, sql, out_data) : rc;
}

int MysqlPlayerStore::load_mail_list_by_state(
    uint64_t uid,
    pb::MailState state,
    uint32_t limit,
    pb::PB_MAIL_LIST* out_data)
{
    std::string sql;
    int rc;

    if (db_ == NULL || state == pb::MAIL_STATE_INVALID) {
        return svc::SERVICE_STATUS_INVALID_ARG;
    }
    rc = build_mail_list_sql(&sql, uid, state, limit, 1);
    return rc == svc::SERVICE_STATUS_OK ? load_mail_list_by_sql(db_, sql, out_data) : rc;
}

} /* namespace store */
} /* namespace service */
} /* namespace abe */
