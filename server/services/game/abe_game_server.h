#ifndef ABE_SERVICE_GAME_SERVER_H
#define ABE_SERVICE_GAME_SERVER_H

#include "abe_game_session.h"
#include "abe_service_runtime.h"
#include "abe_session_manager.h"

#include <stdint.h>

namespace abe {
namespace service {
namespace game {

struct GameServerConfig {
    uint32_t max_rooms;
    uint32_t max_players_per_room;
    uint64_t server_id;
    uint64_t idle_timeout_ms;
};

void set_game_server_defaults(GameServerConfig* config);

class GameServer : public abe::service::common::Service {
public:
    GameServer();

    virtual const char* name() const;
    virtual const char* config_path() const;
    virtual void defaults();
    virtual int load_config(const abe_config_t* config);
    virtual int init(abe::service::common::Context& context);
    virtual int process_message(const abe::service::common::Message& message);
    virtual int update(uint64_t now_ms);
    virtual void close(uint64_t now_ms);

    int handle_disconnect(
        uint64_t gateway_id,
        uint64_t connection_id,
        uint64_t now_ms);

    abe::service::session::SessionManager* session_manager();
    const abe::service::session::SessionManager* session_manager() const;
    int initialized() const;

private:
    GameServer(const GameServer&);
    GameServer& operator=(const GameServer&);

    GameServerConfig config_;
    abe::service::session::SessionManager sessions_;
    int initialized_;
};

} /* namespace game */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_GAME_SERVER_H */
