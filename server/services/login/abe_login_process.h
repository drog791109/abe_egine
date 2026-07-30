#ifndef ABE_SERVICE_LOGIN_PROCESS_H
#define ABE_SERVICE_LOGIN_PROCESS_H

#include "abe_snowflake.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace login {

enum {
    ABE_LOGIN_ACCOUNT_CAPACITY = 33u,
    ABE_LOGIN_NICKNAME_CAPACITY = 65u,
    ABE_LOGIN_DEVICE_ID_CAPACITY = 65u,
    ABE_LOGIN_CLIENT_VERSION_CAPACITY = 33u,
    ABE_LOGIN_REGION_CAPACITY = 33u,
    ABE_LOGIN_AUTH_TOKEN_MAX_SIZE = 256u
};

struct LoginProcessConfig {
    uint32_t max_accounts;
    uint32_t allow_register;
    uint32_t unique_nickname;
    uint32_t require_auth_token;
    const char* dirty_words;
};

struct LoginAuthRequest {
    const char* account;
    const char* nickname;
    const char* auth_token;
    const char* device_id;
    const char* client_version;
    const char* region;
    uint32_t reconnect;
    uint64_t now_ms;
};

struct LoginAccountData {
    uint64_t account_id;
    uint64_t uid;
    uint64_t created_at_ms;
    uint64_t last_login_time_ms;
    char account_name[ABE_LOGIN_ACCOUNT_CAPACITY];
    char device_id[ABE_LOGIN_DEVICE_ID_CAPACITY];
    char client_version[ABE_LOGIN_CLIENT_VERSION_CAPACITY];
};

struct LoginPlayerData {
    uint64_t uid;
    uint64_t account_id;
    uint64_t created_at_ms;
    uint64_t last_login_time_ms;
    uint32_t sex;
    uint32_t avatar_id;
    uint32_t level;
    char nickname[ABE_LOGIN_NICKNAME_CAPACITY];
    char region[ABE_LOGIN_REGION_CAPACITY];
};

struct LoginAuthResult {
    LoginAccountData account_data;
    LoginPlayerData player_data;
    uint32_t created;
};

void set_login_process_defaults(LoginProcessConfig* config);

int login_validate_account_name(
    const char* account,
    const char* dirty_words);
int login_validate_nickname(
    const char* nickname,
    const char* dirty_words);

class LoginProcess {
public:
    LoginProcess();
    ~LoginProcess();

    int init(
        const LoginProcessConfig& config,
        abe_snowflake_t* id_generator);
    void close();

    int authenticate(
        const LoginAuthRequest& request,
        LoginAuthResult* out_result);
    int create_account(
        const LoginAuthRequest& request,
        LoginAccountData* out_account_data,
        LoginPlayerData* out_player_data);
    int mark_login_success(
        uint64_t uid,
        const LoginAuthRequest& request,
        LoginAccountData* out_account_data,
        LoginPlayerData* out_player_data);
    int find_account_by_name(
        const char* account_name,
        LoginAccountData* out_account_data) const;
    int find_account_by_uid(
        uint64_t uid,
        LoginAccountData* out_account_data) const;
    int find_player_by_uid(
        uint64_t uid,
        LoginPlayerData* out_player_data) const;

    uint32_t account_count() const;
    int initialized() const;

private:
    LoginProcess(const LoginProcess&);
    LoginProcess& operator=(const LoginProcess&);

    struct AccountSlot;

    AccountSlot* find_slot_by_name(const char* account_name);
    const AccountSlot* find_slot_by_name(const char* account_name) const;
    AccountSlot* find_slot_by_uid(uint64_t uid);
    const AccountSlot* find_slot_by_uid(uint64_t uid) const;
    AccountSlot* find_slot_by_nickname(const char* nickname);
    AccountSlot* find_free_slot();

    LoginProcessConfig config_;
    abe_snowflake_t* id_generator_;
    AccountSlot* accounts_;
    uint32_t account_count_;
    int initialized_;
};

} /* namespace login */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_LOGIN_PROCESS_H */
