#ifndef ABE_TEST_H
#define ABE_TEST_H

#include "abe_log.h"

enum AbeTestStatus {
    ABE_TEST_STATUS_OK = 0,
    ABE_TEST_STATUS_FAILED = 1
};

namespace abe {
namespace test {

inline void ensure_log_ready()
{
    if (!::abe::log::is_enabled(::abe::log::level_error)) {
        (void)::abe::log::init_console("abe_unit_test");
    }
}

} /* namespace test */
} /* namespace abe */

#define TEST_REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            ::abe::test::ensure_log_ready(); \
            ABE_LOG_ERROR("requirement failed expr=%s", #expr); \
            ::abe::log::flush(); \
            return ABE_TEST_STATUS_FAILED; \
        } \
    } while (0)

#endif /* ABE_TEST_H */
