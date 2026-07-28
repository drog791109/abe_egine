#include "abe_rpc.h"

#include "../../abe_test.h"

#include <string.h>
#include <vector>

namespace rpc = abe::logic::rpc;

struct LinkBuffer {
    rpc::RpcEndpoint* peer;
    std::vector<unsigned char> last_packet;
    uint32_t send_count;
};

struct EchoContext {
    const char* reply;
    uint32_t call_count;
};

static int send_to_peer(const void* packet, uint32_t packet_size, void* user_data)
{
    LinkBuffer* link;

    link = (LinkBuffer*)user_data;
    TEST_REQUIRE(link != NULL);
    TEST_REQUIRE(packet != NULL || packet_size == 0u);

    link->last_packet.assign(
        (const unsigned char*)packet,
        (const unsigned char*)packet + packet_size);
    ++link->send_count;

    if (link->peer == NULL) {
        return ABE_OK;
    }
    return link->peer->receive_packet(packet, packet_size);
}

static int echo_handler(
    rpc::RpcEndpoint* endpoint,
    const rpc::RpcRequest& request,
    void* user_data)
{
    EchoContext* context;

    TEST_REQUIRE(endpoint != NULL);
    context = (EchoContext*)user_data;
    TEST_REQUIRE(context != NULL);
    ++context->call_count;
    return endpoint->send_response(
        request,
        context->reply,
        (uint32_t)strlen(context->reply));
}

static int notify_handler(
    rpc::RpcEndpoint* endpoint,
    const rpc::RpcRequest& request,
    void* user_data)
{
    EchoContext* context;

    (void)endpoint;
    (void)request;
    context = (EchoContext*)user_data;
    TEST_REQUIRE(context != NULL);
    ++context->call_count;
    return ABE_OK;
}

static rpc::RpcTask<rpc::RpcResponse> call_echo(
    rpc::RpcEndpoint* endpoint,
    const char* body)
{
    rpc::RpcCallRequest request;

    memset(&request, 0, sizeof(request));
    request.msg_id = 1001u;
    request.target_server = 2u;
    request.trace_id = 88u;
    request.now_ms = 1000u;
    request.timeout_ms = 100u;
    request.body = body;
    request.body_size = (uint32_t)strlen(body);
    co_return co_await endpoint->call(request);
}

static rpc::RpcTask<rpc::RpcResponse> call_missing(
    rpc::RpcEndpoint* endpoint)
{
    rpc::RpcCallRequest request;

    memset(&request, 0, sizeof(request));
    request.msg_id = 404u;
    request.target_server = 2u;
    request.now_ms = 1000u;
    request.timeout_ms = 100u;
    co_return co_await endpoint->call(request);
}

static rpc::RpcTask<rpc::RpcResponse> call_timeout(
    rpc::RpcEndpoint* endpoint)
{
    rpc::RpcCallRequest request;

    memset(&request, 0, sizeof(request));
    request.msg_id = 1001u;
    request.target_server = 2u;
    request.now_ms = 1000u;
    request.timeout_ms = 10u;
    co_return co_await endpoint->call(request);
}

static int init_endpoint(
    rpc::RpcEndpoint* endpoint,
    uint32_t server_id,
    LinkBuffer* link)
{
    rpc::RpcEndpointConfig config;

    memset(&config, 0, sizeof(config));
    config.server_id = server_id;
    config.max_pending = 8u;
    config.max_packet_size = 4096u;
    config.default_timeout_ms = 100u;
    config.sender.send = send_to_peer;
    config.sender.user_data = link;
    return endpoint->init(config);
}

static int test_rpc_call_response(void)
{
    rpc::RpcEndpoint client;
    rpc::RpcEndpoint server;
    LinkBuffer client_link;
    LinkBuffer server_link;
    EchoContext echo;
    rpc::RpcTask<rpc::RpcResponse> task;
    rpc::RpcResponse response;

    memset(&client_link, 0, sizeof(client_link));
    memset(&server_link, 0, sizeof(server_link));
    memset(&echo, 0, sizeof(echo));
    client_link.peer = &server;
    server_link.peer = &client;
    echo.reply = "world";

    TEST_REQUIRE(init_endpoint(&client, 1u, &client_link) == ABE_OK);
    TEST_REQUIRE(init_endpoint(&server, 2u, &server_link) == ABE_OK);
    TEST_REQUIRE(server.set_handler(1001u, echo_handler, &echo) == ABE_OK);

    task = call_echo(&client, "hello");
    TEST_REQUIRE(task.done());
    TEST_REQUIRE(task.status() == ABE_OK);
    response = task.result();
    TEST_REQUIRE(response.status == ABE_OK);
    TEST_REQUIRE(response.header.rpc_id != 0u);
    TEST_REQUIRE(response.header.source_server == 2u);
    TEST_REQUIRE(response.header.target_server == 1u);
    TEST_REQUIRE(response.body == "world");
    TEST_REQUIRE(echo.call_count == 1u);
    TEST_REQUIRE(client.pending_count() == 0u);

    client.close();
    server.close();
    return ABE_TEST_STATUS_OK;
}

static int test_rpc_error_response(void)
{
    rpc::RpcEndpoint client;
    rpc::RpcEndpoint server;
    LinkBuffer client_link;
    LinkBuffer server_link;
    rpc::RpcTask<rpc::RpcResponse> task;
    rpc::RpcResponse response;

    memset(&client_link, 0, sizeof(client_link));
    memset(&server_link, 0, sizeof(server_link));
    client_link.peer = &server;
    server_link.peer = &client;

    TEST_REQUIRE(init_endpoint(&client, 1u, &client_link) == ABE_OK);
    TEST_REQUIRE(init_endpoint(&server, 2u, &server_link) == ABE_OK);

    task = call_missing(&client);
    TEST_REQUIRE(task.done());
    response = task.result();
    TEST_REQUIRE(response.status == ABE_NOT_FOUND);
    TEST_REQUIRE(response.header.flags & rpc::RPC_FLAG_ERROR);
    TEST_REQUIRE(client.pending_count() == 0u);

    client.close();
    server.close();
    return ABE_TEST_STATUS_OK;
}

static int test_rpc_timeout(void)
{
    rpc::RpcEndpoint client;
    LinkBuffer client_link;
    rpc::RpcTask<rpc::RpcResponse> task;
    rpc::RpcResponse response;
    uint32_t timeout_count;

    memset(&client_link, 0, sizeof(client_link));
    client_link.peer = NULL;

    TEST_REQUIRE(init_endpoint(&client, 1u, &client_link) == ABE_OK);
    task = call_timeout(&client);
    TEST_REQUIRE(!task.done());
    TEST_REQUIRE(client.pending_count() == 1u);

    timeout_count = 0u;
    TEST_REQUIRE(client.update(1011u, &timeout_count) == ABE_OK);
    TEST_REQUIRE(timeout_count == 1u);
    TEST_REQUIRE(task.done());
    response = task.result();
    TEST_REQUIRE(response.status == ABE_TIMEOUT);
    TEST_REQUIRE(client.pending_count() == 0u);

    client.close();
    return ABE_TEST_STATUS_OK;
}

static int test_rpc_notify(void)
{
    rpc::RpcEndpoint client;
    rpc::RpcEndpoint server;
    LinkBuffer client_link;
    LinkBuffer server_link;
    EchoContext context;
    rpc::RpcCallRequest request;

    memset(&client_link, 0, sizeof(client_link));
    memset(&server_link, 0, sizeof(server_link));
    memset(&context, 0, sizeof(context));
    client_link.peer = &server;
    server_link.peer = &client;

    TEST_REQUIRE(init_endpoint(&client, 1u, &client_link) == ABE_OK);
    TEST_REQUIRE(init_endpoint(&server, 2u, &server_link) == ABE_OK);
    TEST_REQUIRE(server.set_handler(2001u, notify_handler, &context) == ABE_OK);

    memset(&request, 0, sizeof(request));
    request.msg_id = 2001u;
    request.target_server = 2u;
    request.now_ms = 1000u;
    TEST_REQUIRE(client.notify(request) == ABE_OK);
    TEST_REQUIRE(context.call_count == 1u);
    TEST_REQUIRE(client.pending_count() == 0u);

    client.close();
    server.close();
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_rpc_call_response() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_rpc_error_response() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_rpc_timeout() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_rpc_notify() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
