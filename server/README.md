# Server 目录结构

`server/` 是服务端源码根目录，包含基础设施和服务进程。客户端与服务端共享的协议定义位于仓库根级 `share/`。下面的箭头表示“右侧可以依赖左侧”：

```text
engine/base -> engine/common -> engine/adapters -> services
                         \----> engine/backends --------> services
engine/base -> engine/log ----------------------> services
proto definitions ------------------------------> common/services
```

## 目录职责

```text
abe_engine/
  server/
    engine/
      src/
        base/       基础设施，C API 优先，公开头保持 C ABI 和 C++03 兼容
        common/     稳定的共享 C 接口与跨服务基础能力，例如 ID、RPC、服务发现、DB 抽象
        log/        原生 C++ 日志封装，提供简洁的 C++11 接口和日志宏
        backends/   common/base 接口的具体实现，隔离 MySQL C API 等原始第三方接口
        adapters/   将 base/common 的 C 接口适配为不高于 C++11 的简单 RAII 和类接口
    services/       可独立启动的服务进程入口和组装层
  share/proto/      客户端与服务端共享的协议定义源文件
```

## Engine 接口硬约束

以下规则是项目常驻背景，适用于以后每一次代码生成、修改和评审：

- `server/engine` 下所有由项目维护的公开接口必须能够使用 C 编译器或 `-std=c++11` 编译，严禁要求 C++14 或更高标准。
- C++11 是最高兼容标准，不代表鼓励使用全部 C++11 特性；能用普通 C 或简单 C++03 风格表达时，优先采用更直接的写法。
- C 接口优先使用普通函数、plain struct、opaque handle 和显式错误码；需要 C++ 接口时，只使用简单类和直接的 RAII。
- 原生 C++ 日志库直接封装在 `engine/src/log`，对外提供简单的 C++11 接口和宏；公共头不暴露 spdlog 或 STL。
- `engine/src/adapters` 产生的 C++ 接口不得要求高于 C++11 的编译标准。
- engine 公共接口原则上不得暴露 STL 类型，包括 `std::string`、标准容器、`std::function` 和智能指针；字符串、数组和缓冲区优先使用 `const char*`、指针加长度、普通结构体和显式回调。
- engine 项目自维护的实现代码也应尽量少用或不用 STL；只有局部使用明显比 C 写法更简单，且不会泄漏到公共接口时才允许使用。
- 严禁为了炫技或减少少量代码使用模板元编程、可变参数模板、完美转发、复杂 SFINAE、表达式模板、复杂 lambda、复杂继承、重度运算符重载、异常驱动流程、依赖 RTTI 的设计，以及隐藏控制流的复杂宏。
- engine 中由项目维护的实现代码应优先使用 C 或直接、易懂的 C++，接口和实现都以人工阅读、调试和长期维护为第一目标。
- 第三方 C++ 库若强制要求高于 C++11 的标准，只能隔离在独立 `backends` target 的私有实现文件中；不得把第三方类型或更高编译标准传递到项目公共接口。
- 新增或修改 engine 接口时，必须按接口语言检查 C 编译兼容性或 C++11 编译兼容性。

## 分层规则

- `engine/src/base` 不依赖 `common` 或 `services`。
- `engine/src/common` 可以依赖 `base`，负责定义稳定的共享 C 接口，不依赖具体后端、C++ adapter 或 `services`。
- `engine/src/backends` 可以依赖 `base`、`common` 和原始第三方库，负责实现 C 接口或提供最小 C ABI 桥接；第三方类型不得进入 `base/common` 公共头。
- `engine/src/log` 直接封装原生 C++ 日志库，可依赖 `base/time` 获取时间，供 C++ 的 `services` 使用；C 模块确实需要日志时，再单独设计最小 C 接口。
- `engine/src/adapters` 只依赖 `base/common` 暴露的 C 接口，将其适配为不高于 C++11 的简单 RAII 和类接口；不得承载 MySQL、spdlog 等具体后端实现。
- 原生 C++ 库如果只供 C++ 模块使用，直接在对应 C++ 模块中使用，不为统一形式机械地再封装一层 C 接口。
- 只有当 `base/common` 的 C 接口确实需要原生 C++ 实现时，才允许在 `backends` 中增加范围最小的 C ABI 桥接。
- `services` 负责进程入口、配置加载、后端选择、依赖装配和服务生命周期，不承载复杂玩法逻辑。
- `services/common` 放各服务进程都会复用的启动组件和服务侧公共组件，例如 `ServiceRuntime`、Session 生命周期、配置/日志/DB 初始化和停止信号处理；不放具体玩法规则。
- `services/gateway` 负责把 `engine/adapters/net`、`services/common/session` 和协议解码串起来；协议号和消息定义仍然以 `share/proto/client/protocol.proto` 为准。
- `services/login` 负责登录请求编排、账号/昵称校验、注册策略、登录返回资料和短期 session token 发放。
- `services/gatehub` 负责维护登录后的在线连接索引、断线重连窗口和同账号重复登录顶替策略。
- `share/proto/client` 放客户端协议，`share/proto/internal` 放服务间协议。

## 错误码分层

- 基础设施错误码统一放在 `engine/src/base/error/abe_error.h`，例如 `ABE_NOT_FOUND`、`ABE_PARSE_ERROR`、`ABE_CONNECT_FAILED`。
- base/common/backends 模块可以保留自己的前缀状态名，例如 `ABE_CONFIG_NOT_FOUND`、`ABE_DB_QUERY_FAILED`，但这些值应当别名到 `abe_status_t`，不再维护各自独立的负数区间。
- 客户端可见和业务逻辑错误放在 `share/proto/client/protocol.proto` 的 `ErrorCode`；服务层负责把底层 `abe_status_t` 转成合适的业务错误，不把基础设施错误号直接暴露给客户端。
- `services` 需要引用业务错误码时依赖 `abe_proto_client` 生成的 `protocol.pb.h`；本地状态名只能作为 `ErrorCode` 的兼容别名，不能再定义独立错误号。

数据库模块的目标结构示例：

```text
engine/src/common/db/          稳定的数据库 C 接口
engine/src/backends/db_mysql/  基于 MySQL C API 的具体实现
engine/src/adapters/db_cpp/    基于 abe_db_t 的 C++11 及以下简单 RAII/类接口
```

数据库、消息和缓存后端放在 `engine/src/backends`，只暴露项目自己的 C 接口，第三方头文件只允许出现在
对应 `.c` 实现文件中：

```text
engine/src/backends/db_mysql/  基于 MySQL C API 的数据库实现
engine/src/backends/redis/     基于 hiredis 的 Redis 同步和非阻塞命令接口
engine/src/backends/kafka/     基于 librdkafka 的 Kafka producer/consumer 接口
engine/src/backends/rabbitmq/  基于 rabbitmq-c 的 RabbitMQ publish/consume 接口
```

这些后端都是可选构建目标。缺少对应开发包时，CMake 会跳过该 target，不影响基础 engine 构建。

网络模块的 C++ 适配位于 `engine/src/adapters/net`，只基于 `base/net` 的 C 接口提供简单封装，
不直接包含 libevent 等具体后端接口。

- `Loop` 封装事件循环，`update()` 是非阻塞轮询，适合服务主循环每帧调用。
- `TcpLink` 表示一条已经建立的 TCP 连接，用于 `connect`、`send`、`disconnect`、
  `on_connect`、`on_receive` 和 `on_disconnect`。
- `UdpLink` 表示一个 UDP 端点，用于 `bind`、`send_to`、`unbind` 和 `on_receive`。
- `TcpListener` 是偏底层的监听 socket 封装，只负责 `listen` 和 accept。accept 回调中传入的
  `TcpLink` 只在回调期间有效；需要保存服务端连接时，用自己的 `TcpLink` 调用 `attach()` 接管。
- `TcpServer` 是服务端运行层封装，内部包含一个 `TcpListener`，并使用调用方传入的 `TcpLink`
  数组保存多条客户端连接。它提供 `init()`、`update()`、`close()`、`send_to_all()`、
  `active_count()` 和 `capacity()`。
- `TcpClient` 是客户端运行层封装，使用调用方传入的 `TcpLink` 数组保存多条外连连接。
  `connect_one()` 每次占用一个空闲 link 发起连接，多个 link 时可以多次调用。

文件职责上，`abe_net_link.h/.cpp` 只放 `Loop`、`TcpLink`、`TcpListener` 和 `UdpLink`；
`abe_net_server.h/.cpp` 只放 `TcpServer` 和 `TcpClient` 运行层封装。

TCP/UDP 收包统一通过 `on_receive` 回调通知，不提供阻塞式 `receive`，避免 adapter 层维护额外
缓存队列或改变 `base/net` 的事件模型。`TcpServer/TcpClient` 不自己创建连接数组，调用方负责
提供存储并保证它们的生命周期覆盖 `init()` 到 `close()`。

## 当前迁移说明

- 原顶层 `server/gate`、`server/lobby`、`server/game` 已统一归入 `server/services/`，避免服务进程散落在源码根目录。
- 原 `server/engine/src/module` 曾统一改名为 `adapters`；现在进一步拆分为 `backends` 和 `adapters`，避免“第三方后端实现”和“C 到 C++ 适配”共用同一个概念。
- MySQL、Redis、Kafka 和 RabbitMQ 已按新规则放入 `engine/src/backends`，spdlog 已按新规则位于 `engine/src/log`。
- 后续新增的 C++ 包装只放入 `engine/src/adapters`，编译标准不高于 C++11，且不在公共接口中暴露 STL，例如基于 `abe_db_t` 的数据库 RAII 接口。
- `share/proto` 位于 `server` 和 `client` 的同级目录，避免游戏协议定义被误认为服务端引擎基础设施。
- gateway 之类的 service 只消费 `share/proto` 的协议定义，不在 service 代码里重复定义协议号和消息结构。

## Service Runtime 约定

`server/services/common` 提供轻量的 `ServiceRuntime` 进程骨架。公共 runtime 负责：

- 调用每个服务模块的默认配置初始化。
- 加载服务固定 JSON 配置文件，并按 `runtime.*`、`log.*`、后端配置和服务自己的配置段初始化。
- 初始化日志。
- 初始化必需的 MySQL 工作线程连接池和 Redis 非阻塞连接，并通过 runtime context 交给具体服务。
- 创建雪花 ID 生成器，并通过 runtime context 交给具体服务。
- 创建并持有网络 `Loop`、公共时间轮和公共消息处理队列。
- 安装停止信号处理。
- 按主循环顺序执行 `Loop::update()`、时间轮 update、消息队列 tick、MySQL/Redis 异步回调和服务 `update()`。
- 关闭服务，再销毁消息队列、时间轮、网络 `Loop`、Redis、MySQL 工作线程、雪花 ID、配置和日志。

每个具体服务保留自己的 server 对象，并继承公共 `Service` 接口。入口里只创建 server，然后调用
`run(server)`。服务模块仍然负责自己的业务资源，例如监听端口、SessionManager、RPC 客户端、缓存连接等。

## 服务间消息

服务间消息继续复用 `engine/src/common/protocol/abe_protocol.h` 的 `MsgHeader + Body` 包格式。当前仓库不再保留独立 RPC target；后续需要 RPC 客户端或服务端时，优先放在具体 `services` 模块或 `services/common` 的小型公共组件里，由服务层直接装配真实网络和后端连接。

## 玩家数据存储

玩家持久化数据按“固定查询列 + protobuf blob”落库：

- `share/proto/store/player_store.proto` 是账号、玩家、背包、任务、邮件的唯一完整数据结构定义；玩家 protobuf 统一使用 `PB_PLAYER_*` 命名。
- MySQL 表结构放在 `deploy/sql/mysql/001_player_store.sql`，当前包含 `account_data`、`player_data`、`bag_data`、`task_data`、`mail_data`。当前数据库尚未投入数据，直接以该脚本创建新表；后续已投入数据的表结构变更须新增版本化迁移脚本。
- 每张表只展开业务查询、排序、分片或排障常用的固定列，例如 `uid`、`account_id`、`state`、`level`、`send_time_ms`、计数和版本字段。
- 完整业务数据统一写入 `data_blob`，内容是对应的 `PB_*_DATA` protobuf 二进制。
- `server/services/common/store` 提供 `PlayerStore` 抽象和 `MysqlPlayerStore` 实现；它依赖 `abe_db_t`，不直接暴露 MySQL 后端类型。
- 具体服务只依赖存储抽象和 protobuf 数据对象；真实 DB 连接仍由 `services` runtime 装配。

背包内部按 protobuf 分成 `item_list`、`equipment_list`、`appearance_list`；邮件可按 `mail_id` 单封读取，也可按 `uid` 加状态加载列表。需要给运营或排障查询的新条件，优先补固定投影列；只参与业务内存计算、无需 SQL 查询的字段留在 protobuf blob 中。

## 全局 ID 与异步后端

`common/id/abe_snowflake.h` 提供全区全服共用的 64 位雪花 ID。格式为 41 位自 `2024-01-01T00:00:00Z` 起的毫秒、10 位 `id.node_id` 和 12 位毫秒内序号。账号 ID、玩家 UID、邮件 ID、背包内实例 ID、任务实例 ID 都从同一生成器取得。每个正在运行的服务进程必须拥有全局唯一的 `id.node_id`，范围 `0..1023`；本地开发可使用 `0`，部署环境禁止复用节点号。

运行时 `Context` 提供 `id_generator`、`mysql` 和 `redis`：

- MySQL 使用 `abe_db_mysql_async_*`。每个工作线程独占一个连接，SQL 在工作线程执行，查询结果复制后由服务主循环回调；回调内才可读取结果，不能保存结果指针。
- Redis 使用 `abe_redis_async_*` 和 hiredis 非阻塞连接。连接完成前命令返回 `ABE_WOULD_BLOCK`，服务主循环持续调用 `update` 后再提交命令。
- 公共 runtime 已自动在每个 tick 驱动这两个异步句柄，因此服务代码不创建 DB 线程，也不在 Session 中阻塞等待数据库。

现有 `abe_db_t` 和 `MysqlPlayerStore` 保留给启动迁移、运维脚本或明确的同步管理路径；正常在线服务通过 `Context::mysql` 和 `Context::redis` 发起异步访问。
`gateway.server_id` 仍是 Session 路由的服务标识，不替代 `id.node_id`；后者必须按进程实例全局分配。

## Gateway 服务

`server/services/gateway` 提供 gateway 进程的基础骨架：

- `abe_gateway` 是可启动进程，默认监听 `0.0.0.0:7000`。
- gateway 可执行文件固定输出到 `bin/abe_gateway`，配置文件固定使用 `bin/gate.json`。
- `abe_gateway_main.cpp` 只创建 `GatewayServer`，然后调用公共 `run()`。
- `GatewayServer` 是普通 server 对象，封装 gateway 模块生命周期，持有 tcp server、SessionManager、link 槽位和 gateway session 槽位。
- `GatewayServer` 默认配置文件是 `bin/gate.json`，服务进程不再接受命令行参数覆盖。
- 网络 `Loop` 由 `ServiceRuntime` 创建和驱动，`GatewayServer` 只在初始化时把监听 server 挂到该 loop。
- 进程采用单主循环事件驱动模型，不创建业务线程；需要扩容时优先多开进程实例。
- `GatewayServer` 把 `TcpServer` 回调接到 `GatewaySession` 和 `service/session::SessionManager`。
- `GatewaySession` 继承 `service/session::Session`，是每条客户端 link 的会话对象。
- TCP 外层仍使用 `engine/base/net` 的 4 字节大端长度头。
- TCP payload 是固定 `MsgHeader` 加变长 `Body`。`MsgHeader` 的二进制编解码在 `engine/src/common/protocol`。
- `MsgHeader.msg_id` 是消息 ID，`Body` 是 `share/proto/client/protocol.proto` 中定义的 `PB_<消息ID枚举名>` protobuf 消息。
- libevent 收到 TCP payload 后由 gateway 入队，runtime 出队时交给 `GatewayServer::process_message`；
  `GatewayServer` 找到对应 `GatewaySession`，由 `GatewaySession::handle_packet` 解固定头得到 `msg_id`
  和 body，再走 Gateway 自己的消息注册表；具体 protobuf 对象由成员处理函数解析。

### Gateway 消息处理链条

客户端到服务的消息处理按以下顺序执行：

1. `ServiceRuntime::run_loop` 每 tick 调用 `context->loop->update()`，驱动 libevent 网络事件。
2. `engine/src/base/net` 的 TCP read callback 按 4 字节大端长度头拆包，取出 payload。
3. `engine/src/adapters/net` 把 C 网络回调转成 C++ `TcpLink` / `TcpListener` 的 `on_receive` 回调。
4. `GatewayServer::tcp_on_receive` 调用 `GatewayServer::on_receive`。
5. `GatewayServer::on_receive` 不直接执行业务，而是把 `TcpLink*`、`link_id`、packet 拷贝进公共 `MessageQueue`。
6. `ServiceRuntime::update_message_queue` 按 `runtime.message_tick_hz` 驱动队列，每 tick 最多处理 `runtime.message_max_per_tick` 条。
7. `MessageQueue::process` 构造 `service::common::Message`，调用当前 service 的 `process_message()`。
8. `GatewayServer::process_message` 从 `Message.source` 取回 `TcpLink*`，再调用 `GatewayServer::dispatch`。
9. `GatewayServer::dispatch` 根据 `link_id` 找到 `GatewaySession`，调用 `GatewaySession::handle_packet`。
10. `GatewaySession::handle_packet` 进入 `Session::receive()`，由基类更新会话活跃时间，再调用 `GatewaySession::on_message()` 解包。
11. 登录、大厅和游戏消息按 ID 区间进入后端路由；Gateway 本地消息在所有 session 共享的静态 `handlers_` 中查找成员处理函数，并通过 `(this->*handler)(message)` 调用。

回包链条是反向的：业务 handler 调 `Session::send()`，进入 `GatewaySession::send_packet()`，再调用 `TcpLink::send()`；底层 `abe_net_tcp_send()` 会重新加 4 字节长度头并写回 socket。

服务间消息使用同一个 `MsgHeader + Body` 包格式。后续装配 RPC 或服务路由时继续复用 `rpc_id`、`source_server`、`target_server`、`route_type` 和 `flags` 字段。

`GatewaySession` 构造时通过 `std::call_once` 初始化共享注册表，注册项是“消息 ID -> 非静态成员处理函数”，不再为每个 session 保存一份函数表。当前仅 `CS_PING` 由 Gateway 本地处理并回复 `PB_SC_PONG`；登录、角色、大厅和游戏请求进入后端路由，路由连接尚未装配时返回 `ERROR_CODE_SYSTEM_SERVICE_UNAVAILABLE`。当前登录业务 `LoginServer::handle_login()` 已实现 `PB_CS_LOGIN_REQ -> PB_SC_LOGIN_RESP` 的核心处理，后续需要把 Gateway 后端路由接到 login/gatehub 的服务间 handler 上。

## Login 和 GateHub 服务

`server/services/login` 提供登录服务骨架和账号校验核心：

- `abe_login` 是可启动进程，默认配置文件是 `bin/login.json`。
- 账号只允许 ASCII 字母、数字、`_`、`-`、`.`，长度 4 到 32，并拒绝 SQL 关键字、注释符、引号、反斜杠等 SQL-like 输入。
- 昵称要求合法 UTF-8，长度 2 到 16 个 codepoint，拒绝控制字符、SQL-like 输入和配置的脏字。
- `login.dirty_words` 使用逗号、竖线、分号或换行分隔。
- `login.unique_nickname=true` 时昵称全局唯一；重复昵称返回 `ERROR_CODE_AUTH_NICKNAME_EXISTS`。
- 登录成功返回 `PB_LOGIN_ACCOUNT_INFO`、`PB_PLAYER_ID`、短期 `session_token` 和过期时间。
- 重连请求带旧 `session_token`，校验通过后保留原 token 并切换到新 gateway/connection。
- `login.replace_duplicate_login=true` 时同账号新登录会顶替旧连接；为 `false` 时返回 `ERROR_CODE_AUTH_DUPLICATE_LOGIN`。

`server/services/gatehub` 提供独立的在线会话索引：

- `abe_gatehub` 是可启动进程，默认配置文件是 `bin/gatehub.json`。
- 维护 `uid -> session_id/session_token/gateway_id/connection_id`。
- 连接断开后，如果 `gatehub.allow_reconnect=true`，会话进入 `GATEHUB_SESSION_RECONNECTING`，在 `gatehub.reconnect_grace_ms` 内允许用原 token 重新绑定。
- 超过重连窗口或 session TTL 后，`update()` 会清理会话。
- `gatehub.replace_duplicate_login=true` 时同账号新登录会返回旧连接信息，调用方据此踢掉旧连接；为 `false` 时拒绝新登录。

构建和运行脚本：

```bash
# 在项目根目录执行。Docker 包装脚本默认进入 deploy/docker 的 dev 容器。
deploy/docker/build.sh          # 在 dev 容器内编译 abe_gateway
deploy/docker/rebuild.sh        # 在 dev 容器内清理默认 build 目录后重新编译 abe_gateway
deploy/docker/build.sh abe_login
deploy/docker/build.sh abe_gatehub

# 如果已经在容器 /workspace 内执行，使用当前环境脚本编译和起停服务。
scripts/build.sh
scripts/rebuild.sh
scripts/services_start.sh login gatehub gateway
scripts/services_stop.sh login gatehub gateway
```

Docker 包装脚本默认 build 目录为容器本地 `/tmp/abe_engine_build/engine`，避免共享目录并发写构建产物时出现
截断文件；纯编译脚本 `scripts/build.sh` 和 `scripts/rebuild.sh` 默认 build 目录为 `build/engine`。
gateway、login、gatehub 可执行文件分别输出到 `bin/abe_gateway`、`bin/abe_login`、`bin/abe_gatehub`；
配置文件分别默认为 `bin/gate.json`、`bin/login.json`、`bin/gatehub.json`。可以用 `BUILD_DIR`
覆盖 build 目录；服务配置文件路径由对应 server 的 `config_path()` 固定提供。
服务启停脚本只负责启动已经编译好的二进制，不会自动编译代码。默认 pid 写到
`bin/run/<service>.pid`，stdout/stderr 写到 `bin/logs/<service>/stdout.log`。
停服脚本只删除 pid 文件，不删除日志。三个服务的默认配置都使用 `log.output=daily`，
所以业务日志写文件，不会打印到启服终端；实时查看可以直接 tail 日志文件。

gateway 专属参数只从 `bin/gate.json` 的 `gateway.*` 读取。公共 runtime、日志、数据库、Redis
和雪花 ID 设置也只从各服务固定 JSON 的对应字段读取。MySQL 和 Redis 是服务启动必需依赖，
runtime 初始化失败会直接停止服务启动。
gateway、login 和 gatehub 的 JSON 配置字段都直接通过 `abe_config.h` 的 `abe_config_get_*`
接口按 path 读取；服务代码不维护自己的配置读取接口。

对应 JSON 配置键：

```text
runtime.tick_ms
runtime.timer_max_count
runtime.message_tick_hz
runtime.message_max_per_tick
runtime.message_queue_capacity
log.output
log.file
log.dir
log.level
log.utc_offset_minutes
mysql.host
mysql.port
mysql.database
mysql.user
mysql.password
mysql.worker_count
mysql.queue_capacity
redis.host
redis.port
redis.password
redis.database
redis.connect_timeout_ms
redis.command_timeout_ms
redis.memory_pool_capacity
id.node_id
gateway.host
gateway.port
gateway.max_clients
gateway.backlog
gateway.max_packet_size
gateway.server_id
gateway.idle_ms
login.max_accounts
login.allow_register
login.unique_nickname
login.require_auth_token
login.dirty_words
login.default_region
login.max_sessions
login.allow_reconnect
login.replace_duplicate_login
login.reconnect_grace_ms
login.session_ttl_ms
gatehub.max_sessions
gatehub.allow_reconnect
gatehub.replace_duplicate_login
gatehub.reconnect_grace_ms
gatehub.session_ttl_ms
```

## 日志约定

`engine/src/log` 提供 spdlog 的简洁 C++11 封装。按天写文件时：

```text
<root_directory>/YYYY-MM-DD/<logger_name>.log
```

gateway 默认配置写到 `bin/logs/gateway/YYYY-MM-DD/gateway.log`。
gateway 进程 stdout/stderr 写到 `bin/logs/gateway/stdout.log`。查看当前日志：

```bash
tail -F bin/logs/gateway/stdout.log bin/logs/gateway/$(date +%F)/gateway.log
```

例如东八区日志：

```cpp
abe::log::init_daily_file("game", "logs", 8 * 60);
ABE_LOG_INFO("room id=%d", room_id);
```

- `utc_offset_minutes` 使用固定 UTC 偏移分钟数，东八区传 `480`，纽约冬令时可传 `-300`。
- 日志当前时间统一来自 `base/time` 的 `abe_time_real_ms()`，日志时间戳和日期目录使用同一个偏移计算，不修改进程全局 `TZ`。
- 跨过当地午夜后，下一条日志自动创建新的日期目录并切换文件。
- `init_console`、`init_file` 和 `init_daily_file` 会关闭当前 logger；初始化和关闭应放在不并发写日志的生命周期阶段。

## 时间接口约定

`engine/src/base/time/abe_time.h` 提供基础时间 C 接口：

- `abe_time_real_ms()` 和 `abe_time_real_sec()` 返回 Unix 真实时间；如果系统墙上时钟回拨，接口会保持上一次已经返回的值，不能倒退。
- `abe_time_mono_ms()` 和 `abe_time_mono_sec()` 返回单调时间，只用于定时器和计算耗时，不代表 Unix 时间。
- `abe_time_get_timezone_offset_minutes()` 返回当前系统本地时区相对于 UTC 的分钟偏移，例如东八区为 `480`。
- `abe_time_get_date()` 按系统本地时区拆解 Unix 秒时间戳；`abe_time_get_date_with_offset()` 按指定固定 UTC 偏移拆解，星期值遵循 `0=星期日` 到 `6=星期六`。
- `abe_time_diff_ms()` 接收两个毫秒时间戳，结束时间早于开始时间时返回错误，否则输出完整的天、小时和分钟。

时间轮规则：

- `abe_time_wheel_create()` 默认是手动时间轮，适合单元测试和自定义 tick；`start_time_ms` 由调用方指定。
- `abe_time_wheel_create_mono()` 创建单调时间轮，内部起点使用 `abe_time_mono_ms()`；服务进程运行时优先使用这个模式。
- `abe_time_wheel_update_mono()` 用当前单调时间推进时间轮，避免系统真实时间调整影响定时器。
- `abe_time_wheel_schedule_once()` 和 `abe_time_wheel_schedule_repeat()` 的 `delay_ms`、`interval_ms` 都是单调时长。
- `abe_time_wheel_schedule_once_at_mono_ms()` 接收 `abe_time_mono_ms()` 体系下的绝对单调毫秒。
- `abe_time_wheel_schedule_once_at_utc_ms()` 接收 UTC Unix 毫秒时间戳，并在调用时转换为单调时间目标；用于“某个 UTC 时刻触发”的一次性定时器。
- `abe_timer_reschedule_at_mono_ms()` 和 `abe_timer_reschedule_at_utc_ms()` 用于按单调绝对时间或 UTC 绝对时间重排已有定时器。

## 文件权限约定

- 仓库里的普通源码、配置、文档文件默认使用 `0644`。
- 只有明确需要执行的脚本才使用 `0755`。
- 如果未来把脚本复制进镜像，必须在 Dockerfile 里显式设置权限，例如 `COPY --chmod=0755` 或 `RUN chmod +x`。
- 当前 `dev` 容器通过 bind mount 挂载源码目录，`/workspace` 的最终权限取决于宿主机文件系统；跨机器拷贝时不要依赖共享目录自动保留执行位。
- 需要把带源码的镜像搬到其他机器时，使用 Dockerfile 的 `portable` 目标；它通过 `COPY --chmod=u=rwX,go=rX` 在镜像内固定文件权限。
