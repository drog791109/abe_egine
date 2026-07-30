#include "abe_gatehub_server.h"
#include "abe_service_runtime.h"

namespace service_common = abe::service::common;
namespace gatehub = abe::service::gatehub;

int main()
{
    gatehub::GateHubServer server;

    return service_common::run(server);
}
