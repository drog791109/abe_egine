#include "abe_lobby_server.h"
#include "abe_service_runtime.h"

namespace service_common = abe::service::common;
namespace lobby = abe::service::lobby;

int main()
{
    lobby::LobbyServer server;

    return service_common::run(server);
}
