#ifndef ABE_SERVICE_COMMON_STORE_PLAYER_STORE_H
#define ABE_SERVICE_COMMON_STORE_PLAYER_STORE_H

#include "abe_db.h"
#include "abe_service_runtime.h"
#include "player_store.pb.h"

namespace abe {
namespace service {
namespace store {

class PlayerStore {
public:
    virtual ~PlayerStore();

    virtual int save_account(const abe::proto::store::PB_ACCOUNT_DATA& data) = 0;
    virtual int load_account_by_id(
        uint64_t account_id,
        abe::proto::store::PB_ACCOUNT_DATA* out_data) = 0;
    virtual int load_account_by_name(
        const char* account_name,
        abe::proto::store::PB_ACCOUNT_DATA* out_data) = 0;
    virtual int load_account_by_open_id(
        const char* open_id,
        abe::proto::store::PB_ACCOUNT_DATA* out_data) = 0;

    virtual int save_player(const abe::proto::store::PB_PLAYER_DATA& data) = 0;
    virtual int load_player(uint64_t uid, abe::proto::store::PB_PLAYER_DATA* out_data) = 0;

    virtual int save_bag(const abe::proto::store::PB_BAG_DATA& data) = 0;
    virtual int load_bag(uint64_t uid, abe::proto::store::PB_BAG_DATA* out_data) = 0;

    virtual int save_task(const abe::proto::store::PB_TASK_DATA& data) = 0;
    virtual int load_task(uint64_t uid, abe::proto::store::PB_TASK_DATA* out_data) = 0;

    virtual int save_mail(const abe::proto::store::PB_MAIL_DATA& data) = 0;
    virtual int load_mail(
        uint64_t mail_id,
        abe::proto::store::PB_MAIL_DATA* out_data) = 0;
    virtual int load_mail_list(
        uint64_t uid,
        uint32_t limit,
        abe::proto::store::PB_MAIL_LIST* out_data) = 0;
    virtual int load_mail_list_by_state(
        uint64_t uid,
        abe::proto::store::MailState state,
        uint32_t limit,
        abe::proto::store::PB_MAIL_LIST* out_data) = 0;
};

class MysqlPlayerStore : public PlayerStore {
public:
    MysqlPlayerStore();

    int init(abe_db_t* db);

    virtual int save_account(const abe::proto::store::PB_ACCOUNT_DATA& data);
    virtual int load_account_by_id(
        uint64_t account_id,
        abe::proto::store::PB_ACCOUNT_DATA* out_data);
    virtual int load_account_by_name(
        const char* account_name,
        abe::proto::store::PB_ACCOUNT_DATA* out_data);
    virtual int load_account_by_open_id(
        const char* open_id,
        abe::proto::store::PB_ACCOUNT_DATA* out_data);

    virtual int save_player(const abe::proto::store::PB_PLAYER_DATA& data);
    virtual int load_player(uint64_t uid, abe::proto::store::PB_PLAYER_DATA* out_data);

    virtual int save_bag(const abe::proto::store::PB_BAG_DATA& data);
    virtual int load_bag(uint64_t uid, abe::proto::store::PB_BAG_DATA* out_data);

    virtual int save_task(const abe::proto::store::PB_TASK_DATA& data);
    virtual int load_task(uint64_t uid, abe::proto::store::PB_TASK_DATA* out_data);

    virtual int save_mail(const abe::proto::store::PB_MAIL_DATA& data);
    virtual int load_mail(
        uint64_t mail_id,
        abe::proto::store::PB_MAIL_DATA* out_data);
    virtual int load_mail_list(
        uint64_t uid,
        uint32_t limit,
        abe::proto::store::PB_MAIL_LIST* out_data);
    virtual int load_mail_list_by_state(
        uint64_t uid,
        abe::proto::store::MailState state,
        uint32_t limit,
        abe::proto::store::PB_MAIL_LIST* out_data);

private:
    MysqlPlayerStore(const MysqlPlayerStore&);
    MysqlPlayerStore& operator=(const MysqlPlayerStore&);

    abe_db_t* db_;
};

} /* namespace store */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_COMMON_STORE_PLAYER_STORE_H */
