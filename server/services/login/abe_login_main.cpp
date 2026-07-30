#include "abe_login_server.h"
#include "abe_service_runtime.h"

namespace service_common = abe::service::common;
namespace login = abe::service::login;

int main()
{
    login::LoginServer server;

    return service_common::run(server);
}
