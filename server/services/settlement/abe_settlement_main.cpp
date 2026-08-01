#include "abe_settlement_server.h"
#include "abe_service_runtime.h"

namespace service_common = abe::service::common;
namespace settlement = abe::service::settlement;

int main()
{
    settlement::SettlementServer server;

    return service_common::run(server);
}
