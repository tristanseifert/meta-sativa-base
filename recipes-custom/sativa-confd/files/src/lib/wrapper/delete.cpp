#include <cbor.h>

#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "rpc/types.h"
#include "confd.h"
#include "Exceptions.h"
#include "RpcConnection.h"

int confd_delete(const char *key) {
    if(!key) {
        return kConfdInvalidArguments;
    }

    // TODO: implement this
    return kConfdNotSupported;
}
