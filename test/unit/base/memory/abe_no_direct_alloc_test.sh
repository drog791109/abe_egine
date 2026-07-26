#!/usr/bin/env bash
#
# Run example:
#   ./test/unit/base/memory/abe_no_direct_alloc_test.sh "$(pwd)"
# Command description:
#   Scan project C/C++ code for direct allocation calls outside memory modules.

set -eu

repo_root="${1:?repo root is required}"

pattern='(^|[^[:alnum:]_])(malloc|calloc|realloc|free)[[:space:]]*\(|(^|[^[:alnum:]_])(new|delete)([^[:alnum:]_]|$)'

matches="$(
    find "${repo_root}/server" "${repo_root}/test" \
        -type f \
        \( -name '*.c' -o -name '*.h' -o -name '*.cc' -o -name '*.cpp' -o -name '*.hpp' \) \
        ! -path "${repo_root}/server/engine/src/base/memory/*" \
        ! -path "${repo_root}/server/engine/src/base/shm/*" \
        -print0 |
    xargs -0 grep -n -E "${pattern}" || true
)"

if [ -n "${matches}" ]; then
    printf '%s\n' "Direct allocation is only allowed in base memory pool modules:"
    printf '%s\n' "${matches}"
    exit 1
fi
