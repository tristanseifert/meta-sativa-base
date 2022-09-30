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
 * @brief Serialize a query for the given key name
 */
static std::vector<std::byte> SerializeKeyRequest(const char *keyName) {
    std::vector<std::byte> msgBuf;

    // build the request object
    cbor_item_t *root = cbor_new_definite_map(1);
    cbor_map_add(root, (struct cbor_pair) {
        .key = cbor_move(cbor_build_string("key")),
        .value = cbor_move(cbor_build_string(keyName))
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
 * @brief Retrieve the value from a query response
 *
 * This will convert errors from the server (such as "key not found" or "access denied") into the
 * corresponding exception.
 */
static cbor_item_t *ExtractValue(cbor_item_t *root) {
    cbor_item_t *value{nullptr};
    bool found{false};

    // root item _must_ be a map
    if(!cbor_isa_map(root)) {
        throw ConfdError("invalid root (expected map)", kConfdInvalidResponse);
    }

    // iterate over all keys
    auto keys = cbor_map_handle(root);

    for(size_t i = 0; i < cbor_map_size(root); i++) {
        auto &pair = keys[i];

        // validate key type: must be a string
        if(!cbor_isa_string(pair.key)) {
            throw ConfdError("invalid root key type (expected string)", kConfdInvalidResponse);
        }

        const auto keyStr = reinterpret_cast<const char *>(cbor_string_handle(pair.key));
        const auto keyStrLen = cbor_string_length(pair.key);

        if(!keyStr) {
            throw ConfdError("failed to get root key", kConfdInvalidResponse);
        }

        // is this the "found" flag?
        if(!strncmp(keyStr, "found", keyStrLen)) {
            if(!cbor_isa_float_ctrl(pair.value) || !cbor_is_bool(pair.value)) {
                throw ConfdError("invalid `found` key (expected bool)", kConfdInvalidResponse);
            }

            found = cbor_get_bool(pair.value);
        }
        // is this the value?
        else if(!strncmp(keyStr, "value", keyStrLen)) {
            value = pair.value;
        }
    }

    // ensure the item was found, then return the ref to it
    if(!found) {
        throw ConfdError("key not found", kConfdNotFound);
    }

    // was the found item `null`?
    else if(cbor_isa_float_ctrl(value) && cbor_float_get_width(value) == CBOR_FLOAT_0 &&
            cbor_is_null(value)) {
        throw ConfdError("value is null", kConfdNullValue);
    }

    return value;
}

/**
 * @brief Handle a request for a variable
 *
 * @param key Name of the key to request
 * @param Func Function to invoke with the reply
 *
 * @return Status code
 */
static int DoQuery(const char *key, const std::function<int(cbor_item_t *)> &replyHandler) {
    int ret{kConfdNotSupported};

    try {
        std::span<const std::byte> replyPayload;

        // serialize request
        auto req = SerializeKeyRequest(key);

        // send the request and await the response
        std::lock_guard lg(RpcConnection::The()->lock);
        RpcConnection::The()->sendPacketWithReply(kConfigQuery, req, replyPayload);

        // set up CBOR decoder
        cbor_load_result res{};
        auto root = cbor_load(reinterpret_cast<const cbor_data>(replyPayload.data()),
                replyPayload.size(), &res);
        if(!root || res.error.code != CBOR_ERR_NONE) {
            return kConfdInvalidResponse;
        }

        // invoke the specified handler
        try {
            // perform common validation and extract value
            auto value = ExtractValue(root);
            if(!value) {
                throw std::logic_error("found value, but do not have an associated cbor item!");
            }

            // with the value, invoke the reply handler (it just verifies type and retrieves it)
            ret = replyHandler(value);

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



int confd_get_string(const char *key, char *outStr, const size_t outStrLen,
        size_t *outActualLen) {
    if(!key || !outStr || !outStrLen) {
        return kConfdInvalidArguments;
    }

    return DoQuery(key, [&](auto value) -> int {
        if(!cbor_isa_string(value)) {
            throw ConfdError("invalid value type", kConfdTypeMismatch);
        }

        // copy out string, ensuring it's zero terminated
        const auto str = reinterpret_cast<const char *>(cbor_string_handle(value));
        const auto strLen = cbor_string_length(value);

        if(outActualLen) {
            *outActualLen = strLen;
        }

        const auto toCopy = std::min(strLen, outStrLen - 1);
        memcpy(outStr, str, toCopy);
        outStr[toCopy] = '\0';

        return 0;
    });
}

int confd_get_blob(const char *key, void *outBlob, const size_t outBlobLen,
        size_t *outActualLen) {
    if(!key || !outBlob || !outBlobLen) {
        return kConfdInvalidArguments;
    }

    return DoQuery(key, [&](auto value) -> int {
        if(!cbor_isa_bytestring(value)) {
            throw ConfdError("invalid value type", kConfdTypeMismatch);
        }

        // copy out string, ensuring it's zero terminated
        const auto blob = reinterpret_cast<const std::byte *>(cbor_bytestring_handle(value));
        const auto blobLen = cbor_bytestring_length(value);

        if(outActualLen) {
            *outActualLen = blobLen;
        }

        const auto toCopy = std::min(blobLen, outBlobLen);
        memcpy(outBlob, blob, toCopy);

        return 0;
    });
}

int confd_get_int(const char *key, int64_t *outValue) {
    if(!key || !outValue) {
        return kConfdInvalidArguments;
    }

    return DoQuery(key, [&](auto value) -> int {
        if(!cbor_isa_uint(value)) {
            throw ConfdError("invalid value type", kConfdTypeMismatch);
        }

        switch(cbor_int_get_width(value)) {
            case CBOR_INT_8:
                *outValue = cbor_get_uint8(value);
                break;
            case CBOR_INT_16:
                *outValue = cbor_get_uint16(value);
                break;
            case CBOR_INT_32:
                *outValue = cbor_get_uint32(value);
                break;
            case CBOR_INT_64:
                *outValue = cbor_get_uint64(value);
                break;
        }

        return 0;
    });
}

int confd_get_real(const char *key, double *outValue) {
    if(!key || !outValue) {
        return kConfdInvalidArguments;
    }


    return DoQuery(key, [&](auto value) -> int {
        if(!cbor_isa_float_ctrl(value)) {
            throw ConfdError("invalid value type", kConfdTypeMismatch);
        }

        // convert it to the appropriate floating point type
        switch(cbor_float_get_width(value)) {
            case CBOR_FLOAT_16:
                *outValue = cbor_float_get_float2(value);
                break;
            case CBOR_FLOAT_32:
                *outValue = cbor_float_get_float4(value);
                break;
            case CBOR_FLOAT_64:
                *outValue = cbor_float_get_float8(value);
                break;

            default:
                throw ConfdError("unsupported CBOR float type", kConfdTypeMismatch);
        }

        return 0;
    });
}

int confd_get_bool(const char *key, bool *outValue) {
    if(!key || !outValue) {
        return kConfdInvalidArguments;
    }

    return DoQuery(key, [&](auto value) -> int {
        // check if integer
        if(cbor_isa_uint(value)) {
            uint64_t temp;
            switch(cbor_int_get_width(value)) {
                case CBOR_INT_8:
                    temp = cbor_get_uint8(value);
                    break;
                case CBOR_INT_16:
                    temp = cbor_get_uint16(value);
                    break;
                case CBOR_INT_32:
                    temp = cbor_get_uint32(value);
                    break;
                case CBOR_INT_64:
                    temp = cbor_get_uint64(value);
                    break;
            }

            *outValue = !!temp;
        }

        // otherwise, check if it's an actual bool value
        if(!cbor_isa_float_ctrl(value) || !cbor_is_bool(value)) {
            throw ConfdError("invalid value type", kConfdTypeMismatch);
        }

        *outValue = cbor_get_bool(value);
        return 0;
    });
}
