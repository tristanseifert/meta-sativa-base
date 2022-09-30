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

/**
 * @brief Serialize an update request for the given key name
 *
 * @param keyName Key name to update
 * @param value Value to set for the key
 */
static std::vector<std::byte> SerializeUpdateRequest(const char *keyName, cbor_item_t *value) {
    std::vector<std::byte> msgBuf;

    // build the request object
    cbor_item_t *root = cbor_new_definite_map(2);
    cbor_map_add(root, (struct cbor_pair) {
        .key = cbor_move(cbor_build_string("key")),
        .value = cbor_move(cbor_build_string(keyName))
    });
    cbor_map_add(root, (struct cbor_pair) {
        .key = cbor_move(cbor_build_string("value")),
        .value = cbor_move(value)
    });

    // serialize it
    size_t rootBufLen;
    unsigned char *rootBuf{nullptr};
    const size_t serializedBytes = cbor_serialize_alloc(root, &rootBuf, &rootBufLen);

    // copy it into a vector
    msgBuf.resize(serializedBytes);
    std::copy(reinterpret_cast<const std::byte *>(rootBuf),
            reinterpret_cast<const std::byte *>(rootBuf + serializedBytes), msgBuf.begin());

    // clean up
    free(rootBuf);
    cbor_decref(&root);

    return msgBuf;
}

/**
 * @brief Validate a set response
 *
 * Ensures the update completed successfully, raising an error if not.
 */
static void ValidateResponse(const cbor_item_t *root) {
    // TODO: implement
}

/**
 * @brief Handle an update of a variable
 *
 * @param key Name of the key to request
 * @param value CBOR value corresponding to the key
 * @param Func Function to invoke with the reply
 *
 * @return Status code
 */
static int DoUpdate(const char *key, cbor_item_t *value,
        const std::function<int(cbor_item_t *)> &replyHandler = [](auto) -> int {
            return 0;
        }) {
    int ret{kConfdNotSupported};

    try {
        std::span<const std::byte> replyPayload;

        // serialize request
        auto req = SerializeUpdateRequest(key, value);

        // send the request and await the response
        std::lock_guard lg(RpcConnection::The()->lock);
        RpcConnection::The()->sendPacketWithReply(kConfigUpdate, req, replyPayload);

        // set up CBOR decoder
        cbor_load_result res{};
        auto root = cbor_load(reinterpret_cast<const cbor_data>(replyPayload.data()),
                replyPayload.size(), &res);
        if(!root || res.error.code != CBOR_ERR_NONE) {
            return kConfdInvalidResponse;
        }

        // invoke the specified handler
        try {
            // perform common validation
            ValidateResponse(root);

            // invoke the handler
            ret = replyHandler(root);

            // clean up root
            cbor_decref(&root);
        }
        // propagate exception, but clean up CBOR item
        catch(const std::exception &) {
            cbor_decref(&root);
            throw;
        }
    }
    // confd errors include a status code
    catch(const ConfdError &e) {
        ret = e.status();
    }
    // return the underlying errno for system errors
    catch(const std::system_error &e) {
        ret = -e.code().value();
    }
    // generic errors have no more information
    catch(const std::exception &) {
        ret = -1;
    }

    return ret;
}



int confd_set_string(const char *key, const char *str, const size_t strLen) {
    if(!key || !str) {
        return kConfdInvalidArguments;
    }

    // copy the string
    char *temp = reinterpret_cast<char *>(malloc(strLen + 1));
    if(!temp) {
        return kConfdNoMemory;
    }

    memcpy(temp, str, strLen);
    temp[strLen] = '\0';

    // shove it into a CBOR string
    auto cborValue = cbor_new_definite_string();
    if(!cborValue) {
        free(temp);
        return kConfdNoMemory;
    }

    cbor_string_set_handle(cborValue, reinterpret_cast<cbor_mutable_data>(temp), strLen);

    return DoUpdate(key, cborValue);
}

int confd_set_blob(const char *key, const void *blob, const size_t blobLen) {
    if(!key || !blob) {
        return kConfdInvalidArguments;
    }

    auto cborValue = cbor_build_bytestring(reinterpret_cast<cbor_data>(blob), blobLen);
    if(!cborValue) {
        return kConfdNoMemory;
    }

    return DoUpdate(key, cborValue);
}

int confd_set_int(const char *key, const int64_t value) {
    if(!key) {
        return kConfdInvalidArguments;
    }

    auto cborValue = cbor_build_uint64(static_cast<uint64_t>(value));
    if(!cborValue) {
        return kConfdNoMemory;
    }

    return DoUpdate(key, cborValue);
}

int confd_set_real(const char *key, const double value) {
    if(!key) {
        return kConfdInvalidArguments;
    }

    auto cborValue = cbor_build_float8(value);
    if(!cborValue) {
        return kConfdNoMemory;
    }

    return DoUpdate(key, cborValue);
}

int confd_set_bool(const char *key, const bool value) {
    if(!key) {
        return kConfdInvalidArguments;
    }

    auto cborValue = cbor_build_bool(value);
    if(!cborValue) {
        return kConfdNoMemory;
    }

    return DoUpdate(key, cborValue);
}

int confd_set_null(const char *key) {
    if(!key) {
        return kConfdInvalidArguments;
    }

    auto cborValue = cbor_new_null();
    if(!cborValue) {
        return kConfdNoMemory;
    }

    return DoUpdate(key, cborValue);
}
