# ABE Engine Project Instructions

These rules are mandatory project background. Apply them to every generated or
modified file, and read
`.codex/skills/abe-engine-server-rules/SKILL.md` before changing server engine
code, build files, tests, or architecture documentation.

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
