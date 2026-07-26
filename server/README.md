# Server 目录结构

`server/` 是服务端源码根目录。目录按“基础设施、共享能力、业务逻辑、服务进程、协议定义”分层。下面的箭头表示“右侧可以依赖左侧”：

```text
engine/base -> engine/common -> engine/adapters -> logic -> services
                         \----> engine/backends --------> services
engine/base -> engine/log ----------------------> logic/services
proto definitions ------------------------------> common/logic/services
```

## 目录职责

```text
server/
  engine/
    src/
      base/       基础设施，C API 优先，公开头保持 C ABI 和 C++03 兼容
      common/     稳定的共享 C 接口与跨服务基础能力，例如 ID、RPC、服务发现、DB 抽象
      log/        原生 C++ 日志封装，提供简洁的 C++11 接口和日志宏
      backends/   common/base 接口的具体实现，隔离 MySQL C API 等原始第三方接口
      adapters/   将 base/common 的 C 接口适配为不高于 C++11 的简单 RAII 和类接口
  logic/          游戏业务逻辑和玩法模块，可使用 C++11+
  services/       可独立启动的服务进程入口和组装层
  share/proto/    协议定义源文件，分为 client 和 internal
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

- `engine/src/base` 不依赖 `common`、`logic` 或 `services`。
- `engine/src/common` 可以依赖 `base`，负责定义稳定的共享 C 接口，不依赖具体后端、C++ adapter、`logic` 或 `services`。
- `engine/src/backends` 可以依赖 `base`、`common` 和原始第三方库，负责实现 C 接口或提供最小 C ABI 桥接；第三方类型不得进入 `base/common` 公共头。
- `engine/src/log` 直接封装原生 C++ 日志库，可依赖 `base/time` 获取时间，供 C++ 的 `logic/services` 使用；C 模块确实需要日志时，再单独设计最小 C 接口。
- `engine/src/adapters` 只依赖 `base/common` 暴露的 C 接口，将其适配为不高于 C++11 的简单 RAII 和类接口；不得承载 MySQL、spdlog 等具体后端实现。
- 原生 C++ 库如果只供 C++ 模块使用，直接在对应 C++ 模块中使用，不为统一形式机械地再封装一层 C 接口。
- 只有当 `base/common` 的 C 接口确实需要原生 C++ 实现时，才允许在 `backends` 中增加范围最小的 C ABI 桥接。
- `logic` 可以依赖 `base`、`common`、`log` 和 `adapters`，但不得直接依赖具体 `backends` 或包含第三方后端头文件。
- `services` 负责进程入口、配置加载、后端选择、依赖装配和服务生命周期，不承载复杂玩法逻辑。
- `services/common` 放各服务进程都会复用的启动组件，例如 `ServiceRuntime`、命令行参数解析、配置/日志/DB 初始化和停止信号处理；不放具体服务业务规则。
- `services/gateway` 负责把 `engine/adapters/net`、`logic/session` 和协议解码串起来；协议号和消息定义仍然以 `share/proto/client/protocol.proto` 为准。
- `share/proto/client` 放客户端协议，`share/proto/internal` 放服务间协议。

## 错误码分层

- 基础设施错误码统一放在 `engine/src/base/error/abe_error.h`，例如 `ABE_NOT_FOUND`、`ABE_PARSE_ERROR`、`ABE_CONNECT_FAILED`。
- base/common/backends 模块可以保留自己的前缀状态名，例如 `ABE_CONFIG_NOT_FOUND`、`ABE_DB_QUERY_FAILED`，但这些值应当别名到 `abe_status_t`，不再维护各自独立的负数区间。
- 客户端可见和业务逻辑错误放在 `share/proto/client/protocol.proto` 的 `ErrorCode`；服务层负责把底层 `abe_status_t` 转成合适的业务错误，不把基础设施错误号直接暴露给客户端。
- `logic/services` 需要引用业务错误码时依赖 `abe_proto_client` 生成的 `protocol.pb.h`；本地状态名只能作为 `ErrorCode` 的兼容别名，不能再定义独立错误号。

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
engine/src/backends/redis/     基于 hiredis 的 Redis 同步命令接口
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
- `server/share/proto` 保持在 engine 外部，避免游戏协议定义被误认为引擎基础设施的一部分。
- gateway 之类的 service 只消费 `server/share/proto` 的协议定义，不在 service 代码里重复定义协议号和消息结构。

## Service Runtime 约定

`server/services/common` 提供轻量的 `ServiceRuntime` 进程骨架。公共 runtime 负责：

- 调用每个服务模块的默认配置初始化。
- 合并公共参数和服务自己的参数表，并统一解析命令行。
- 加载可选 JSON 配置文件，命令行参数最终覆盖配置文件。
- 初始化日志。
- 按需初始化 MySQL 数据库连接和 Redis 连接，并通过 runtime context 交给具体服务。
- 创建并持有网络 `Loop`。
- 安装停止信号处理。
- 按主循环顺序执行 `Loop::update()` 和服务 `update()`。
- 关闭服务，再销毁网络 `Loop`、Redis、DB、配置和日志。

每个具体服务保留自己的 server 对象，并继承公共 `Service` 接口。入口里只创建 server，然后调用
`run(argc, argv, server)`。服务模块仍然负责自己的业务资源，例如监听端口、SessionServer、RPC 客户端、缓存连接等。

## Gateway 服务

`server/services/gateway` 提供 gateway 进程的基础骨架：

- `abe_gateway` 是可启动进程，默认监听 `0.0.0.0:7000`。
- gateway 可执行文件固定输出到 `bin/abe_gateway`，配置文件固定使用 `bin/gate.json`。
- `abe_gateway_main.cpp` 只创建 `GatewayServer`，然后调用公共 `run()`。
- `GatewayServer` 是普通 server 对象，封装 gateway 模块生命周期，持有 tcp server、SessionServer、link 槽位和 gateway session 槽位。
- `GatewayServer` 默认配置文件是 `bin/gate.json`，也可以通过 `--config <path>` 覆盖。
- 网络 `Loop` 由 `ServiceRuntime` 创建和驱动，`GatewayServer` 只在初始化时把监听 server 挂到该 loop。
- 进程采用单主循环事件驱动模型，不创建业务线程；需要扩容时优先多开进程实例。
- `GatewayServer` 把 `TcpServer` 回调接到 `GatewaySession` 和逻辑 `SessionServer`。
- `GatewaySession` 继承 `logic/session::Session`，是每条客户端 link 的会话对象。
- TCP 外层仍使用 `engine/base/net` 的 4 字节大端长度头。
- TCP payload 是固定 `MsgHeader` 加变长 `Body`。`MsgHeader` 的二进制编解码在 `engine/src/common/protocol`。
- `MsgHeader.msg_id` 是消息 ID，`Body` 是 `share/proto/client/protocol.proto` 中定义的 `PB_<消息ID枚举名>` protobuf 消息。
- gateway 只解固定头得到 `msg_id` 和 body，再转给 session handler；具体 protobuf 对象由业务 session 自己解析。

构建和运行脚本：

```bash
# 在项目根目录执行。Docker 包装脚本默认进入 deploy/docker 的 dev 容器。
deploy/docker/build.sh          # 在 dev 容器内编译 abe_gateway
deploy/docker/rebuild.sh        # 在 dev 容器内清理默认 build 目录后重新编译 abe_gateway

# 如果已经在容器 /workspace 内执行，使用当前环境脚本编译和起停服务。
scripts/build.sh
scripts/rebuild.sh
scripts/services_start.sh gateway
scripts/services_stop.sh gateway
```

Docker 包装脚本默认 build 目录为容器本地 `/tmp/abe_engine_build/engine`，避免共享目录并发写构建产物时出现
截断文件；纯编译脚本 `scripts/build.sh` 和 `scripts/rebuild.sh` 默认 build 目录为 `build/engine`。
gateway 可执行文件固定输出到 `bin/abe_gateway`，配置文件默认为 `bin/gate.json`。可以用 `BUILD_DIR`
覆盖 build 目录，用 `GATEWAY_CONFIG` 覆盖 gateway 配置文件。
服务启停脚本只负责启动已经编译好的二进制，不会自动编译代码。默认把 gateway pid 写到
`bin/run/gateway.pid`，stdout/stderr 写到 `bin/logs/gateway/stdout.log`。

gateway 专属参数只从 `bin/gate.json` 的 `gateway.*` 读取。命令行只保留公共 runtime 参数，
用于切换配置文件或临时覆盖日志、数据库、Redis 等公共运行环境。

公共命令行参数：

```text
--config <path>          JSON 配置文件，gateway 默认 bin/gate.json
--tick-ms <ms>           主循环 sleep 毫秒数，默认 10
--log-output <mode>      console/file/daily，默认 console；gateway 配置默认为 daily
--log-file <path>        log-output=file 时的日志文件
--log-dir <path>         log-output=daily 时的日志根目录，默认 logs；gateway 配置默认为 bin/logs/gateway
--log-level <level>      trace/debug/info/warn/error/critical/off，默认 info
--mysql-enable <0|1>     启动时是否连接 MySQL，默认 0
--mysql-host <host>      MySQL 地址，默认 127.0.0.1
--mysql-port <port>      MySQL 端口，默认 3306
--mysql-database <name>  MySQL 数据库名，默认服务名
--mysql-user <user>      MySQL 用户
--mysql-password <pwd>   MySQL 密码
--redis-enable <0|1>     启动时是否连接 Redis，默认 0
--redis-host <host>      Redis 地址，默认 127.0.0.1
--redis-port <port>      Redis 端口，默认 6379
--redis-password <pwd>   Redis 密码，默认空
--redis-database <index> Redis database，默认 0
--redis-connect-timeout-ms <ms> Redis 连接超时，默认 1000
--redis-command-timeout-ms <ms> Redis 命令超时，默认 1000
```

对应 JSON 配置键：

```text
runtime.tick_ms
log.output
log.file
log.dir
log.level
log.utc_offset_minutes
mysql.enable
mysql.host
mysql.port
mysql.database
mysql.user
mysql.password
redis.enable
redis.host
redis.port
redis.password
redis.database
redis.connect_timeout_ms
redis.command_timeout_ms
redis.memory_pool_capacity
gateway.host
gateway.port
gateway.max_clients
gateway.backlog
gateway.max_packet_size
gateway.server_id
gateway.idle_ms
```

## 日志约定

`engine/src/log` 提供 spdlog 的简洁 C++11 封装。按天写文件时：

```text
<root_directory>/YYYY-MM-DD/<logger_name>.log
```

gateway 默认配置写到 `bin/logs/gateway/YYYY-MM-DD/gateway.log`。

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
