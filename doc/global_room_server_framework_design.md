# 全区全服房间服务器框架设计文档

## 1. 背景与目标

本文档规划一个使用 C++ 实现的游戏后端服务器框架，目标形态是“全区全服、动态开房间、百万级同时在线”。这里的“全区全服”指玩家处在同一个逻辑服体系内，账号、匹配、房间、好友、排行榜等全局可见，不再按传统大区拆出多个互不相通的孤岛。

核心目标：

- 支持 100w 同时在线玩家，服务可水平扩容。
- 支持动态创建房间、匹配进房、邀请进房、断线重连、房间结算。
- 支持多机房或多可用区部署，单节点故障不影响整体服务。
- 业务逻辑以房间为核心隔离单元，降低共享状态复杂度。
- C++ 作为主服务语言，提供高性能网络、调度、协议、服务治理和基础组件。
- 框架层与玩法逻辑解耦，玩法可以以模块、脚本或动态库形式迭代。

非目标：

- 不在第一版实现所有业务系统，例如支付、商城、运营后台、反作弊判定平台。
- 不把全量房间状态都强一致持久化。实时房间以低延迟优先，结算结果和关键事件必须可靠。
- 不承诺跨洲远距离实时对战体验。跨地域可见和跨地域实时低延迟是两个不同问题。

## 2. 关键假设

容量设计需要先明确业务模型。若实际游戏类型不同，需要用真实数据替换下面的保守估算。

| 项目 | 初始假设 |
| --- | --- |
| 同时在线 | 1,000,000 CCU |
| 单房间人数 | 2 到 100 人，按玩法配置 |
| 房间平均时长 | 3 到 30 分钟 |
| 客户端上行消息 | 2 到 10 条/秒/人 |
| 服务端下行消息 | 5 到 30 条/秒/人，按广播范围裁剪 |
| 网关单实例连接数 | 20,000 到 50,000，视机器规格和协议而定 |
| 房间服单实例承载 | 500 到 5,000 人，取决于 tick、AOI、同步量、脚本开销 |
| 可用性目标 | 核心链路 99.9% 起步，成熟后提升到 99.95% |
| 延迟目标 | 同地域 P95 小于 80ms，P99 小于 150ms |

百万在线不是单点能力，而是整体系统的水平扩展能力。设计上每个服务都必须能按玩家、房间、账号或路由分片。

## 3. 总体架构

推荐采用“接入层 + 全局路由层 + 房间调度层 + 房间执行层 + 数据与事件层”的分层架构。

```text
Client
  |
  | TCP/WebSocket/UDP/QUIC
  v
L4/L7 Load Balancer
  |
  v
Gateway Cluster
  |
  | internal rpc / pubsub
  v
Session Service ---- Auth/Login Service ---- Account/Profile Service
  |
  v
Global Router ---- Matchmaking Service
  |
  v
Room Coordinator ---- Room Allocator ---- Room Server Cluster
  |
  v
Settlement Service ---- DB/Cache/Event Bus/Analytics
```

服务分层：

- Gateway：长连接接入、协议编解码、加解密、心跳、限流、粘包拆包、连接迁移辅助。
- Session Service：维护玩家在线状态、连接绑定、断线重连窗口、踢人和顶号。
- Auth/Login Service：认证、登录票据、区服入口、设备与风控信息校验。
- Global Router：根据玩家、房间、地域、版本和负载选择后端目标。
- Matchmaking Service：匹配队列、组队、规则筛选、匹配票据。
- Room Coordinator：房间元数据、房间创建、加入、销毁、租约、迁移决策。
- Room Allocator：根据节点容量和约束为房间选择 Room Server。
- Room Server：房间 actor、玩法 tick、状态同步、玩家输入处理、结算事件生成。
- Settlement Service：结算幂等处理、奖励、战绩、任务进度、排行榜事件。
- Data Layer：缓存、关系型数据库、KV、消息队列、对象存储、日志分析。

## 4. 全区全服模型

全区全服的重点是“逻辑全局唯一”，而不是所有请求都打到同一个物理集群。

### 4.1 全局唯一 ID

所有核心对象使用全局唯一 ID：

- `uid`：玩家 ID。
- `room_id`：房间 ID。
- `match_ticket_id`：匹配票据 ID。
- `session_id`：登录会话 ID。
- `trace_id`：链路追踪 ID。

ID 推荐结构：

```text
64-bit id = timestamp_bits + region_bits + service_bits + sequence_bits
```

优点：

- 按时间大致有序，方便日志和数据库索引。
- 包含 region 或 service 信息，便于定位。
- 不依赖单点数据库自增。

### 4.2 逻辑服与物理地域

逻辑服只有一个，但物理部署可以有多个地域或可用区：

- 玩家登录时进入最近或最优地域的 Gateway。
- 全局目录服务维护玩家当前位置、房间所在地域、房间所在节点。
- 匹配可以优先同地域，必要时跨地域。
- 房间一旦创建，应固定在一个主地域执行，避免实时逻辑跨地域写多份。

跨地域玩法建议：

- 休闲、异步、排行榜类：可以全局统一。
- 实时对战类：优先同地域匹配，跨地域只作为兜底。
- 大型世界或战场：按地图、房间、场景实例切分，而不是按传统大区切分。

### 4.3 全局目录

全局目录负责查询：

- 玩家当前是否在线。
- 玩家连接在哪个 Gateway。
- 玩家是否在房间中。
- 房间在哪个 Room Server。
- 服务实例健康状态和容量状态。

目录服务不能是单点。推荐按 `uid` 和 `room_id` 做一致性哈希分片，每个分片有主从或 Raft 组。读多写少的目录可以使用缓存加租约机制，避免所有心跳都打到强一致存储。

## 5. 核心服务设计

### 5.1 Gateway

Gateway 是连接密集型服务，主要职责是把不可信公网连接转换为可信内网消息。

职责：

- 监听客户端连接，支持 TCP/WebSocket，实时玩法可扩展 UDP/KCP 或 QUIC。
- TLS 终止或应用层加密。
- 协议头解析、包体长度校验、压缩、反序列化。
- 连接级限流、IP 限流、账号级限流、消息频率控制。
- 心跳检测、弱网重传辅助、断线通知。
- 将客户端消息路由到 Session、Lobby、Room 等服务。
- 将后端推送写回客户端。

设计要点：

- Gateway 不保存核心业务状态，只保存连接态和短期路由缓存。
- 单 Gateway 故障后，玩家可以重新连接到其他 Gateway 并恢复会话。
- 后端推送需要通过 `uid -> gateway_id -> conn_id` 查找连接。
- Gateway 对单连接设置发送队列上限，超限时触发降级或断开，防止慢客户端拖垮服务。

推荐线程模型：

```text
acceptor thread
  -> io worker threads
       -> protocol decode
       -> route to backend
       -> output queue
```

### 5.2 Session Service

Session Service 管理玩家在线态。

核心数据：

```text
uid
session_id
gateway_id
conn_id
login_token
room_id
state: online | reconnecting | offline
last_heartbeat_at
reconnect_deadline_at
```

关键能力：

- 顶号：同一账号新登录时踢掉旧连接。
- 断线重连：玩家短暂断线后可以回到原房间。
- 状态广播：通知好友、队伍、房间玩家上下线。
- 踢人：风控、封禁、运营命令。
- 会话租约：Gateway 周期续租，超时自动清理。

### 5.3 Auth/Login Service

登录链路需要独立于房间链路，避免登录峰值影响战斗。

职责：

- 校验平台 token。
- 生成短期登录票据。
- 加载账号基础信息。
- 查询封禁、白名单、版本灰度。
- 返回推荐 Gateway 或直接完成接入。

登录完成后客户端不应该每条消息都带平台 token，而是使用短期 session token。session token 必须可吊销、可过期、可绑定设备或连接。

### 5.4 Global Router

Global Router 根据目标对象路由请求。

路由类型：

- `uid` 路由：玩家私有状态、好友、邮件等。
- `room_id` 路由：房间内消息。
- `service_type` 路由：登录、匹配、排行榜等无状态或弱状态服务。
- `region` 路由：跨地域请求。

Router 可以做成轻量库嵌入各服务，也可以做成独立服务。高频链路不建议每条消息都远程查询 Router，而应使用本地路由缓存加版本号失效。

### 5.5 Matchmaking Service

匹配服务处理玩家从大厅进入房间前的组织过程。

能力：

- 单人匹配、组队匹配、邀请房间、自定义房间。
- 规则匹配：段位、延迟、地区、版本、玩法、人数、黑名单。
- 等待时间扩圈。
- 匹配取消、超时、重试。
- 与 Room Coordinator 协作创建房间。

匹配队列可按玩法和地域分片。每个分片由一个或多个 matcher actor 负责，避免大锁和跨线程共享。

### 5.6 Room Coordinator

Room Coordinator 是房间元数据的权威服务。

核心职责：

- 创建房间。
- 为房间分配 Room Server。
- 维护房间状态和成员列表。
- 发放加入房间 token。
- 处理断线重连。
- 关闭房间。
- 检测 Room Server 心跳。
- 处理 Room Server 故障后的恢复或结算。

房间状态机：

```text
creating -> waiting -> running -> settling -> closed
              |           |
              v           v
            closing     crashed
```

元数据示例：

```text
room_id
game_mode
region
owner_uid
server_id
member_uids
capacity
state
version
lease_expire_at
created_at
updated_at
```

一致性要求：

- `CreateRoom` 必须幂等。
- `JoinRoom` 必须校验 room version 和 join token。
- 房间成员变更需要串行化，防止重复进房、超员、幽灵玩家。
- Room Coordinator 与 Room Server 之间使用租约，避免双主房间。

### 5.7 Room Allocator

Room Allocator 根据当前容量选择房间运行节点。

调度输入：

- room type。
- 目标地域。
- 当前节点 CPU、内存、网络、tick 耗时。
- 房间数量、在线人数、发送队列积压。
- 版本号、玩法模块版本。
- 节点标签，例如 SSD、专用物理机、高频 CPU。

调度策略：

- 先过滤不可用节点。
- 再按地域和版本过滤。
- 对剩余节点按负载评分。
- 保留 headroom，不把节点打满。
- 使用租约锁定容量，创建失败后释放。

评分示例：

```text
score = cpu_weight * cpu_usage
      + mem_weight * mem_usage
      + net_weight * net_usage
      + tick_weight * p99_tick_cost
      + room_weight * room_count_ratio
```

### 5.8 Room Server

Room Server 是实时逻辑执行层。建议每个房间是一个 actor，同一房间内消息串行处理。

职责：

- 维护房间内玩家、实体、场景和玩法状态。
- 按固定 tick 或事件驱动方式推进逻辑。
- 处理玩家输入并生成状态变更。
- 做 AOI、广播裁剪、同步频率控制。
- 生成结算事件。
- 支持断线重连状态恢复。

线程模型：

```text
network io threads
  -> room dispatch queue
  -> room worker threads
       -> room actor
       -> game logic tick
       -> snapshot/event output
  -> async db/event threads
```

关键原则：

- 房间内尽量单线程串行，避免锁。
- 房间之间可以并行。
- 大房间可以进一步拆 actor，例如 scene actor、team actor、AOI actor。
- 玩法逻辑不直接访问数据库，所有外部写入走异步结算或事件服务。
- tick 超时要可观测，并能自动降频、降广播或踢出异常房间。

## 6. 房间生命周期

### 6.1 创建房间

流程：

```text
Client -> Gateway -> Matchmaking/Room Coordinator
Room Coordinator -> Room Allocator
Room Allocator -> Room Server ReserveRoom
Room Server -> Room Coordinator Ack
Room Coordinator -> Client room_id + join_token
```

要求：

- 客户端请求带 `client_request_id`，服务端按该 ID 幂等。
- Allocator 预占容量，Room Server 创建成功后确认。
- 如果 Room Server 创建失败，Coordinator 释放预占并重试其他节点。
- 创建完成前对外状态是 `creating`，不允许普通 join 绕过 token。

### 6.2 加入房间

流程：

```text
Client -> Gateway -> Room Coordinator JoinRoom
Room Coordinator -> validate member/capacity/state
Room Coordinator -> issue join_token
Client -> Gateway -> Room Server EnterRoom
Room Server -> Coordinator ConfirmMemberEntered
```

`join_token` 建议包含：

- `uid`
- `room_id`
- `room_version`
- `expire_at`
- `nonce`
- `signature`

Room Server 必须校验 token，不能只相信 Gateway。

### 6.3 房间运行

运行期消息路径：

```text
Client -> Gateway -> Room Server
Room Server -> Gateway -> Client
Room Server -> Event Bus -> Settlement/Analytics
```

高频战斗消息不经过 Room Coordinator。Coordinator 只处理房间元数据和控制面。

### 6.4 断线重连

断线后：

- Gateway 检测连接断开并通知 Session。
- Session 将玩家标记为 `reconnecting`。
- Room Server 暂停或托管该玩家，保留重连窗口。
- 玩家重新登录后，Session 查询原 `room_id`。
- Room Coordinator 发放 reconnect token。
- Room Server 下发快照和增量，恢复玩家控制。

### 6.5 结算与关闭

Room Server 生成结算事件：

```text
room_id
round_id
member_results
reason
started_at
ended_at
event_seq
signature
```

Settlement Service 必须按 `room_id + round_id` 幂等处理。结算成功后，Coordinator 将房间状态置为 `closed`，Room Server 释放内存。

## 7. 数据设计

### 7.1 数据分类

| 类型 | 示例 | 存储策略 |
| --- | --- | --- |
| 连接态 | conn_id、gateway_id | Gateway 内存，Session 租约 |
| 在线态 | uid、session_id、room_id | Session 分片，缓存加租约 |
| 房间元数据 | room_id、成员、server_id | Coordinator 分片，强一致或准强一致 |
| 房间实时状态 | 坐标、血量、临时实体 | Room Server 内存，必要时快照 |
| 账号资料 | 昵称、等级、背包 | DB 分库分表，缓存 |
| 结算结果 | 战绩、奖励、任务进度 | DB 事务或可靠事件 |
| 日志分析 | 行为、性能、风控 | Event Bus 到离线/实时分析 |

### 7.2 数据库

推荐组合：

- 关系型数据库：账号、资产、订单、战绩等强一致数据。
- KV/缓存：会话、热点资料、房间目录缓存。
- 消息队列：结算、日志、异步任务、跨服务事件。
- 对象存储：战斗回放、快照、日志归档。

分库分表原则：

- 玩家私有数据按 `uid` 分片。
- 房间结算按 `room_id` 或时间分片。
- 排行榜按玩法和赛季分片。
- 所有跨分片事务都要谨慎，优先使用最终一致和补偿任务。

### 7.3 缓存策略

- 写关键资产时使用 DB 作为最终权威。
- 玩家资料读多写少，可缓存并设置版本号。
- 在线状态使用短 TTL 和租约续期。
- 房间目录使用本地缓存加 Coordinator 版本校验。
- 绝不把缓存当作唯一持久化来源。

## 8. 协议设计

### 8.1 外部协议

客户端协议建议使用二进制协议，编码可以选择 Protobuf、FlatBuffers 或自研紧凑编码。早期建议 Protobuf，生态成熟、调试方便。

协议头示例：

```text
magic        uint16
version      uint16
header_len   uint16
flags        uint16
opcode       uint32
seq          uint32
session_id   uint64
uid          uint64
body_len     uint32
checksum     uint32
```

设计要求：

- 所有请求都有 `seq`，方便去重、重试和排查。
- 协议版本可灰度。
- 服务端对 `body_len` 设置硬上限。
- 高频同步包可以使用更紧凑的 delta 格式。
- 低频业务包使用统一 RPC 风格。

### 8.2 内部协议

内部服务之间分控制面和数据面：

- 控制面：服务发现、创建房间、踢人、配置更新，可用 gRPC 或自研 RPC。
- 数据面：房间消息、广播、同步，建议使用轻量二进制 RPC 或消息直连。

内部协议需要带：

- `trace_id`
- `source_service`
- `target_service`
- `deadline`
- `idempotency_key`
- `auth_context`

### 8.3 错误码

错误码按模块分段：

```text
0          OK
10000      common
20000      auth
30000      session
40000      matchmaking
50000      room
60000      settlement
90000      system
```

客户端可展示错误和服务端内部错误要分离，避免泄露系统细节。

## 9. C++ 框架设计

### 9.1 技术基线

推荐技术选择：

- Linux x86_64 为主要运行环境。
- CMake 作为构建系统。
- `server/engine` 下由项目维护的公开接口必须使用 C 或 C++11 可编译的设计，不得要求 C++14+；`base/common` 契约优先采用 C ABI。
- 上层逻辑层允许使用 C++11 及以上标准，具体可按玩法复杂度选择 C++11/C++14/C++17/C++20。
- Protobuf 作为首版 IDL。
- 网络层基于 libevent 封装，统一事件循环、连接、定时器和异步回调接口。
- 原生 C++ 日志库直接放在 `server/engine/src/log` 封装，提供简单的 C++11 接口和日志宏；不为 spdlog 机械复制一套 C API。
- 指标暴露 Prometheus 格式。
- 链路追踪兼容 OpenTelemetry。

### 9.2 基础设施层边界

engine 目录接口硬约束：

- 本节规则属于项目常驻背景，适用于以后每一次 engine 代码生成、修改和评审。
- `server/engine` 下所有由项目维护的公开头文件必须能够使用 C 编译器或 `-std=c++11` 编译，不得要求 C++14 或更高标准。
- C++11 是最高兼容标准；能用普通 C 或简单 C++03 风格表达时，不主动引入更多 C++11 特性。
- C API 优先使用普通函数、plain struct、opaque handle 和显式错误码；C++ API 只使用简单类和直接的 RAII。
- engine 公共接口原则上不得暴露 STL 类型，包括 `std::string`、标准容器、`std::function` 和智能指针；优先使用 `const char*`、指针加长度、普通结构体和显式回调。
- engine 项目自维护的实现代码也应尽量少用或不用 STL；只有局部使用明显更简单且不会泄漏到公共接口时才允许使用。
- 禁止模板元编程、可变参数模板、完美转发、复杂 SFINAE、表达式模板、复杂 lambda、复杂继承、重度运算符重载、异常驱动流程、依赖 RTTI 的设计和隐藏控制流的复杂宏。
- engine 项目自维护的实现代码应优先使用 C 或直接、易懂的 C++，以人工阅读、调试和长期维护为第一目标。
- 第三方 C++ 库强制要求高于 C++11 的标准时，只允许独立 `backends` target 的私有实现使用该标准；项目公共接口仍必须保持 C/C++11 兼容。
- 新增或修改 engine 接口后，必须按接口语言执行 C 或 C++11 编译兼容性检查。

`base/common` 中的基础设施契约包括基础数学库、网络库、普通内存池、共享内存型内存池、数据库访问接口、配置、时间、错误码和基础容器。这些公开 C 契约必须遵守统一规则：

- 对外接口使用 C 风格 API，公开头文件使用 `.h`。
- 公开头文件必须能被 C 编译器和 C++03 编译器包含。
- 不在公开接口中暴露 STL、模板、异常、RTTI、lambda、`auto`、`std::thread`、`std::chrono`、智能指针等 C++11 及以上能力。
- 不在公开接口中暴露第三方库类型，例如 `event_base`、`spdlog::logger`、数据库驱动连接对象等。
- 使用 opaque handle 隐藏实现细节，例如 `abe_net_loop_t*`、`abe_pool_t*`、`abe_db_t*`。
- 函数返回统一错误码，复杂结果通过输出参数返回。
- 所有资源必须提供成对的 `create/destroy` 或 `init/shutdown` 接口。
- 所有异步回调必须携带 `void* user_data`。
- 所有内存所有权必须写在接口注释里，明确由调用方释放还是模块内部管理。
- `base/common` 不得反向依赖 `backends`、`adapters` 或逻辑层。

推荐接口风格：

```c
typedef struct abe_pool abe_pool_t;

typedef struct abe_pool_config {
    unsigned int block_size;
    unsigned int block_count;
    const char* name;
} abe_pool_config_t;

int abe_pool_create(const abe_pool_config_t* config, abe_pool_t** out_pool);
void* abe_pool_alloc(abe_pool_t* pool, unsigned int size);
void abe_pool_free(abe_pool_t* pool, void* ptr);
void abe_pool_destroy(abe_pool_t* pool);
```

模块约束：

- 基础数学库：使用 C 实现，提供向量、矩阵、随机数、定点数、几何检测等常用能力；接口避免 C++ 重载和模板。
- 网络库：基于 libevent 实现，封装连接、监听器、定时器、DNS、信号和线程唤醒；业务层不直接操作 libevent 原生对象。
- 普通内存池：用于高频临时分配、网络 buffer、协议解析和房间 tick 临时对象。
- 共享内存型内存池：用于跨进程共享状态、热重启恢复、同机多进程通信；所有结构必须版本化并避免裸进程内指针。
- 时间接口：在 `engine/src/base/time` 提供 C 风格的 `real` 真实时间和 `mono` 单调时间接口，两组都提供毫秒和秒；真实时间使用 Unix 时间且做进程内不倒退保护，单调时间只用于定时器和耗时计算。时间轮运行时优先使用单调时间推进；UTC 指定时刻只作为调度输入，在调用时转换为单调时间目标。时间模块还提供系统时区查询、日期拆解和毫秒时间差。
- 日志库：原生 C++ spdlog 由 `engine/src/log` 直接封装，对外提供简单的 C++11 函数和 `ABE_LOG_*` 宏；公共头不暴露 spdlog 类型和 STL。日志当前时间统一来自 `base/time` 的 `real` 时间接口，文件日志按 `root/YYYY-MM-DD/logger.log` 组织，目录和时间戳使用同一个固定 UTC 偏移。
- 数据库访问接口：在 `common/db` 提供 C 风格连接、查询、事务、结果集和异步回调接口；具体 MySQL/PostgreSQL/Redis 驱动放在 `backends`，C++ RAII 包装放在 `adapters`。

统一 C/C++ 适配规则：

1. `base/common` 定义稳定 C 接口，不暴露第三方类型，也不负责提供 C++ 便利封装。
2. `backends` 负责接入原始第三方接口和实现 `base/common` 契约，例如使用 MySQL C API 实现数据库接口。
3. `adapters` 只负责把已有 C 接口转换为不高于 C++11 的简单 RAII 和类接口，供 `services` 使用；公共接口不暴露 STL。
4. 第三方库本身是 C++ 且只有 C++ 模块使用时，C++ 模块可以使用其原生接口，不额外复制一套 C 封装。
5. 当 C 基础设施确实需要一个 C++ 库作为实现时，在 `backends` 中提供范围最小的 C ABI 桥接，并保持实现可替换。

关于 spdlog 的编译边界：

- spdlog 本身是 C++ 日志库，直接由 `engine/src/log` 的 C++11 实现文件封装。
- `engine/src/log/abe_log.h` 只提供项目自己的简单函数、枚举和 `ABE_LOG_*` 宏，不暴露 `spdlog::logger`、标准容器或智能指针。
- C++ 业务层直接使用 `engine/src/log`，不再经过 `backends` 或 C ABI 桥接。
- 只有未来 C 模块确实需要调用日志时，才额外增加最小 C API；不能因为 spdlog 是底层库就提前增加一整套 C 封装。

### 9.3 模块划分

建议仓库结构：

```text
server/
  engine/
    src/
      base/
        math/
        net/
        memory/
        shm/
        time/
        metrics/
        config/
        threading/
      log/
      common/
        protocol/
        rpc/
        id/
        error/
        service_discovery/
        serialization/
        db/
      backends/
        db_mysql/
        redis/
        rabbitmq/
        kafka/
      adapters/
        db_cpp/
  services/
    common/
      session/
      store/
    gateway/
    lobby/
    coordinator/
    match/
    session/
    settlement/
    game/
  proto/
    client/
    internal/
deploy/
  docker/
  k8s/
  systemd/
test/
  unit/
  integration/
  load/
doc/
```

目录边界：

- `server/engine/src/base`：最底层基础设施，优先 C 实现，公开头文件保持 C ABI 和 C++03 兼容。
- `server/engine/src/common`：协议、错误码、ID、RPC 抽象、服务发现、数据库抽象等稳定共享 C 接口。
- `server/engine/src/log`：原生 C++ 日志库封装，提供不高于 C++11 的简单接口和 `ABE_LOG_*` 宏；可依赖 `base/time` 计算时间戳和日期目录，公共接口不暴露 spdlog 或 STL。
- `server/engine/src/backends`：第三方原始接口和具体实现，例如 MySQL、Redis、RabbitMQ、Kafka；可依赖 `base/common`，不向其公共头暴露第三方类型。
- `server/engine/src/adapters`：将 `base/common` C 接口适配为不高于 C++11 的简单 RAII 和类接口；不承载具体后端，公共接口不暴露 STL，也不得要求 C++14+。
- 独立逻辑层已移除：公共服务组件放入 `services/common`，具体业务编排放入对应服务模块。
- `server/services`：可独立启动的服务进程入口和组装层，例如 Gateway、Lobby、Coordinator、Match、Session、Settlement；负责选择并链接具体后端。
- `server/share/proto`：协议定义源文件。`client` 面向客户端协议，`internal` 面向服务间协议；生成代码后续可按语言和构建系统单独放入生成目录。

当前仓库中的 MySQL、Redis、RabbitMQ、Kafka 具体后端已按新规则放入 `engine/src/backends`；spdlog 已按新规则迁入 `engine/src/log`。后续新增具体后端继续放入 `backends`，只向外暴露项目自己的 C 接口。

基础设施层建议以独立静态库或共享库组织：

```text
abe_base_math      C / C++03-compatible headers
abe_base_net       C API, libevent backend
abe_base_memory    C API, normal memory pool
abe_base_shm       C API, shared memory pool
abe_engine_log          C++11 wrapper and macros over spdlog
abe_common_db           C API, database contract
abe_backend_db_mysql    C backend using MySQL C API
abe_adapter_db_cpp      C++11-or-earlier simple RAII wrapper over abe_common_db
abe_service_session     C++11 service component
```

### 9.4 进程模型

每个服务进程包含通用运行时：

```text
Main
  -> ConfigManager
  -> Logger
  -> MetricsRegistry
  -> ServiceDiscovery
  -> RpcServer
  -> RpcClientPool
  -> WorkerRuntime
  -> GracefulShutdown
```

通用能力：

- 热加载配置。
- 优雅停机，停止接新请求，等待房间自然结束或迁出。
- 健康检查和就绪检查。
- 运行时指标。
- 统一错误码和日志字段。
- 内存水位保护。

### 9.5 并发模型

推荐组合：

- 网络 IO 使用 libevent event loop。
- 服务内部使用 actor 或 mailbox。
- 房间逻辑单 actor 串行。
- 跨 actor 通过消息传递，不共享可变状态。
- 慢操作进入异步任务池，例如 DB、HTTP、对象存储。

房间 actor 示例：

```text
RoomActor
  mailbox<ClientInput>
  mailbox<SystemEvent>
  tick_timer
  state
  logic_module
```

优点：

- 避免房间内复杂加锁。
- 方便回放和调试。
- 可以按 room_id 稳定分配 worker。
- 故障影响范围清晰。

### 9.6 内存管理

百万在线对内存和分配器非常敏感。

建议：

- 网络包使用 buffer pool。
- Protobuf 解析使用 arena。
- 房间临时对象使用帧级 arena，tick 结束批量释放。
- 对高频对象使用对象池。
- 需要跨进程保留或共享的数据使用共享内存型内存池，并为结构体增加 magic、version、size 和 checksum。
- 限制每连接、每房间、每服务的队列长度。
- 周期输出内存统计，按模块拆分。

禁止：

- 高频路径无控制地 `new/delete`。
- 广播时为每个玩家复制完整包体。
- 对慢客户端无限堆积发送队列。

### 9.7 玩法逻辑接入

框架层需要为玩法提供稳定接口：

```cpp
class IRoomLogic {
public:
    virtual ~IRoomLogic() = default;
    virtual void OnCreate(const RoomCreateContext& ctx) = 0;
    virtual void OnJoin(const PlayerContext& player) = 0;
    virtual void OnLeave(uint64_t uid, LeaveReason reason) = 0;
    virtual void OnInput(const PlayerInput& input) = 0;
    virtual void OnTick(uint64_t now_ms) = 0;
    virtual RoomResult OnClose(CloseReason reason) = 0;
};
```

玩法模块可以通过静态链接、动态库或脚本运行时接入。第一版建议静态链接或动态库，减少脚本沙箱和热更新复杂度。

## 10. 服务发现与配置

服务实例启动后注册：

```text
service_name
service_id
region
zone
ip
port
version
weight
capacity
metadata
heartbeat_at
```

配置分层：

- 静态配置：端口、线程数、基础路径。
- 动态配置：限流阈值、匹配规则、广播频率、开关。
- 灰度配置：按 uid、地域、版本、渠道生效。

配置更新必须有版本号，服务收到新配置后先校验再应用。错误配置要能自动回滚到上一版本。

## 11. 高可用与故障处理

### 11.1 故障场景

| 故障 | 影响 | 处理策略 |
| --- | --- | --- |
| Gateway 挂掉 | 连接断开 | 客户端重连，Session 保留短期状态 |
| Session 分片挂掉 | 在线态查询受影响 | 主从切换或租约重建 |
| Matchmaking 挂掉 | 新匹配受影响 | 队列分片重启，票据幂等恢复 |
| Room Coordinator 主挂掉 | 房间控制面受影响 | 分片 leader 切换，Room Server 租约续期 |
| Room Server 挂掉 | 该节点房间受影响 | 断线重连、快照恢复、异常结算 |
| DB 主库挂掉 | 持久化受影响 | 主从切换，写入降级和队列缓冲 |
| 消息队列挂掉 | 异步事件积压 | 本地 WAL 缓冲，恢复后补发 |

### 11.2 Room Server 故障策略

不同玩法使用不同恢复级别：

- 轻量休闲房间：Room Server 故障后直接异常结算或退款。
- 中等实时房间：周期快照，故障后新节点恢复到最近快照。
- 长时长房间：事件日志加快照，重放恢复。

第一版建议实现：

- 房间基础元数据可靠保存。
- 关键结算事件可靠保存。
- 房间快照作为可选能力。
- 不做实时无感迁移。

### 11.3 优雅停服

Room Server 下线流程：

```text
mark draining
stop accepting new rooms
notify Coordinator
wait active rooms finish
force close timeout rooms if maintenance deadline reached
flush settlement events
unregister service
exit
```

这可以支持滚动更新和灰度发布。

## 12. 安全与风控

基础安全：

- 全链路 TLS 或公网 TLS 加内部 mTLS。
- 登录票据短期有效。
- 请求签名和重放保护。
- IP、设备、账号多级限流。
- 协议长度、字段范围、枚举值全校验。
- 服务间鉴权，禁止任意服务调用高权限接口。

反作弊基础：

- 服务端权威状态。
- 客户端输入只作为意图，不直接相信结果。
- 速度、频率、伤害、资源变化校验。
- 关键战斗事件可回放。
- 异常行为进入风控事件流。

DDoS 与洪泛：

- L4 防护和黑洞策略。
- Gateway accept 限速。
- 未认证连接低资源占用。
- 登录失败指数退避。
- 单连接发送队列硬上限。

## 13. 可观测性

必须从第一天建设指标，否则百万在线问题无法定位。

指标：

- 在线人数、连接数、登录成功率。
- 每服务 QPS、错误率、P95/P99 延迟。
- Gateway 收发包量、发送队列长度、断线原因。
- Room Server 房间数、玩家数、tick 耗时、广播大小。
- Coordinator 创建房间成功率、分配失败率、租约超时。
- Matchmaking 队列长度、平均等待、取消率。
- DB/Cache/MQ 延迟、错误、积压。
- CPU、内存、网络、文件描述符、上下文切换。

日志字段：

```text
timestamp
level
service
service_id
region
trace_id
uid
room_id
opcode
error_code
latency_ms
message
```

追踪：

- 登录链路。
- 创建房间链路。
- 加入房间链路。
- 结算链路。
- 跨服务错误链路。

告警：

- 登录成功率突降。
- 创建房间失败率升高。
- Gateway 连接异常断开升高。
- Room tick P99 超阈值。
- 消息队列积压持续增长。
- DB 主从延迟过高。

## 14. 容量规划

### 14.1 Gateway 估算

若单 Gateway 稳定承载 30,000 连接，100w 在线需要：

```text
1,000,000 / 30,000 = 34 台
考虑 30% headroom 和故障冗余，建议 50 台起步
```

如果使用更高规格机器和充分调优，单实例可能达到更高连接数。但设计时不应该依赖极限值。

Gateway 关键瓶颈：

- 文件描述符。
- 内核网络参数。
- TLS 开销。
- 出口带宽。
- 慢连接发送队列。
- 日志量。

### 14.2 Room Server 估算

房间服取决于玩法。示例：

```text
单房间 10 人，100w 在线约 100,000 个房间
单 Room Server 承载 1,000 人，约需 1,000 台实例
考虑 headroom，约 1,300 台实例
```

如果是 100 人大房间：

```text
100w 在线约 10,000 个房间
单 Room Server 承载 2,000 人，约需 500 台实例
考虑 headroom，约 650 台实例
```

因此房间服容量必须通过真实压测决定，不能只看连接数。

### 14.3 消息量估算

假设每人上行 5 条/秒：

```text
1,000,000 * 5 = 5,000,000 msg/s
```

假设每人下行 15 条/秒：

```text
1,000,000 * 15 = 15,000,000 msg/s
```

如果每条下行平均 120 bytes，纯业务负载约：

```text
15,000,000 * 120 = 1.8 GB/s
约 14.4 Gbps，不含协议头、重传、TLS、峰值
```

实际网络预算要按 2 到 3 倍准备。

## 15. 性能优化原则

网络：

- 减少小包，合理合包。
- 广播包体复用，按接收者差异补丁。
- 优先 delta 同步。
- 避免跨线程频繁拷贝大包。

逻辑：

- 房间 tick 分层，低重要性对象降频。
- AOI 裁剪，避免全房间无脑广播。
- 大房间拆区域。
- 慢逻辑从 tick 中剥离。

系统：

- CPU 亲和性和 NUMA 感知。
- 预分配内存池。
- 减少锁竞争。
- 日志异步批量。
- 使用 perf、flamegraph、heap profiler 建立优化闭环。

## 16. 部署架构

推荐早期使用 Kubernetes 管理无状态和弱状态服务，Room Server 根据性能要求可以先在 Kubernetes 中运行，后续高负载玩法可迁移到专用物理机或混合调度。

部署分层：

- Edge：LB、Gateway。
- Control Plane：Auth、Session、Router、Coordinator、Allocator、Matchmaking。
- Data Plane：Room Server。
- Data：DB、Cache、MQ、对象存储。
- Ops：监控、日志、追踪、配置中心、发布系统。

Room Server 发布策略：

- 新版本节点注册为 `version=N+1`。
- Allocator 只给新房间分配新版本。
- 老版本节点 drain，等待旧房间结束。
- 长房间达到维护窗口后强制关闭或迁移。

## 17. 压测与验证

压测必须分阶段，不要直接冲百万。

阶段：

| 阶段 | 目标 | 验证点 |
| --- | --- | --- |
| P0 | 单机 1,000 连接 | 协议、心跳、基础稳定性 |
| P1 | 单服务 10,000 连接 | Gateway 内存、CPU、发送队列 |
| P2 | 小集群 100,000 在线 | 服务发现、路由、房间创建 |
| P3 | 300,000 在线 | Coordinator 分片、MQ、DB 压力 |
| P4 | 1,000,000 在线 | 全链路容量、故障演练、告警 |

压测工具：

- C++ 或 Go 编写机器人客户端。
- 支持真实协议、登录、匹配、进房、发送输入、断线重连。
- 支持行为模型，例如静默玩家、活跃玩家、高频玩家。
- 支持多机分布式压测和统一控制。

验收指标：

- 登录成功率。
- 匹配成功率。
- 创建房间成功率。
- 房间 tick P95/P99。
- 消息端到端延迟。
- CPU、内存、网络水位。
- 故障恢复时间。
- 数据丢失和重复结算数量。

## 18. 迭代路线

### M0：框架骨架

- CMake 工程。
- 日志、配置、指标。
- Protobuf 协议生成。
- 基础 TCP Gateway。
- 简单 RPC 框架。
- 服务注册和健康检查。

### M1：单房间闭环

- 登录 mock。
- 创建房间。
- 加入房间。
- RoomActor tick。
- 客户端消息转发。
- 房间关闭和结算 mock。
- 单机压测 1,000 到 10,000 连接。

### M2：集群化

- Gateway 集群。
- Session Service。
- Room Coordinator。
- Room Allocator。
- Room Server 注册和心跳。
- 房间元数据持久化。
- 分布式压测 100,000 在线。

### M3：高可用

- Coordinator 分片。
- 租约和故障检测。
- Gateway 断线重连。
- Room Server drain。
- 结算幂等。
- MQ 事件可靠投递。
- 故障演练。

### M4：百万在线

- 全链路压测。
- 热点消除。
- 内存池和零拷贝优化。
- AOI 和广播裁剪。
- 多地域路由。
- 完整监控告警。
- 灰度发布体系。

## 19. 首版接口草案

### 19.1 创建房间

```proto
message CreateRoomRequest {
  uint64 uid = 1;
  string client_request_id = 2;
  uint32 game_mode = 3;
  uint32 capacity = 4;
  string region_hint = 5;
  repeated uint64 invited_uids = 6;
}

message CreateRoomResponse {
  uint32 error_code = 1;
  uint64 room_id = 2;
  string join_token = 3;
  string room_endpoint = 4;
}
```

### 19.2 加入房间

```proto
message JoinRoomRequest {
  uint64 uid = 1;
  uint64 room_id = 2;
  string join_token = 3;
  uint64 known_room_version = 4;
}

message JoinRoomResponse {
  uint32 error_code = 1;
  uint64 room_id = 2;
  uint64 room_version = 3;
  bytes snapshot = 4;
}
```

### 19.3 房间输入

```proto
message RoomInput {
  uint64 uid = 1;
  uint64 room_id = 2;
  uint32 seq = 3;
  uint64 client_time_ms = 4;
  bytes payload = 5;
}
```

### 19.4 房间广播

```proto
message RoomBroadcast {
  uint64 room_id = 1;
  uint64 server_time_ms = 2;
  uint32 tick = 3;
  bytes payload = 4;
}
```

## 20. 主要风险

| 风险 | 表现 | 缓解 |
| --- | --- | --- |
| 低估广播量 | 出口带宽爆炸、延迟升高 | AOI、delta、降频、合包 |
| Coordinator 热点 | 创建房间失败、join 延迟 | 按 room_id 分片、本地缓存、幂等重试 |
| 慢客户端堆积 | Gateway 内存持续上涨 | 发送队列上限、丢弃低优先级包、断开 |
| 结算重复 | 奖励重复发放 | 幂等键、事务、唯一索引 |
| 房间服故障 | 玩家对局丢失 | 快照、事件日志、异常结算 |
| 跨地域延迟 | 玩家体验差 | 地域优先匹配、房间主地域固定 |
| 过早复杂化 | 开发周期失控 | 先打通 M1/M2，再做高级恢复 |

## 21. 建议下一步

优先实现最小可运行链路：

```text
ClientBot -> Gateway -> Room Coordinator -> Room Server -> Gateway -> ClientBot
```

最小链路只需要：

- 一个二进制协议。
- 一个 Gateway。
- 一个 Coordinator。
- 一个 Room Server。
- 一个机器人压测客户端。
- 一个 Prometheus 指标出口。

当单机闭环跑通后，再引入 Session、Allocator、服务发现、分布式压测和数据持久化。这样可以尽早发现真正的性能瓶颈，避免在没有压测数据时过度设计。

## 22. 开发环境与 Docker

为了让工程环境可以直接落地，建议把依赖统一放进 Docker 开发镜像和 compose 编排中。

镜像内建议包含：

- `spdlog`
- `libevent`
- `libjuice`
- `protobuf` / `protoc` / `grpc_cpp_plugin`
- `mysql` 客户端开发库
- `redis` 客户端开发库
- `rabbitmq` C 客户端开发库
- `kafka` C 客户端开发库
- `grpc`

推荐文件：

- `deploy/docker/Dockerfile`：统一安装编译工具和 C/C++ 依赖，并默认从源码构建 `libjuice`。
- `deploy/docker/docker-compose.yml`：启动 `mysql`、`redis`、`rabbitmq`、`zookeeper`、`kafka` 和开发容器。
- `deploy/docker/.env.example`：保存默认环境变量模板。
- `.dockerignore`：排除构建产物和本地缓存。

开发容器应只负责“编译、联调、起服务”，不要把业务逻辑塞进镜像脚本里。业务启动命令留给后续的可执行文件或服务启动脚本。

## 23. Docker 安装

以下命令适用于 Ubuntu 22.04/24.04，按 Docker 官方仓库方式安装 Docker Engine 和 Compose 插件。

```bash
sudo apt-get remove -y docker.io docker-doc docker-compose docker-compose-v2 podman-docker containerd runc

sudo rm -f /etc/apt/sources.list.d/docker.sources

sudo apt-get update
sudo apt-get install -y ca-certificates curl

sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc

sudo tee /etc/apt/sources.list.d/docker.sources > /dev/null <<EOF
Types: deb
URIs: https://download.docker.com/linux/ubuntu
Suites: $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}")
Components: stable
Architectures: $(dpkg --print-architecture)
Signed-By: /etc/apt/keyrings/docker.asc
EOF

sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

sudo systemctl enable --now docker
sudo docker run hello-world
```

可选地，把当前用户永久加入 `docker` 组，后续就不用每次加 `sudo`：

```bash
deploy/docker/dev.sh access
```

这一步会写入系统用户组配置，重启后仍然有效，不需要每次开机重复执行。注销并重新登录一次后，
新终端会自动获得 `docker` 组权限。如果希望当前终端立刻生效，可以临时执行：

```bash
newgrp docker
docker compose version
```

如果普通用户执行 `docker ps` 或 `docker compose` 时出现
`permission denied while trying to connect to the docker API at unix:///var/run/docker.sock`，
先确认 `/var/run/docker.sock` 属于 `root:docker`，再把当前用户加入 `docker` 组：

```bash
ls -l /var/run/docker.sock
id
getent group docker
deploy/docker/dev.sh access
```

注销并重新登录一次后再验证：

```bash
docker ps
```

如果不想重登，可以在当前终端临时执行 `newgrp docker` 后再运行 `docker ps`。`newgrp` 只影响当前 shell，
不是开机后必须重复执行的步骤。`docker` 组拥有接近 root 的权限，只建议给可信的本机开发账号使用。

## 24. Docker 环境配置

本仓库的本地开发环境位于 `deploy/docker/`，首次使用时复制环境变量模板：

```bash
cd deploy/docker
cp .env.example .env
docker compose config
```

默认依赖服务：

| 服务 | 默认地址 | 说明 |
| --- | --- | --- |
| MySQL | `127.0.0.1:3306` | database: `abe_engine`, user: `abe`, password: `abe123` |
| Redis | `127.0.0.1:6379` | 本地缓存，开启 AOF |
| RabbitMQ | `127.0.0.1:5672` | 默认用户 `abe` |
| RabbitMQ Management | `127.0.0.1:15672` | 本地管理页面 |
| ZooKeeper | `127.0.0.1:2181` | Kafka 本地依赖 |
| Kafka | `127.0.0.1:9092` | 单 broker，本地事件流 |

依赖服务镜像默认使用可直接访问的镜像前缀，避免 Docker Hub 超时：

| 变量 | 默认镜像 |
| --- | --- |
| `MYSQL_IMAGE` | `m.daocloud.io/docker.io/mysql:8.0` |
| `REDIS_IMAGE` | `m.daocloud.io/docker.io/redis:7-alpine` |
| `RABBITMQ_IMAGE` | `m.daocloud.io/docker.io/rabbitmq:3-management` |
| `ZOOKEEPER_IMAGE` | `m.daocloud.io/docker.io/zookeeper:3.9` |
| `KAFKA_IMAGE` | `m.daocloud.io/docker.io/apache/kafka:3.7.0` |

如果已经配置 Docker daemon 的阿里云 mirror，可以在 `.env` 中把这些变量改回 `mysql:8.0`、
`redis:7-alpine` 等原始 Docker Hub 名称。

当前 compose 使用 `network_mode: host`，所有服务直接占用宿主机端口。启动前应确认端口未被宿主机上的
其他进程占用：

```bash
ss -ltnp | grep -E ':(3306|6379|5672|15672|2181|9092)\b' || true
```

启动开发环境：

```bash
docker compose up -d --build
docker compose ps
docker compose exec dev bash
```

`deploy/docker/` 提供统一操作脚本：

| 命令 | 用途 |
| --- | --- |
| `dev.sh start` | 构建有变化的镜像并启动环境。 |
| `dev.sh stop` | 停止并移除容器，保留数据卷。 |
| `dev.sh restart` | 重启整套环境，并构建有变化的镜像。 |
| `dev.sh build` | 构建镜像。 |
| `dev.sh rebuild` | 无缓存重新构建镜像并启动环境。 |
| `dev.sh status` | 查看服务状态。 |
| `dev.sh logs` | 跟踪日志，可追加服务名。 |
| `dev.sh enter` | 进入 `dev` 开发容器。 |
| `dev.sh clean` | 停止环境并删除数据卷。 |
| `dev.sh config` | 检查 compose 配置。 |
| `dev.sh portable` | 构建可迁移镜像并导出 tar 包。 |
| `dev.sh access` | 将当前登录用户永久加入 `docker` 组。 |
| `dev.sh mirror-aliyun` | 配置 Docker daemon 使用阿里云镜像加速。 |
| `dev.sh mirror-show` | 查看当前 Docker 镜像加速地址。 |
| `build.sh` | 在 dev 容器内执行纯代码编译脚本。 |
| `rebuild.sh` | 在 dev 容器内清理并重新编译代码。 |

`deploy/docker/` 只负责 Docker/Compose 环境和容器包装命令。进入 `dev` 容器后，使用仓库根目录下的
`scripts/services_start.sh` 和 `scripts/services_stop.sh` 管理业务服务进程。服务启停脚本不编译代码，
当前支持 `gateway`：

```bash
scripts/services_start.sh gateway
scripts/services_stop.sh gateway
```

gateway 默认运行文件放在项目 `bin` 目录：pid 文件为 `bin/run/gateway.pid`，stdout/stderr 为
`bin/logs/gateway/stdout.log`，业务日志为 `bin/logs/gateway/YYYY-MM-DD/gateway.log`。
停服脚本只删除 pid 文件，不删除日志；gateway 默认写按天业务日志，不打印到启服终端。需要实时查看时，
直接执行 `tail -F bin/logs/gateway/stdout.log bin/logs/gateway/$(date +%F)/gateway.log`。

`dev` 容器内源码挂载到 `/workspace`，并预设以下连接配置：

| 环境变量 | 默认值 |
| --- | --- |
| `ABE_MYSQL_HOST` | `127.0.0.1` |
| `ABE_MYSQL_PORT` | `3306` |
| `ABE_MYSQL_DATABASE` | `${MYSQL_DATABASE:-abe_engine}` |
| `ABE_MYSQL_USER` | `${MYSQL_USER:-abe}` |
| `ABE_MYSQL_PASSWORD` | `${MYSQL_PASSWORD:-abe123}` |
| `ABE_REDIS_HOST` | `127.0.0.1` |
| `ABE_REDIS_PORT` | `6379` |
| `ABE_RABBITMQ_HOST` | `127.0.0.1` |
| `ABE_RABBITMQ_PORT` | `5672` |
| `ABE_KAFKA_BROKERS` | `127.0.0.1:9092` |
| `ABE_GRPC_PORT` | `50051` |
| `ABE_LOG_LEVEL` | `info` |

停止环境：

```bash
docker compose down
```

清空本地数据卷：

```bash
docker compose down -v
```

如果需要修改默认端口，必须同时调整服务监听参数和 `dev` 容器里的 `ABE_*` 连接配置；仅修改旧的端口映射
变量不会改变 host 网络下的监听端口。

### 24.1 代理与 IP 修改

如果本机需要走代理，可以在 `deploy/docker/.env` 里取消注释代理变量，或在执行前导出环境变量。
先把代理 IP 和端口换成你自己的值：

```bash
export HTTP_PROXY=http://<proxy-ip>:<proxy-port>
export HTTPS_PROXY=http://<proxy-ip>:<proxy-port>
export NO_PROXY=localhost,127.0.0.1,::1
docker compose up -d --build
```

当前 compose 会把这组变量作为 `dev` 镜像构建参数，也会传入 `dev` 容器运行环境。
镜像拉取由 Docker daemon 执行，如果 `docker pull` 或基础镜像拉取仍然失败，请单独按 Docker 官方 daemon
代理配置处理。

如果后端服务的地址不是本机，而是改成了其他 IP，那么还需要同步修改：

- `ABE_MYSQL_HOST`
- `ABE_REDIS_HOST`
- `ABE_RABBITMQ_HOST`
- `ABE_KAFKA_BROKERS`

如果 Docker Hub 拉取镜像超时，可以通过统一脚本把 Docker daemon 的 `registry-mirrors` 切到阿里云镜像加速地址。
阿里云加速器地址是账号专属地址，需要在阿里云容器镜像服务控制台复制完整 URL：

```bash
deploy/docker/dev.sh mirror-aliyun "<从阿里云控制台复制的完整加速地址>"
deploy/docker/dev.sh mirror-show
```

也可以在 `deploy/docker/.env` 中配置：

```bash
ALIYUN_MIRROR_URL=<从阿里云控制台复制的完整加速地址>
deploy/docker/dev.sh mirror-aliyun
```

该命令会备份 `/etc/docker/daemon.json`，更新 `registry-mirrors`，然后重启 Docker daemon。
如果当前用户不在 `docker` 组，脚本会提示输入 `sudo` 密码。

### 24.2 文件权限约定

`dev` 容器会通过 bind mount 挂载源码目录，所以 `/workspace` 内文件的可执行位最终取决于宿主机。
为避免不同机器之间权限表现不一致，仓库内普通文本文件统一使用 `0644`，只有真正需要执行的脚本才使用 `0755`。

如果后续要把脚本复制进镜像，必须在 Dockerfile 里显式写出权限，例如：

```dockerfile
COPY --chmod=0755 deploy/docker/entrypoint.sh /usr/local/bin/entrypoint.sh
```

不要依赖宿主机目录的默认权限，也不要假定共享文件系统会稳定保留执行位。

需要把带源码的容器镜像拷贝到其他机器时，使用 Dockerfile 的 `portable` 目标：

```bash
# 在仓库根目录执行
deploy/docker/dev.sh portable

# 在目标机器上执行
docker load -i abe-engine-portable.tar
docker run --rm -it --network host abe-engine-portable:latest bash
```

`portable` 目标通过 `COPY --chmod=u=rwX,go=rX` 固定镜像内权限：普通文件为 `0644`，目录为 `0755`，
已经有执行位的脚本继续保留执行权限。
