# ABE Engine 目录职责定义

本文定义仓库目录和文件归属边界。后续新增源码、测试、部署文件和架构文档时，先按本文选择目录；如果一个文件同时像是属于多个目录，优先按依赖方向和运行时职责判断。

项目常驻规则仍以 [AGENTS.md](../AGENTS.md) 和仓库 skill `.codex/skills/abe-engine-server-rules/SKILL.md` 为准。本文是这些规则在目录层面的落地说明。

## 1. 总体分层

```text
abe_engine/
  .codex/          Codex 项目规则、技能和协作配置
  deploy/          本地和线上部署资产
  doc/             架构、环境、目录和工程约束文档
  scripts/         当前环境内运行的项目命令脚本
  server/          服务端源码
  test/            单元、集成、压测测试
```

服务端源码按下面方向依赖：

```text
engine/base -> engine/common -> engine/adapters -> services
                         \----> engine/backends --------> services
engine/base -> engine/log ----------------------> services
share/proto ------------------------------------> common/services
```

关键边界：

- `engine/src/base` 和 `engine/src/common` 定义稳定 C 契约，不依赖上层。
- `engine/src/backends` 放具体第三方后端实现，不向 engine 公共头泄漏第三方类型。
- `engine/src/adapters` 只做 C API 到简单 C++11-or-earlier 接口的适配。
- `services` 放服务进程入口、配置加载、依赖装配和生命周期，不承载复杂业务规则。
- `share/proto` 放协议定义，不放生成代码和业务逻辑。

## 2. 根目录

| 路径 | 放什么 | 不放什么 | 后续补全方向 |
| --- | --- | --- | --- |
| `AGENTS.md` | 仓库级强约束、必读规则、跨目录工程标准。 | 具体模块的大段实现设计。 | 新增跨项目硬约束时更新；模块细节放到 `doc/` 或对应 README。 |
| `.dockerignore` | Docker 构建上下文排除规则。 | 业务配置和运行时参数。 | 随构建产物、缓存目录、生成目录变化更新。 |
| `.codex/` | Codex 项目技能、代理配置和协作规则。 | 业务源码、测试源码、部署脚本。 | 项目规则变化时更新 skill；不要把普通文档搬进这里。 |
| `deploy/` | Docker、Kubernetes、systemd 等部署资产。 | 游戏逻辑、engine 实现、当前环境内的项目命令脚本。 | 按部署形态拆子目录，脚本只做环境和部署编排管理。 |
| `doc/` | 架构设计、开发环境、目录职责、工程决策。 | 可执行源码、生成代码、大型二进制资产。 | 新增设计说明、ADR、目录契约和运维手册。 |
| `scripts/` | 当前环境内运行的项目命令脚本，例如编译、测试、服务起停。 | Docker/Compose 环境控制、容器创建、部署编排模板。 | 后续新增服务时扩展 `services_start.sh` / `services_stop.sh` 的服务表。 |
| `server/` | 服务端工程源码根目录。 | 测试工程、部署编排、长期架构文档。 | 按 `engine/services/share` 分层补全。 |
| `test/` | 单元、集成、压测测试。 | 生产入口、真实部署配置。 | 与源码目录镜像对应，按风险补覆盖。 |

## 3. Codex 配置目录

| 路径 | 放什么 | 不放什么 |
| --- | --- | --- |
| `.codex/skills/` | 本仓库专用技能目录。 | 通用业务文档和运行时配置。 |
| `.codex/skills/abe-engine-server-rules/` | ABE Engine 服务端规则 skill。 | 模块源码和测试。 |
| `.codex/skills/abe-engine-server-rules/agents/` | 与该 skill 配套的代理配置。 | 不属于 Codex 协作的项目文件。 |

修改 `server/engine`、构建文件、架构文档或测试前，必须先应用 `abe-engine-server-rules` 中的边界。

## 4. 部署目录

| 路径 | 放什么 | 不放什么 | 后续补全方向 |
| --- | --- | --- | --- |
| `deploy/docker/` | 本地开发容器、Compose 依赖服务、Docker 操作脚本、镜像构建说明、容器包装命令。 | 业务服务启停脚本、游戏逻辑、engine 实现。 | 保持 `dev.sh` 只做环境操作；服务起停入口放 `scripts/`。 |
| `deploy/sql/mysql/` | MySQL schema 和迁移 SQL，例如玩家数据投影表。 | 业务逻辑、数据库访问代码、临时查询草稿。 | 后续按版本递增补迁移文件，保持可重复执行或明确迁移顺序。 |
| `deploy/k8s/` | Kubernetes manifests、Helm/Kustomize 配置、服务发现和健康检查部署模板。 | 本地 Docker 专用脚本、源码。 | 后续按 `gateway/session/coordinator/room/settlement` 拆 deployment/service/configmap。 |
| `deploy/systemd/` | 单机或物理机部署的 unit、环境文件模板、滚动重启脚本。 | K8s manifest、业务逻辑。 | 适合 Room Server 或压测工具的物理机部署模板。 |

部署目录只负责“如何运行”，不决定业务规则。服务业务和进程组装统一放 `services/`。

## 5. 文档目录

| 路径 | 放什么 | 不放什么 |
| --- | --- | --- |
| `doc/README.md` | 文档入口、开发环境索引、常用命令入口。 | 大段模块细节实现。 |
| `doc/global_room_server_framework_design.md` | 全区全服房间服务器总体架构设计。 | 与当前代码无关的零散笔记。 |
| `doc/directory_layout.md` | 仓库目录职责契约，也就是本文。 | 单个函数或类的 API 说明。 |

新增设计文档优先放 `doc/`。如果是服务端源码目录说明，先更新 `server/README.md`，再从 `doc/README.md` 链接过去。

## 6. 服务端源码总览

```text
server/
  engine/       基础设施和共享能力
  services/     进程入口和依赖装配
  share/proto/  协议定义
```

### 6.1 `server/engine`

`server/engine` 是基础设施层。项目自维护的公开接口必须保持 C 或 C++11 可编译；`base/common` 公共头优先保持 C ABI 和 C++03 可包含。

| 路径 | 放什么 | 不放什么 | 后续补全方向 |
| --- | --- | --- | --- |
| `server/engine/CMakeLists.txt` | engine 聚合构建入口。 | 具体服务业务规则。 | 聚合 engine、proto、services 和测试入口。 |
| `server/engine/src/base/` | 最底层基础设施 C API。 | common/services 依赖、具体业务规则。 | 每个子模块独立 CMake target，公共头保持 C 兼容。 |
| `server/engine/src/common/` | 跨服务稳定 C 接口和抽象。 | 具体 MySQL/Redis/Kafka/RabbitMQ 头、adapter C++ 便利层。 | DB、ID、RPC、服务发现等 C 契约。 |
| `server/engine/src/log/` | spdlog 的薄 C++11 封装和日志宏。 | C ABI 日志桥、业务日志策略。 | 只有 C 模块真的需要时再新增最小 C 接口。 |
| `server/engine/src/backends/` | 第三方依赖接入和 C 契约实现。 | 上层业务逻辑、C++ adapter。 | 每个后端单独 target，可选构建。 |
| `server/engine/src/adapters/` | `base/common` C API 到简单 C++11 RAII/class 的适配。 | 具体后端实现、第三方原始类型泄露。 | C++ 业务需要便利接口时在这里补。 |

#### 6.1.1 `engine/src/base`

| 路径 | 放什么 | 不放什么 |
| --- | --- | --- |
| `base/config/` | 简单配置读取、键值解析、基础配置结构。 | 服务灰度规则和动态配置中心客户端的业务策略。 |
| `base/error/` | 基础设施统一错误码和错误码名称转换。 | 客户端可见业务错误码、具体模块日志文案。 |
| `base/math/` | 向量、矩阵、随机数、定点数、几何检测等基础数学 C API。 | 玩法规则、C++ 模板数学库。 |
| `base/memory/` | 普通内存池、对象池、临时分配器等高频内存基础设施。 | 业务对象生命周期和跨进程共享状态。 |
| `base/metrics/` | 指标基础结构、Prometheus 文本输出等低层能力。 | 某个服务的指标含义和告警规则。 |
| `base/net/` | libevent 网络和事件循环 C API。 | adapter 层 C++ socket 类、服务进程网络业务处理。 |
| `base/shm/` | 共享内存池和跨进程共享结构基础设施。 | 进程内裸指针持久化、业务房间状态直接定义。 |
| `base/threading/` | 基础线程、锁、队列或唤醒原语。 | actor 业务模型和服务级调度策略。 |
| `base/time/` | real/mono 时间、日期拆解、时间差、时间轮。 | 业务活动时间规则和结算周期配置。 |

`base` 子目录新增文件时，优先使用 `.h + .c`，接口返回错误码，通过输出参数返回复杂结果，回调携带 `void* user_data`。

#### 6.1.2 `engine/src/common`

| 路径 | 放什么 | 不放什么 |
| --- | --- | --- |
| `common/db/` | 数据库 C 抽象、连接、查询、事务、结果集、driver vtable。 | MySQL C API 头和具体 SQL 业务。 |
| `common/error/` | 跨服务错误分类、错误映射和错误转换辅助。基础错误码放 `base/error/`。 | 客户端展示文案、具体模块日志。 |
| `common/id/` | 全局 ID 生成和解析 C 接口，例如雪花 ID。 | 数据库自增策略依赖和业务对象存储。 |
| `common/protocol/` | 协议头、包边界、opcode 基础定义。 | Protobuf 生成代码和业务消息处理。 |
| `common/rpc/` | 服务间 RPC 抽象、请求上下文、deadline、回调契约。 | 具体 gRPC 服务实现和业务 handler。 |
| `common/serialization/` | 通用序列化辅助和 buffer 契约。 | 具体玩法 payload 结构。 |
| `common/service_discovery/` | 服务注册、发现、租约、健康状态的稳定接口。 | Etcd/Consul/ZooKeeper 具体客户端类型泄露。 |

`common` 可以依赖 `base`，不得依赖 `backends/adapters/services`。

#### 6.1.3 `engine/src/backends`

| 路径 | 放什么 | 不放什么 |
| --- | --- | --- |
| `backends/db_mysql/` | 基于 MySQL C API 的同步访问和工作线程异步访问实现。 | 业务 SQL 组织、服务业务直接散落。 |
| `backends/redis/` | 基于 hiredis 的同步命令与非阻塞事件访问实现。 | 会话业务规则、排行榜规则。 |
| `backends/kafka/` | 基于 librdkafka 的 producer/consumer 接口。 | 结算事件业务语义。 |
| `backends/rabbitmq/` | 基于 rabbitmq-c 的 publish/consume 接口。 | 业务队列路由策略和 handler。 |

后端目录可以包含第三方头，但第三方类型不得进入 `base/common` 公共头，也不得要求整个 engine 升级到 C++14+。

#### 6.1.4 `engine/src/adapters`

| 路径 | 放什么 | 不放什么 |
| --- | --- | --- |
| `adapters/math/` | `base/math` 的简单 C++11 适配。 | 新的基础数学实现、模板-heavy 数学库。 |
| `adapters/net/` | `base/net` 的 `Loop/TcpLink/UdpLink/TcpServer/TcpClient` 等简单 C++ 封装。 | libevent 原始类型暴露、业务协议处理。 |

adapter 公共接口不得暴露 STL 容器、`std::function`、智能指针或第三方类型。

#### 6.1.5 `engine/src/log`

| 路径 | 放什么 | 不放什么 |
| --- | --- | --- |
| `log/abe_log.h` | 项目自己的日志级别、初始化函数和 `ABE_LOG_*` 宏。 | spdlog 类型、STL 容器、业务日志字段定义。 |
| `log/abe_log.cpp` | spdlog 初始化、文件/控制台输出、按天切目录实现。 | C 模块日志桥，除非未来 C 模块确实需要。 |

日志时间使用 `base/time` 的 real 时间接口，日期目录使用固定 UTC offset。

### 6.2 已移除的独立逻辑层

当前代码不再保留单独的 logic 层；公共服务组件放入 `services/common`，具体业务编排放入对应 `services/<name>` 模块。后续如果出现真正可复用、可独立测试的玩法状态机，再按实际边界决定新目录，而不是预留空层。

### 6.3 `server/services`

`server/services` 是进程入口和组装层。它负责把 `engine`、配置、日志、服务间消息、DB/MQ/Cache 后端连接起来。

| 路径 | 放什么 | 不放什么 | 后续补全方向 |
| --- | --- | --- | --- |
| `services/common/` | 各服务进程入口复用的轻量启动组件，例如 `ServiceRuntime`、命令行参数解析、配置/日志/DB 初始化、停止信号处理、统一启动返回码辅助。 | 具体服务的业务生命周期、网络监听对象、玩法规则。 | 保持小而直接，只抽公共启动流程，不做成万能应用框架。 |
| `services/common/session/` | 服务侧公共 Session 和 SessionServer 生命周期组件。 | Gateway 具体网络连接、登录账号规则、Redis 会话存储策略。 | 只保留连接绑定、状态流转、消息分发表等通用行为。 |
| `services/common/store/` | 上层可复用的持久化 repository，例如 `PlayerStore` 和 MySQL 实现。 | engine 公共 DB 契约、具体玩法规则、客户端协议处理。 | 存储实现依赖 `abe_db_t` 和 `share/proto/store`，真实连接由服务装配。 |
| `services/gateway/` | Gateway 进程入口、监听端口、连接生命周期、协议编解码接入、转发到后端服务。 | 账号登录规则、房间玩法逻辑。 | 装配 `adapters/net`，接入协议层和路由客户端。 |
| `services/lobby/` | 大厅服务入口、低频玩家请求路由、账户/社交/匹配入口聚合。 | Gateway 长连接实现、Room tick。 | 后续可根据业务规模拆出独立服务。 |
| `services/session/` | Session Service 进程入口、会话存储/缓存后端选择、服务间 handler。 | `services/common/session` 的基础状态流转。 | 连接 Redis 或其他会话存储。 |
| `services/coordinator/` | Room Coordinator 服务入口、房间元数据逻辑装配、Allocator/RPC/后端选择、服务生命周期。 | 复杂房间规则直接写在进程入口。 | 负责创建/加入/关闭房间控制面，不承载 Room tick。 |
| `services/match/` | Matchmaking Service 进程入口、匹配逻辑装配、队列分片、服务间 handler。 | 匹配规则核心算法直接散在 main。 | 后续按业务规模拆公共匹配组件。 |
| `services/game/` | Room Server 或 Game Service 进程入口、房间 runtime 装配、worker/tick 生命周期。 | 登录/session 规则、具体 DB 业务写入。 | 后续按业务规模拆公共房间组件，输出结算事件。 |
| `services/settlement/` | Settlement Service 进程入口、结算逻辑装配、DB/MQ/RPC 后端选择、补偿任务调度。 | 奖励计算细节直接写在进程入口。 | 后续按业务规模拆公共结算组件，持久化结果并投递事件。 |

服务目录可以依赖具体 `engine/backends`，但这种依赖应停留在装配层，不能倒灌到 `engine/base` 或 `engine/common`。

### 6.4 `server/share/proto`

| 路径 | 放什么 | 不放什么 | 后续补全方向 |
| --- | --- | --- | --- |
| `share/proto/client/` | 客户端和服务端之间的协议 IDL，例如登录、房间输入、广播。 | C++ 生成代码、服务间私有协议。 | 按功能拆 `login.proto`、`game.proto`、`protocol.proto`。 |
| `share/proto/store/` | 持久化数据结构 IDL，例如账号、用户、背包、任务、邮件。 | MySQL 建表语句、查询投影策略、客户端可见错误码。 | 固定列只放 SQL，完整状态以 `PB_*_DATA` blob 落库。 |
| `share/proto/internal/` | 服务间 RPC、控制面、事件流 IDL。 | 客户端可见协议和业务实现。 | 后续补 `session.proto`、`coordinator.proto`、`settlement.proto` 等。 |

生成代码应放入构建输出目录或明确的 generated 目录，不直接手改生成文件。
当前客户端 proto 通过 `share/proto/client` 下的 `abe_proto_client` target 生成 C++ 代码；上层逻辑错误码以 `protocol.proto` 的 `ErrorCode` 为唯一来源。

## 7. 测试目录

```text
test/
  unit/         快速单元测试
  integration/ 依赖服务或多模块联调
  load/        压测和机器人客户端
```

| 路径 | 放什么 | 不放什么 |
| --- | --- | --- |
| `test/unit/base/` | `engine/src/base` 的 C API 单元测试。 | 依赖 MySQL/Redis/Kafka 的集成测试。 |
| `test/unit/common/` | `engine/src/common` C 契约测试。 | 具体后端联调。 |
| `test/unit/backends/` | 后端头兼容性、可选后端最小行为测试。 | 业务逻辑测试。 |
| `test/unit/adapters/` | C++ adapter 生命周期和封装行为测试。 | 原始第三方库完整集成测试。 |
| `test/unit/log/` | 日志初始化、输出路径、轮转基础行为测试。 | 服务级日志字段语义。 |
| `test/unit/services/` | 服务模块和 `services/common` 公共组件单元测试。 | 真实服务进程启动。 |
| `test/integration/` | 需要 Docker 依赖服务、多模块组合、真实网络或真实后端的测试。 | 高频小单测。 |
| `test/load/` | 机器人客户端、连接数、消息量、房间 tick 压测。 | 普通单元测试。 |

新增测试优先放到与源码层级对应的目录。能用单元测试覆盖的行为，不放到集成测试里才验证。

## 8. 后续新增文件规则

1. 先判断文件属于“基础设施、共享契约、后端实现、C++ 适配、业务逻辑、服务进程、协议、测试、部署、文档”哪一类。
2. 基础设施和共享契约优先进入 `server/engine/src/base` 或 `server/engine/src/common`，公共接口保持 C/C++11 兼容。
3. 具体第三方实现进入 `server/engine/src/backends`，不要让第三方头和类型出现在上层公共接口。
4. C API 的 C++ 便利封装进入 `server/engine/src/adapters`，不要在 adapter 中实现具体后端。
5. 业务状态机、规则、数据流转进入对应 `server/services/<name>` 模块；跨服务复用的小组件进入 `server/services/common`。
6. 进程入口、配置加载、服务间 handler 和真实后端装配进入 `server/services`。
7. 协议定义进入 `server/share/proto/client` 或 `server/share/proto/internal`，按可见范围区分。
8. 基础设施错误码进入 `server/engine/src/base/error`，模块状态名只做别名；客户端可见和业务逻辑错误码进入 proto 的 `ErrorCode`。
9. 单测跟随源码目录镜像放入 `test/unit`；真实外部依赖和跨进程测试放入 `test/integration`；容量验证放入 `test/load`。
10. 普通源码、配置和文档默认 `0644`；只有明确可执行脚本才设置 `0755`。
11. 如果目录职责需要改变，先更新本文和 `server/README.md`，再移动或新增代码。
