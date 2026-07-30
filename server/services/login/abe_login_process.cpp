#include "abe_login_process.h"

#include "abe_log.h"
#include "protocol.pb.h"

#include <new>
#include <stddef.h>
#include <string.h>

namespace abe {
namespace service {
namespace login {

namespace proto = ::abe::proto::client;

enum {
    ABE_LOGIN_DEFAULT_MAX_ACCOUNTS = 4096u,
    ABE_LOGIN_ACCOUNT_MIN_SIZE = 4u,
    ABE_LOGIN_NICKNAME_MIN_CODEPOINTS = 2u,
    ABE_LOGIN_NICKNAME_MAX_CODEPOINTS = 16u
};

struct LoginProcess::AccountSlot {
    int used;
    LoginAccountData account_data;
    LoginPlayerData player_data;
};

static unsigned char ascii_lower(unsigned char value)
{
    if (value >= 'A' && value <= 'Z') {
        return (unsigned char)(value + ('a' - 'A'));
    }
    return value;
}

static bool ascii_is_alpha(unsigned char value)
{
    value = ascii_lower(value);
    return value >= 'a' && value <= 'z';
}

static bool ascii_is_digit(unsigned char value)
{
    return value >= '0' && value <= '9';
}

static bool ascii_is_word(unsigned char value)
{
    return ascii_is_alpha(value) || ascii_is_digit(value) || value == '_';
}

static bool text_equals_ascii_word(
    const char* text,
    size_t text_size,
    const char* word)
{
    size_t word_size;
    size_t index;

    if (text == NULL || word == NULL) {
        return false;
    }

    word_size = strlen(word);
    if (text_size != word_size) {
        return false;
    }

    index = 0u;
    while (index < text_size) {
        if (ascii_lower((unsigned char)text[index]) !=
            ascii_lower((unsigned char)word[index])) {
            return false;
        }
        ++index;
    }
    return true;
}

static bool text_contains_ascii_case_insensitive(
    const char* text,
    const char* value,
    size_t value_size)
{
    size_t text_size;
    size_t offset;

    if (text == NULL || value == NULL || value_size == 0u) {
        return false;
    }

    text_size = strlen(text);
    if (value_size > text_size) {
        return false;
    }

    offset = 0u;
    while (offset + value_size <= text_size) {
        size_t index;
        bool equal;

        equal = true;
        index = 0u;
        while (index < value_size) {
            if (ascii_lower((unsigned char)text[offset + index]) !=
                ascii_lower((unsigned char)value[index])) {
                equal = false;
                break;
            }
            ++index;
        }
        if (equal) {
            return true;
        }
        ++offset;
    }
    return false;
}

static bool contains_dirty_word(
    const char* text,
    const char* dirty_words)
{
    const char* cursor;

    if (text == NULL || dirty_words == NULL || dirty_words[0] == '\0') {
        return false;
    }

    cursor = dirty_words;
    while (*cursor != '\0') {
        const char* begin;
        const char* end;

        while (*cursor == ',' ||
               *cursor == '|' ||
               *cursor == ';' ||
               *cursor == '\n' ||
               *cursor == '\r' ||
               *cursor == ' ' ||
               *cursor == '\t') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }

        begin = cursor;
        while (*cursor != '\0' &&
               *cursor != ',' &&
               *cursor != '|' &&
               *cursor != ';' &&
               *cursor != '\n' &&
               *cursor != '\r') {
            ++cursor;
        }
        end = cursor;
        while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) {
            --end;
        }

        if (end > begin &&
            text_contains_ascii_case_insensitive(
                text,
                begin,
                (size_t)(end - begin))) {
            return true;
        }
    }
    return false;
}

static bool contains_sql_pattern(const char* text)
{
    static const char* words[] = {
        "select",
        "insert",
        "update",
        "delete",
        "drop",
        "alter",
        "truncate",
        "union",
        "where",
        "from",
        "exec",
        "execute",
        "sleep",
        "benchmark",
        "or",
        "and"
    };
    const char* cursor;

    if (text == NULL) {
        return false;
    }

    cursor = text;
    while (*cursor != '\0') {
        if (*cursor == '\'' ||
            *cursor == '"' ||
            *cursor == '`' ||
            *cursor == ';' ||
            *cursor == '#' ||
            *cursor == '\\') {
            return true;
        }
        if ((cursor[0] == '-' && cursor[1] == '-') ||
            (cursor[0] == '/' && cursor[1] == '*') ||
            (cursor[0] == '*' && cursor[1] == '/')) {
            return true;
        }

        if (ascii_is_word((unsigned char)*cursor)) {
            const char* begin;
            size_t size;
            uint32_t index;

            begin = cursor;
            while (ascii_is_word((unsigned char)*cursor)) {
                ++cursor;
            }
            size = (size_t)(cursor - begin);
            index = 0u;
            while (index < sizeof(words) / sizeof(words[0])) {
                if (text_equals_ascii_word(begin, size, words[index])) {
                    return true;
                }
                ++index;
            }
            continue;
        }
        ++cursor;
    }
    return false;
}

static bool decode_utf8(
    const unsigned char* text,
    size_t remaining,
    uint32_t* out_codepoint,
    size_t* out_size)
{
    unsigned char first;
    uint32_t codepoint;

    if (text == NULL || remaining == 0u || out_codepoint == NULL || out_size == NULL) {
        return false;
    }

    first = text[0];
    if (first < 0x80u) {
        *out_codepoint = first;
        *out_size = 1u;
        return true;
    }
    if (first >= 0xc2u && first <= 0xdfu) {
        if (remaining < 2u || (text[1] & 0xc0u) != 0x80u) {
            return false;
        }
        codepoint = ((uint32_t)(first & 0x1fu) << 6u) |
            (uint32_t)(text[1] & 0x3fu);
        *out_codepoint = codepoint;
        *out_size = 2u;
        return true;
    }
    if (first >= 0xe0u && first <= 0xefu) {
        if (remaining < 3u ||
            (text[1] & 0xc0u) != 0x80u ||
            (text[2] & 0xc0u) != 0x80u) {
            return false;
        }
        if ((first == 0xe0u && text[1] < 0xa0u) ||
            (first == 0xedu && text[1] >= 0xa0u)) {
            return false;
        }
        codepoint = ((uint32_t)(first & 0x0fu) << 12u) |
            ((uint32_t)(text[1] & 0x3fu) << 6u) |
            (uint32_t)(text[2] & 0x3fu);
        *out_codepoint = codepoint;
        *out_size = 3u;
        return true;
    }
    if (first >= 0xf0u && first <= 0xf4u) {
        if (remaining < 4u ||
            (text[1] & 0xc0u) != 0x80u ||
            (text[2] & 0xc0u) != 0x80u ||
            (text[3] & 0xc0u) != 0x80u) {
            return false;
        }
        if ((first == 0xf0u && text[1] < 0x90u) ||
            (first == 0xf4u && text[1] >= 0x90u)) {
            return false;
        }
        codepoint = ((uint32_t)(first & 0x07u) << 18u) |
            ((uint32_t)(text[1] & 0x3fu) << 12u) |
            ((uint32_t)(text[2] & 0x3fu) << 6u) |
            (uint32_t)(text[3] & 0x3fu);
        *out_codepoint = codepoint;
        *out_size = 4u;
        return true;
    }
    return false;
}

static bool validate_utf8_nickname(const char* nickname)
{
    const unsigned char* text;
    size_t size;
    size_t offset;
    uint32_t count;

    if (nickname == NULL) {
        return false;
    }

    size = strlen(nickname);
    if (size == 0u || size >= ABE_LOGIN_NICKNAME_CAPACITY) {
        return false;
    }
    if (nickname[0] == ' ' || nickname[size - 1u] == ' ') {
        return false;
    }

    text = (const unsigned char*)nickname;
    offset = 0u;
    count = 0u;
    while (offset < size) {
        uint32_t codepoint;
        size_t codepoint_size;

        if (!decode_utf8(
                text + offset,
                size - offset,
                &codepoint,
                &codepoint_size)) {
            return false;
        }
        if (codepoint < 0x20u ||
            (codepoint >= 0x7fu && codepoint <= 0x9fu)) {
            return false;
        }
        offset += codepoint_size;
        ++count;
    }
    return count >= ABE_LOGIN_NICKNAME_MIN_CODEPOINTS &&
        count <= ABE_LOGIN_NICKNAME_MAX_CODEPOINTS;
}

static int validate_optional_text(
    const char* text,
    uint32_t capacity)
{
    size_t size;
    size_t index;

    if (text == NULL || text[0] == '\0') {
        return proto::ERROR_CODE_OK;
    }

    size = strlen(text);
    if (size >= capacity) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }
    if (contains_sql_pattern(text)) {
        return proto::ERROR_CODE_AUTH_SQL_PATTERN;
    }

    index = 0u;
    while (index < size) {
        unsigned char value;

        value = (unsigned char)text[index];
        if (value < 0x20u || value == 0x7fu) {
            return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
        }
        ++index;
    }
    return proto::ERROR_CODE_OK;
}

static int validate_auth_token(const LoginAuthRequest& request, int required)
{
    size_t size;
    size_t index;

    if (request.auth_token == NULL || request.auth_token[0] == '\0') {
        return required ? proto::ERROR_CODE_AUTH_FAILED : proto::ERROR_CODE_OK;
    }

    size = strlen(request.auth_token);
    if (size > ABE_LOGIN_AUTH_TOKEN_MAX_SIZE) {
        return proto::ERROR_CODE_AUTH_FAILED;
    }

    index = 0u;
    while (index < size) {
        unsigned char value;

        value = (unsigned char)request.auth_token[index];
        if (value < 0x20u || value == 0x7fu) {
            return proto::ERROR_CODE_AUTH_FAILED;
        }
        ++index;
    }
    return proto::ERROR_CODE_OK;
}

static int copy_text(char* target, uint32_t capacity, const char* source)
{
    size_t size;

    if (target == NULL || capacity == 0u) {
        return ABE_INVALID_ARG;
    }
    if (source == NULL) {
        target[0] = '\0';
        return ABE_OK;
    }

    size = strlen(source);
    if (size >= capacity) {
        return ABE_BUFFER_TOO_SMALL;
    }
    memcpy(target, source, size + 1u);
    return ABE_OK;
}

void set_login_process_defaults(LoginProcessConfig* config)
{
    if (config == NULL) {
        return;
    }

    config->max_accounts = ABE_LOGIN_DEFAULT_MAX_ACCOUNTS;
    config->allow_register = 1u;
    config->unique_nickname = 1u;
    config->require_auth_token = 1u;
    config->dirty_words = "admin,administrator,system,moderator,gm,root";
}

int login_validate_account_name(
    const char* account,
    const char* dirty_words)
{
    size_t size;
    size_t index;

    if (account == NULL) {
        return proto::ERROR_CODE_AUTH_INVALID_ACCOUNT;
    }

    size = strlen(account);
    if (size < ABE_LOGIN_ACCOUNT_MIN_SIZE ||
        size >= ABE_LOGIN_ACCOUNT_CAPACITY) {
        return proto::ERROR_CODE_AUTH_INVALID_ACCOUNT;
    }
    if (contains_sql_pattern(account)) {
        return proto::ERROR_CODE_AUTH_SQL_PATTERN;
    }
    if (contains_dirty_word(account, dirty_words)) {
        return proto::ERROR_CODE_AUTH_DIRTY_WORD;
    }
    if (!ascii_is_alpha((unsigned char)account[0]) &&
        !ascii_is_digit((unsigned char)account[0])) {
        return proto::ERROR_CODE_AUTH_INVALID_ACCOUNT;
    }
    if (!ascii_is_alpha((unsigned char)account[size - 1u]) &&
        !ascii_is_digit((unsigned char)account[size - 1u])) {
        return proto::ERROR_CODE_AUTH_INVALID_ACCOUNT;
    }

    index = 0u;
    while (index < size) {
        unsigned char value;

        value = (unsigned char)account[index];
        if (!ascii_is_alpha(value) &&
            !ascii_is_digit(value) &&
            value != '_' &&
            value != '-' &&
            value != '.') {
            return proto::ERROR_CODE_AUTH_INVALID_ACCOUNT;
        }
        ++index;
    }
    return proto::ERROR_CODE_OK;
}

int login_validate_nickname(
    const char* nickname,
    const char* dirty_words)
{
    if (nickname == NULL || nickname[0] == '\0') {
        return proto::ERROR_CODE_AUTH_INVALID_NICKNAME;
    }
    if (contains_sql_pattern(nickname)) {
        return proto::ERROR_CODE_AUTH_SQL_PATTERN;
    }
    if (contains_dirty_word(nickname, dirty_words)) {
        return proto::ERROR_CODE_AUTH_DIRTY_WORD;
    }
    if (!validate_utf8_nickname(nickname)) {
        return proto::ERROR_CODE_AUTH_INVALID_NICKNAME;
    }
    return proto::ERROR_CODE_OK;
}

LoginProcess::LoginProcess()
    : id_generator_(NULL),
      accounts_(NULL),
      account_count_(0u),
      initialized_(0)
{
    set_login_process_defaults(&config_);
}

LoginProcess::~LoginProcess()
{
    close();
}

int LoginProcess::init(
    const LoginProcessConfig& config,
    abe_snowflake_t* id_generator)
{
    if (config.max_accounts == 0u ||
        config.allow_register > 1u ||
        config.unique_nickname > 1u ||
        config.require_auth_token > 1u ||
        id_generator == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    close();
    accounts_ = new (std::nothrow) AccountSlot[config.max_accounts];
    if (accounts_ == NULL) {
        return proto::ERROR_CODE_COMMON_SERVER_BUSY;
    }

    memset(accounts_, 0, sizeof(AccountSlot) * config.max_accounts);
    config_ = config;
    id_generator_ = id_generator;
    account_count_ = 0u;
    initialized_ = 1;
    return proto::ERROR_CODE_OK;
}

void LoginProcess::close()
{
    delete[] accounts_;
    accounts_ = NULL;
    id_generator_ = NULL;
    account_count_ = 0u;
    initialized_ = 0;
}

int LoginProcess::authenticate(
    const LoginAuthRequest& request,
    LoginAuthResult* out_result)
{
    AccountSlot* slot;
    int rc;

    if (out_result != NULL) {
        memset(out_result, 0, sizeof(*out_result));
    }
    if (!initialized_) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    if (out_result == NULL || request.now_ms == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    rc = login_validate_account_name(request.account, config_.dirty_words);
    if (rc != proto::ERROR_CODE_OK) {
        return rc;
    }
    rc = validate_auth_token(
        request,
        config_.require_auth_token && !request.reconnect);
    if (rc != proto::ERROR_CODE_OK) {
        return rc;
    }
    rc = validate_optional_text(request.device_id, ABE_LOGIN_DEVICE_ID_CAPACITY);
    if (rc != proto::ERROR_CODE_OK) {
        return rc;
    }
    rc = validate_optional_text(
        request.client_version,
        ABE_LOGIN_CLIENT_VERSION_CAPACITY);
    if (rc != proto::ERROR_CODE_OK) {
        return rc;
    }
    rc = validate_optional_text(request.region, ABE_LOGIN_REGION_CAPACITY);
    if (rc != proto::ERROR_CODE_OK) {
        return rc;
    }
    if (request.nickname != NULL && request.nickname[0] != '\0') {
        rc = login_validate_nickname(request.nickname, config_.dirty_words);
        if (rc != proto::ERROR_CODE_OK) {
            return rc;
        }
    }

    slot = find_slot_by_name(request.account);
    if (slot == NULL) {
        if (request.reconnect) {
            return proto::ERROR_CODE_AUTH_RECONNECT_FAILED;
        }
        if (!config_.allow_register) {
            return proto::ERROR_CODE_AUTH_FAILED;
        }
        rc = create_account(
            request,
            &out_result->account_data,
            &out_result->player_data);
        if (rc == proto::ERROR_CODE_OK) {
            out_result->created = 1u;
        }
        return rc;
    }

    if (request.nickname != NULL &&
        request.nickname[0] != '\0' &&
        strcmp(request.nickname, slot->player_data.nickname) != 0) {
        return proto::ERROR_CODE_AUTH_INVALID_NICKNAME;
    }

    out_result->account_data = slot->account_data;
    out_result->player_data = slot->player_data;
    return proto::ERROR_CODE_OK;
}

int LoginProcess::create_account(
    const LoginAuthRequest& request,
    LoginAccountData* out_account_data,
    LoginPlayerData* out_player_data)
{
    AccountSlot* slot;
    uint64_t account_id;
    uint64_t uid;
    int rc;

    if (!initialized_) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    if (out_account_data == NULL || out_player_data == NULL || request.now_ms == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    rc = login_validate_account_name(request.account, config_.dirty_words);
    if (rc != proto::ERROR_CODE_OK) {
        return rc;
    }
    rc = login_validate_nickname(request.nickname, config_.dirty_words);
    if (rc != proto::ERROR_CODE_OK) {
        return rc;
    }
    if (find_slot_by_name(request.account) != NULL) {
        return proto::ERROR_CODE_AUTH_ACCOUNT_EXISTS;
    }
    if (config_.unique_nickname && find_slot_by_nickname(request.nickname) != NULL) {
        return proto::ERROR_CODE_AUTH_NICKNAME_EXISTS;
    }

    slot = find_free_slot();
    if (slot == NULL) {
        return proto::ERROR_CODE_COMMON_SERVER_BUSY;
    }

    rc = abe_snowflake_next(id_generator_, &account_id);
    if (rc != ABE_OK) {
        ABE_LOG_ERROR(
            "login account id generation failed status=%s",
            abe_status_name(rc));
        return rc == ABE_LIMIT
            ? proto::ERROR_CODE_COMMON_SERVER_BUSY
            : proto::ERROR_CODE_SYSTEM_INTERNAL;
    }
    rc = abe_snowflake_next(id_generator_, &uid);
    if (rc != ABE_OK) {
        ABE_LOG_ERROR(
            "login uid generation failed status=%s",
            abe_status_name(rc));
        return rc == ABE_LIMIT
            ? proto::ERROR_CODE_COMMON_SERVER_BUSY
            : proto::ERROR_CODE_SYSTEM_INTERNAL;
    }

    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->account_data.account_id = account_id;
    slot->account_data.uid = uid;
    slot->account_data.created_at_ms = request.now_ms;
    slot->account_data.last_login_time_ms = 0u;
    slot->player_data.uid = uid;
    slot->player_data.account_id = account_id;
    slot->player_data.created_at_ms = request.now_ms;
    slot->player_data.last_login_time_ms = 0u;
    slot->player_data.sex = 0u;
    slot->player_data.avatar_id = 0u;
    slot->player_data.level = 1u;

    rc = copy_text(
        slot->account_data.account_name,
        ABE_LOGIN_ACCOUNT_CAPACITY,
        request.account);
    if (rc == ABE_OK) {
        rc = copy_text(
            slot->player_data.nickname,
            ABE_LOGIN_NICKNAME_CAPACITY,
            request.nickname);
    }
    if (rc == ABE_OK) {
        rc = copy_text(
            slot->account_data.device_id,
            ABE_LOGIN_DEVICE_ID_CAPACITY,
            request.device_id);
    }
    if (rc == ABE_OK) {
        rc = copy_text(
            slot->account_data.client_version,
            ABE_LOGIN_CLIENT_VERSION_CAPACITY,
            request.client_version);
    }
    if (rc == ABE_OK) {
        rc = copy_text(
            slot->player_data.region,
            ABE_LOGIN_REGION_CAPACITY,
            request.region);
    }
    if (rc != ABE_OK) {
        memset(slot, 0, sizeof(*slot));
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    ++account_count_;
    *out_account_data = slot->account_data;
    *out_player_data = slot->player_data;
    return proto::ERROR_CODE_OK;
}

int LoginProcess::mark_login_success(
    uint64_t uid,
    const LoginAuthRequest& request,
    LoginAccountData* out_account_data,
    LoginPlayerData* out_player_data)
{
    AccountSlot* slot;
    int rc;

    if (!initialized_) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    if (uid == 0u || request.now_ms == 0u) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    slot = find_slot_by_uid(uid);
    if (slot == NULL) {
        return proto::ERROR_CODE_AUTH_FAILED;
    }

    rc = copy_text(
        slot->account_data.device_id,
        ABE_LOGIN_DEVICE_ID_CAPACITY,
        request.device_id);
    if (rc == ABE_OK) {
        rc = copy_text(
            slot->account_data.client_version,
            ABE_LOGIN_CLIENT_VERSION_CAPACITY,
            request.client_version);
    }
    if (rc == ABE_OK && request.region != NULL && request.region[0] != '\0') {
        rc = copy_text(
            slot->player_data.region,
            ABE_LOGIN_REGION_CAPACITY,
            request.region);
    }
    if (rc != ABE_OK) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    slot->account_data.last_login_time_ms = request.now_ms;
    slot->player_data.last_login_time_ms = request.now_ms;
    if (out_account_data != NULL) {
        *out_account_data = slot->account_data;
    }
    if (out_player_data != NULL) {
        *out_player_data = slot->player_data;
    }
    return proto::ERROR_CODE_OK;
}

int LoginProcess::find_account_by_name(
    const char* account_name,
    LoginAccountData* out_account_data) const
{
    const AccountSlot* slot;

    if (!initialized_) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    if (account_name == NULL || out_account_data == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    slot = find_slot_by_name(account_name);
    if (slot == NULL) {
        return proto::ERROR_CODE_AUTH_FAILED;
    }
    *out_account_data = slot->account_data;
    return proto::ERROR_CODE_OK;
}

int LoginProcess::find_account_by_uid(
    uint64_t uid,
    LoginAccountData* out_account_data) const
{
    const AccountSlot* slot;

    if (!initialized_) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    if (uid == 0u || out_account_data == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    slot = find_slot_by_uid(uid);
    if (slot == NULL) {
        return proto::ERROR_CODE_AUTH_FAILED;
    }
    *out_account_data = slot->account_data;
    return proto::ERROR_CODE_OK;
}

int LoginProcess::find_player_by_uid(
    uint64_t uid,
    LoginPlayerData* out_player_data) const
{
    const AccountSlot* slot;

    if (!initialized_) {
        return proto::ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE;
    }
    if (uid == 0u || out_player_data == NULL) {
        return proto::ERROR_CODE_COMMON_INVALID_ARGUMENT;
    }

    slot = find_slot_by_uid(uid);
    if (slot == NULL) {
        return proto::ERROR_CODE_AUTH_FAILED;
    }
    *out_player_data = slot->player_data;
    return proto::ERROR_CODE_OK;
}

uint32_t LoginProcess::account_count() const
{
    return account_count_;
}

int LoginProcess::initialized() const
{
    return initialized_;
}

LoginProcess::AccountSlot* LoginProcess::find_slot_by_name(
    const char* account_name)
{
    uint32_t index;

    if (accounts_ == NULL || account_name == NULL) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_accounts) {
        if (accounts_[index].used &&
            strcmp(accounts_[index].account_data.account_name, account_name) == 0) {
            return &accounts_[index];
        }
        ++index;
    }
    return NULL;
}

const LoginProcess::AccountSlot* LoginProcess::find_slot_by_name(
    const char* account_name) const
{
    uint32_t index;

    if (accounts_ == NULL || account_name == NULL) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_accounts) {
        if (accounts_[index].used &&
            strcmp(accounts_[index].account_data.account_name, account_name) == 0) {
            return &accounts_[index];
        }
        ++index;
    }
    return NULL;
}

LoginProcess::AccountSlot* LoginProcess::find_slot_by_uid(uint64_t uid)
{
    uint32_t index;

    if (accounts_ == NULL || uid == 0u) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_accounts) {
        if (accounts_[index].used && accounts_[index].player_data.uid == uid) {
            return &accounts_[index];
        }
        ++index;
    }
    return NULL;
}

const LoginProcess::AccountSlot* LoginProcess::find_slot_by_uid(
    uint64_t uid) const
{
    uint32_t index;

    if (accounts_ == NULL || uid == 0u) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_accounts) {
        if (accounts_[index].used && accounts_[index].player_data.uid == uid) {
            return &accounts_[index];
        }
        ++index;
    }
    return NULL;
}

LoginProcess::AccountSlot* LoginProcess::find_slot_by_nickname(
    const char* nickname)
{
    uint32_t index;

    if (accounts_ == NULL || nickname == NULL) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_accounts) {
        if (accounts_[index].used &&
            strcmp(accounts_[index].player_data.nickname, nickname) == 0) {
            return &accounts_[index];
        }
        ++index;
    }
    return NULL;
}

LoginProcess::AccountSlot* LoginProcess::find_free_slot()
{
    uint32_t index;

    if (accounts_ == NULL) {
        return NULL;
    }

    index = 0u;
    while (index < config_.max_accounts) {
        if (!accounts_[index].used) {
            return &accounts_[index];
        }
        ++index;
    }
    return NULL;
}

} /* namespace login */
} /* namespace service */
} /* namespace abe */
