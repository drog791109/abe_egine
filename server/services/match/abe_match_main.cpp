#include "abe_match_server.h"
#include "abe_service_runtime.h"

namespace service_common = abe::service::common;
namespace match = abe::service::match;

int main()
{
    match::MatchServer server;

    return service_common::run(server);
}
