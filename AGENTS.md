# ABE Engine Project Instructions

These rules are mandatory project background. Apply them to every generated or
modified file, and read
`.codex/skills/abe-engine-server-rules/SKILL.md` before changing server engine
code, build files, tests, or architecture documentation.

## Default Runtime Environment

- Run project builds, tests, service binaries, and dependency checks inside this
  repository's `dev` container by default.
- Use the host only for editing files, Git operations, and Docker/Compose
  control unless a task explicitly asks for host-side validation.
- Inside the dev container, the current repository must be mounted at
  `/workspace`; do not use another project's container as this project's
  runtime environment.
- The compose `dev` service must use `ABE_REPO_ROOT` as the host source path
  for the `/workspace` bind mount. Project scripts should set it to the current
  repository root before calling `docker compose`.
- If container execution is unavailable and a command must run on the host,
  state that explicitly in the result.

## Engine Interface Compatibility

- Every project-owned public interface under `server/engine` must compile as C
  or as C++ using `-std=c++11`. Do not require C++14 or newer.
- C++11 is the compatibility ceiling, not a reason to use every C++11 feature.
  Prefer plain C APIs and C++03-style code when they express the behavior
  clearly.
- `engine/src/adapters` only adapts project C APIs into simple C++ interfaces.
  Its public interfaces must not require a standard newer than C++11.
- Do not expose STL types in engine public interfaces. Prefer plain structs,
  opaque handles, `const char*`, pointer-plus-size buffers, explicit callbacks,
  and project-owned enums.
- Minimize STL use in project-owned engine implementation code. Use a small,
  private STL facility only when it is clearly simpler than the C alternative
  and does not leak into a public interface.
- Do not use advanced or clever constructs in project-owned engine APIs:
  template metaprogramming, variadic templates, perfect forwarding,
  SFINAE-heavy designs, expression templates, complex lambdas, deep
  inheritance, operator-heavy DSLs, exception-driven control flow,
  RTTI-dependent designs, or macros that hide control flow.
- Favor explicit functions, plain structs, opaque handles, obvious ownership,
  paired create/destroy operations, and direct error-code handling.
- Project-owned engine implementation code should prefer C or straightforward
  C++ that remains easy to inspect and maintain manually.
- If a third-party C++ dependency requires a standard newer than C++11, isolate
  that requirement in
  a private `engine/src/backends` implementation target. Do not expose its
  types or language requirements through project headers.

## Layer Meaning

- `engine/src/base` and `engine/src/common` define stable C contracts.
- `engine/src/backends` contains concrete implementations and raw third-party
  integrations.
- `engine/src/log` contains the direct C++11 wrapper for the native C++ logging
  library and its simple logging macros. It may depend on `base/time` for
  timestamp and daily log directory calculation.
- `engine/src/adapters` contains only C API to simple C++11-or-earlier API
  adaptations.
- Native C++ dependencies used only by `logic` or `services` should be used
  there directly instead of being mechanically mirrored as C APIs.
- Native C++ logging such as spdlog belongs in `engine/src/log`; do not add a C
  ABI bridge unless C modules actually need to call the logger.
- `logic` must not depend on concrete backends. `services` selects and assembles
  backend implementations.

## Change Discipline

- Optimize for clarity and long-term human maintenance over abstraction,
  compactness, novelty, or use of newer language features.
- Keep interfaces small, names explicit, ownership documented, and behavior
  testable.
- When changing engine interfaces, verify C and/or C++11 header compatibility
  as appropriate and keep third-party headers out of public contracts.

## Runtime Logging And Error Codes

- Logic and services runtime code must report runtime failures through the
  project logging wrapper and `ABE_LOG_*` macros. Do not write runtime errors
  directly with `fprintf(stderr, ...)`, `std::cerr`, or `perror`.
- Startup paths that can fail before the configured logger is ready should use a
  temporary console logger or a shared runtime helper before emitting
  `ABE_LOG_*` messages. Keep CLI usage/help output on the logging path as
  informational records instead of printing directly to stdout or stderr.
- Logic and services infrastructure status enums must map to the unified
  `abe_status_t` values from `abe_error.h` or existing service status aliases.
  Do not introduce ad hoc negative error numbers. Client-facing protocol or
  business response enums may remain separate when they represent wire/API
  semantics rather than infrastructure failures.
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

## Shell Script Rules

- Docker/Compose environment control scripts and container-aware wrappers belong
  under `deploy/docker`.
- `scripts/` is for project commands that run in the current environment,
  including code build/test helpers and service start/stop scripts.
- Scripts under `scripts/` should not start Docker, create containers, or call
  `docker exec` as their primary behavior. The default service runtime is the
  dev container `/workspace`; enter that container first, then run service
  scripts there.
- Every generated or modified `*.sh` file must start with a shebang and then a
  short comment block that includes both `Run example:` and
  `Command description:`.
- The run example must show a realistic command from the repository root. If
  the file is meant to be sourced rather than executed, show a `source ...`
  example.
- The command description must explain what the command does, not just repeat
  the file name.
- Keep shell scripts with LF line endings. Scripts intended to be executed
  directly should keep executable permission.

## Python 3 Script Rules

- Every Python 3 script must begin with a comment block or module docstring
  that documents its usage, parameters, and at least one realistic example.
- Put the documentation immediately after the shebang when the script has one;
  otherwise put it at the start of the file.
- The usage section must show how to invoke the script from the repository
  root. The parameters section must describe every command-line parameter,
  relevant environment variable, default value, and validation constraint.
- Keep the header synchronized with `argparse` definitions and update it when
  the script's interface changes.
