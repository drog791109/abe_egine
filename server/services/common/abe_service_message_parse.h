#ifndef ABE_SERVICE_MESSAGE_PARSE_H
#define ABE_SERVICE_MESSAGE_PARSE_H

/* Internal helper used by service session impl files.
 * Not part of any public API.
 */

#include "protocol.pb.h"

#include <google/protobuf/message_lite.h>

namespace abe {
namespace service {
namespace internal {

template <typename T>
inline int parse_message(const void* data, uint32_t size, T* out_request)
{
    const void* src = data;
    if (src == NULL && size == 0u) {
        src = "";
    }
    if (src == NULL || !out_request->ParseFromArray(src, static_cast<int>(size))) {
        return ::abe::proto::client::ERROR_CODE_COMMON_PROTOCOL_ERROR;
    }
    return ::abe::proto::client::ERROR_CODE_OK;
}

} /* namespace internal */
} /* namespace service */
} /* namespace abe */

#endif /* ABE_SERVICE_MESSAGE_PARSE_H */
