#include "abe_game_server.h"
#include "abe_service_runtime.h"

namespace service_common = abe::service::common;
namespace game = abe::service::game;

int main()
{
    game::GameServer server;

    return service_common::run(server);
}
