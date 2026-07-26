#include "abe_gateway_server.h"
#include "abe_service_runtime.h"

namespace service_common = abe::service::common;
namespace gateway = abe::service::gateway;

int main(int argc, char** argv)
{
    gateway::GatewayServer server;

    return service_common::run(argc, argv, server);
}
