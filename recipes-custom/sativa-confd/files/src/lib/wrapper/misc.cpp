#include <unistd.h>

#include <array>
#include <cstring>
#include <string_view>

#include "confd.h"

const char *confd_strerror(int error) {
    // positive errors are our internal error types
    if(error >= 0) {
        // TODO: better way to get max error size?
        static const std::array<std::string_view, 9> gErrorStrings{{
            "success",
            "value type mismatch",
            "access denied",
            "key not found",
            "operation is not supported",
            "invalid confd response",
            "value is null",
            "out of memory",
            "invalid arguments",
        }};

        if(error >= gErrorStrings.size()) {
            return "(unknown)";
        } else {
            return gErrorStrings.at(error).data();
        }
    }
    // negative errors are system errors
    else {
        return strerror(-error);
    }
}
