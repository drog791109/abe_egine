---
name: abe-engine-server-rules
description: ABE Engine game server engineering rules for simple C/C++11-or-earlier engine interfaces, minimal STL use, C-first infrastructure, isolated backends, and modern C++ logic. Use when Codex implements, reviews, refactors, or designs code/docs for this repository's engine, base math library, libevent networking layer, memory pools, logging, database access, service runtime, room server, gateway, coordinator, or upper gameplay logic.
---

# ABE Engine Server Rules

## Core Boundary

Treat the project as a layered game server framework:

- Keep base infrastructure C-first with C ABI.
- Keep every project-owned public interface under `server/engine` compatible with C or C++11; never require C++14+.
- Treat C++11 as a compatibility ceiling. Prefer C or straightforward C++03-style code when it is clear enough.
- Keep engine adapters and project-owned engine implementation code simple and minimize STL use.
- Allow C++11 and newer in upper logic/services. A private backend implementation may use a newer standard only when a third-party dependency requires it.
- Do not create a C mirror of a native C++ API only for naming or layering uniformity.
- Prefer simple, readable, maintainable APIs over clever abstractions.

## Layer Rules

Apply these boundaries unless the user explicitly changes the project standard:

| Layer | Examples | Language rule |
| --- | --- | --- |
| `engine/src/base` | math, libevent net, normal memory pool, shared memory pool, time, config, metrics | C implementation preferred; public `.h` compatible with C and C++03 |
| `engine/src/common` | protocol helpers, IDs, errors, DB access interface, service discovery wrappers | C ABI for shared infrastructure interfaces; C++03-compatible public headers |
| `engine/src/log` | Simple C++11 logging wrapper and `ABE_LOG_*` macros over spdlog | C++11-or-earlier public interface; no spdlog or STL types exposed |
| `engine/src/backends` | MySQL C driver, Redis/RabbitMQ/Kafka implementation | Public project interfaces compile with C/C++11 and expose no third-party/STL types; private implementation may use the dependency's required standard |
| `engine/src/adapters` | DB/log/network C API to C++ RAII and class wrappers | C++11-or-earlier only; simple interfaces, no STL exposure, and no dependency on concrete backends |
| `logic` | room logic, match, session, settlement, gameplay systems | C++11+ allowed; may depend on base/common/log/adapters |
| `services` | process entry points, configuration, dependency assembly, lifecycle | Selects and links concrete backends |

Never let `base/common` depend on adapters, logic, or services. Keep dependency direction one-way:

```text
base -> common -> adapters -> logic -> services
          \----> backends ------------> services
base -> log --------------------------> logic -----> services
```

`backends` and `adapters` have different meanings:

- Put raw C third-party integrations and concrete implementations of C contracts in `backends`.
- Put only C API to simple C++11-or-earlier RAII/class wrappers in `adapters`.
- If a native C++ dependency has only C++ consumers, wrap it directly in the owning C++ module instead of manufacturing a C wrapper.
- Put the project logging wrapper in `engine/src/log`; use a C ABI bridge only if C modules actually need the native C++ logger.
- If C infrastructure truly requires a native C++ implementation, add only the minimum C ABI bridge in `backends`.

## Mandatory Engine Compatibility

Treat these as hard requirements for every generation, modification, and review:

- Every project-owned public header under `server/engine` must compile as C or with `-std=c++11`; do not require C++14+.
- C++11 is the maximum compatibility level, not a feature-use target. Prefer C and straightforward C++03-style code.
- Prefer plain C functions, plain structs, opaque handles, explicit error codes, and obvious create/destroy pairs.
- Use C++ only where it improves ownership or readability, and keep engine C++ interfaces direct and easy to inspect.
- Do not expose STL types in engine public interfaces, including `std::string`, containers, `std::function`, or smart pointers.
- Minimize STL use in project-owned engine implementation code. Allow a small private use only when it is clearly simpler and cannot leak into the public contract.
- Do not use template metaprogramming, variadic templates, perfect forwarding, SFINAE-heavy designs, expression templates, complex lambdas, deep inheritance, operator-heavy DSLs, exception-driven control flow, RTTI-dependent designs, or macros that hide control flow.
- A third-party dependency's post-C++11 requirement is allowed only in a private, independently compiled backend implementation. It must not leak into project headers or raise the language standard for other engine targets.

## C API Style

Use this style for base/common infrastructure APIs:

- Use opaque handles: `abe_net_loop_t*`, `abe_pool_t*`, `abe_shm_pool_t*`, `abe_db_t*`.
- Return `int` error codes or project error enums; do not throw exceptions across infrastructure APIs.
- Return complex results through output parameters.
- Pair resources with `create/destroy` or `init/shutdown`.
- Include `void* user_data` in callbacks.
- Document ownership for every pointer, buffer, and result object.
- Keep names explicit and predictable: `abe_pool_create`, `abe_pool_alloc`, `abe_pool_destroy`.
- Do not expose STL, templates, exceptions, RTTI, lambdas, smart pointers, `std::thread`, or `std::chrono` in public infrastructure headers.
- Do not expose third-party types in public headers.

Example:

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

## Required Infrastructure Choices

Follow these project choices:

- **Math**: implement foundational math as C APIs; avoid C++ overloads/templates in public headers.
- **Networking**: use libevent as the network/event-loop backend; do not introduce Asio, raw epoll framework rewrites, or coroutine-first networking without explicit approval.
- **Normal memory pool**: provide a C API for high-frequency allocations such as network buffers, packet decode, and tick-local temporary data.
- **Shared-memory pool**: provide a C API for cross-process state, warm restart, or same-host IPC; never store raw process-local pointers in durable shared-memory structures.
- **Time**: expose both `real` Unix time and `mono` monotonic time with millisecond and second APIs. Drive runtime timers with monotonic time; convert UTC absolute schedule inputs to monotonic targets at scheduling time.
- **Logging**: wrap native C++ spdlog directly in `engine/src/log` with simple C++11 functions and `ABE_LOG_*` macros. Use `base/time` real time for timestamps and daily log directories. Do not expose spdlog or STL types, and do not add a C bridge unless C callers require it.
- **Database**: expose database access through C-style handles, query/transaction functions, result objects, and async callbacks. Put MySQL/PostgreSQL/Redis implementations in `backends`; put optional C++ RAII wrappers in `adapters`.

## Runtime Logging And Error Codes

Apply these rules to logic and services runtime code:

- Report runtime failures through the project logging wrapper and `ABE_LOG_*`
  macros. Do not write runtime errors directly with `fprintf(stderr, ...)`,
  `std::cerr`, or `perror`.
- Startup paths that can fail before the configured logger is ready should use a
  temporary console logger or a shared runtime helper before emitting
  `ABE_LOG_*` messages. Keep CLI usage/help output on the logging path as
  informational records instead of printing directly to stdout or stderr.
- Infrastructure and service status enums must map to the unified
  `abe_status_t` values from `abe_error.h` or existing service status aliases.
  Do not introduce ad hoc negative error numbers.
- Runtime functions that return `int` status values must return unified
  `ABE_*`, `SERVICE_STATUS_*`, or mapped domain status codes. Do not use bare
  `return 1` or `result = 1` to mean failure.
- Test entry points and test helpers must also use named status enums or
  constants for success and failure returns. Do not write bare `return 0` or
  `return 1` in test control flow.
- C++ test assertion helpers must log failures through the project logging
  wrapper and `ABE_LOG_*` macros instead of printing with `fprintf(stderr, ...)`.
- Internal predicate helpers should return `bool` with `true`/`false` instead
  of using `int` with `0`/`1`, unless the API must remain C-compatible.
- Client-facing protocol or business response enums may remain separate when
  they represent wire/API semantics rather than infrastructure failures.

## C++ Dependency Constraint

For a native C++ library such as spdlog:

- Wrap only the small project-facing API needed by C++ modules.
- Keep the wrapper in `engine/src/log`, not in `backends` or `adapters`.
- Keep the public header free of spdlog and STL types.
- Use C-style format strings and simple macros instead of exposing spdlog templates or macros.
- Add a separate C ABI only if a C module actually needs to call the logger.

## Build Rules

When touching build files:

- Set project-owned engine C++ targets to C++11 at most, except an independently compiled private backend target that must satisfy a third-party dependency.
- Compile C++ engine interface checks with `-std=c++11`; keep base/common C headers independently C-compatible.
- Set logic targets independently to C++11 or newer.
- Do not raise the whole repository to C++17/C++20 just because a logic module needs it.
- Isolate concrete third-party backends with their own compile options and link dependencies; only private backend implementation files may inherit a dependency's post-C++11 requirement.
- Compile C++ adapters independently from the C contracts they wrap.
- Keep generated protocol code and third-party dependency requirements from leaking into base public APIs.

## Shell Script Rules

When generating or modifying `*.sh` files:

- Put Docker/Compose environment control scripts and container-aware wrappers
  under `deploy/docker`.
- Keep `scripts/` for project commands that run in the current environment,
  including code build/test helpers and service start/stop scripts.
- Scripts under `scripts/` should not start Docker, create containers, or call
  `docker exec` as their primary behavior. The default service runtime is the
  dev container `/workspace`; enter that container first, then run service
  scripts there.
- Keep a shebang on the first line.
- Immediately after the shebang, add a short comment block containing both `Run example:` and `Command description:`.
- The run example must show a realistic command from the repository root. For sourced helper scripts, use a `source ...` example.
- The command description must explain what the command does in concrete terms.
- Keep shell scripts with LF line endings. Scripts intended to be executed directly should keep executable permission.

## Review Checklist

Before finishing code or docs work, check:

- Did any base/common public header expose C++11+ syntax or third-party types?
- Can every changed project-owned engine interface compile as C or C++11?
- Did an engine adapter or public header introduce a C++14+ requirement?
- Did an engine public interface expose an STL type?
- Is STL being used in implementation code without a clear simplicity benefit?
- Is the interface straightforward enough for manual inspection, debugging, and maintenance?
- Did the change introduce advanced templates, inheritance, operator tricks, exception-driven flow, RTTI coupling, or control-flow macros?
- Did a base module start depending on logic?
- Was a concrete third-party implementation placed in `backends` rather than `adapters`?
- Does every `adapters` module adapt a project C API into a C++ interface?
- Was a native C++ API unnecessarily mirrored as a broad C API?
- Did a new API make ownership, lifetime, and error handling obvious?
- Did networking stay libevent-based?
- Did the logging wrapper stay simple and hide spdlog/STL types?
- Did logic/services runtime errors and CLI usage/help output use `ABE_LOG_*`
  instead of direct stdout/stderr, and did infrastructure/service statuses map
  to `abe_error.h` values without bare `return 1` failure paths?
- Did database access stay behind the C database interface?
- Did logic-layer code remain free to use C++11+ without forcing that standard onto base infrastructure?
- Did every generated or modified `*.sh` file include a top-of-file run example and command description?
