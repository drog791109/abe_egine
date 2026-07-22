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
      common/     稳定的共享 C 接口与跨服务基础能力，例如错误码、ID、RPC、服务发现、DB 抽象
      log/        原生 C++ 日志封装，提供简洁的 C++11 接口和日志宏
      backends/   common/base 接口的具体实现，隔离 MySQL C API 等原始第三方接口
      adapters/   将 base/common 的 C 接口适配为不高于 C++11 的简单 RAII 和类接口
  logic/          游戏业务逻辑和玩法模块，可使用 C++11+
  services/       可独立启动的服务进程入口和组装层
  proto/          协议定义源文件，分为 client 和 internal
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
- `proto/client` 放客户端协议，`proto/internal` 放服务间协议。

数据库模块的目标结构示例：

```text
engine/src/common/db/          稳定的数据库 C 接口
engine/src/backends/db_mysql/  基于 MySQL C API 的具体实现
engine/src/adapters/db_cpp/    基于 abe_db_t 的 C++11 及以下简单 RAII/类接口
```

网络模块的 C++ 适配位于 `engine/src/adapters/net`，只基于 `base/net` 的 C 接口提供
`Loop`、`TcpListener`、`TcpLink` 和 `UdpLink` 这类简单 RAII 封装，不直接包含 libevent 等具体后端接口。

## 当前迁移说明

- 原顶层 `server/gate`、`server/lobby`、`server/game` 已统一归入 `server/services/`，避免服务进程散落在源码根目录。
- 原 `server/engine/src/module` 曾统一改名为 `adapters`；现在进一步拆分为 `backends` 和 `adapters`，避免“第三方后端实现”和“C 到 C++ 适配”共用同一个概念。
- 当前 `engine/src/adapters/db_mysql` 等具体后端目录属于迁移前路径，后续代码调整时移入 `engine/src/backends`；spdlog 已按新规则位于 `engine/src/log`。
- 后续新增的 C++ 包装只放入 `engine/src/adapters`，编译标准不高于 C++11，且不在公共接口中暴露 STL，例如基于 `abe_db_t` 的数据库 RAII 接口。
- `server/proto` 保持在 engine 外部，避免游戏协议定义被误认为引擎基础设施的一部分。

## 日志约定

`engine/src/log` 提供 spdlog 的简洁 C++11 封装。按天写文件时：

```text
<root_directory>/YYYY-MM-DD/<logger_name>.log
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
