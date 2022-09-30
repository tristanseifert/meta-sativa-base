#include <confd.h>
#include <fmt/core.h>

#include <getopt.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

#include "HexDump.h"



/// Operation to perform
enum class Operation {
    None,
    Read,
    Write,
    Delete,
};
/// Value type
enum class Type {
    Null,
    String,
    Blob,
    Integer,
    Real,
    Bool,
};



/**
 * @brief Check a confd error value and ensure success
 *
 * If the specified error value does not correspond to success, convert it into an exception and
 * throw it.
 *
 * @param err Error value (from a confd library call)
 * @param ignoreNull Whether the "null value" error is ignored
 */
static void EnsureSuccess(const int err, const bool ignoreNull = false) {
    if(ignoreNull && err == kConfdNullValue) {
        return;
    }
    else if(err != kConfdStatusSuccess) {
        std::stringstream str;
        str << "confd error: " << err;

        // get a pretty string describing the error code
        switch(err) {
            case kConfdNotFound:
                str << " (key not found)";
                break;
            case kConfdTypeMismatch:
                str << " (type mismatch)";
                break;
            case kConfdAccessDenied:
                str << " (access denied)";
                break;
            case kConfdNotSupported:
                str << " (not supported)";
                break;
            case kConfdInvalidResponse:
                str << " (invalid response)";
                break;
            case kConfdNoMemory:
                str << " (insufficient memory)";
                break;
            case kConfdInvalidArguments:
                str << " (invalid arguments)";
                break;
        }

        throw std::runtime_error(str.str());
    }
}

/**
 * @brief Read and print the value of the provided key
 *
 * The specified key is queried, according to the specified type. Its value is then printed to the
 * standard output.
 *
 * @param key Key name to query
 * @param type Type of the key
 */
static void ReadKey(const std::string_view &key, const Type type) {
    int err;

    // validate arguments
    if(key.empty()) {
        throw std::invalid_argument("key name cannot be empty");
    } else if(type == Type::Null) {
        throw std::invalid_argument("cannot read NULL type keys");
    }

    // invoke the appropriate read function
    if(type == Type::String) {
        size_t actual{0};
        std::vector<char> buffer;
        // XXX: hacky, can we get the actual string size?
        buffer.resize(1024 * 64);

        err = confd_get_string(key.data(), buffer.data(), buffer.size(), &actual);
        EnsureSuccess(err, true);

        if(actual > buffer.size()) {
            std::cerr << fmt::format("NOTE: string is truncated (total {} bytes, showing {})",
                    actual, buffer.size() - 1);
        }

        if(err == kConfdNullValue) {
            std::cout << fmt::format("{}:{}=(null)", key, "string") << std::endl;
        } else {
            std::cout << fmt::format("{}:{}=`{}`", key, "string", buffer.data()) << std::endl;
        }
    }
    else if(type == Type::Integer) {
        int64_t value;

        err = confd_get_int(key.data(), &value);
        EnsureSuccess(err, true);

        if(err == kConfdNullValue) {
            std::cout << fmt::format("{}:{}=(null)", key, "integer") << std::endl;
        } else {
            std::cout << fmt::format("{}:{}={}", key, "integer", value) << std::endl;
        }
    }
    else if(type == Type::Real) {
        double value;

        err = confd_get_real(key.data(), &value);
        EnsureSuccess(err, true);

        if(err == kConfdNullValue) {
            std::cout << fmt::format("{}:{}=(null)", key, "real") << std::endl;
        } else {
            std::cout << fmt::format("{}:{}={:g}", key, "real", value) << std::endl;
        }
    }
    else if(type == Type::Bool) {
        bool value;

        err = confd_get_bool(key.data(), &value);
        EnsureSuccess(err, true);

        if(err == kConfdNullValue) {
            std::cout << fmt::format("{}:{}=(null)", key, "bool") << std::endl;
        } else {
            std::cout << fmt::format("{}:{}={}", key, "bool", value) << std::endl;
        }
    }
    else if(type == Type::Blob) {
        size_t actual{0};
        std::vector<std::byte> buffer;

        // XXX: hacky, can we get the actual size ahead of time?
        buffer.resize(128 * 1024);

        err = confd_get_blob(key.data(), buffer.data(), buffer.size(), &actual);
        EnsureSuccess(err, true);

        if(actual > buffer.size()) {
            std::cerr << fmt::format("NOTE: blob is truncated (total {} bytes, showing {})",
                    actual, buffer.size() - 1);
        }

        if(err == kConfdNullValue) {
            std::cout << fmt::format("{}:{}=(null)", key, "blob") << std::endl;
        } else {
            buffer.resize(actual);
            std::cout << fmt::format("{}:{}=({} bytes)", key, "blob", actual) << std::endl;

            std::span<const std::byte> span{buffer.data(), actual};
            HexDump::dumpBuffer(std::cout, span);
        }
    }
    // should never get here…
    else {
        throw std::logic_error("unsupported read type (wtf)");
    }
}

/**
 * @brief Writes a key
 *
 * Parses the value string (according to the rules below) and writes it to the specified key.
 *
 * - blob: The value is treated as a filename containing binary data to write.
 *
 * For value types not listed above, they're parsed literally.
 *
 * @param key Key name to write to
 * @param valueStr String containing the raw value to write, which will be parsed
 * @param type Value type to write
 */
static void WriteKey(const std::string_view &key, const std::string &valueStr, const Type type) {
    int err;

    // validate arguments
    if(key.empty()) {
        throw std::invalid_argument("key name cannot be empty");
    }

    // process string writes
    if(type == Type::String) {
        err = confd_set_string(key.data(), valueStr.data(), valueStr.length());
        EnsureSuccess(err);

        std::cout << fmt::format("{}:{}=`{}`", key, "string", valueStr) << std::endl;
    }
    // process integer writes
    else if(type == Type::Integer) {
        const auto value = std::stoll(valueStr);

        err = confd_set_int(key.data(), value);
        EnsureSuccess(err);

        std::cout << fmt::format("{}:{}={}", key, "integer", value) << std::endl;
    }
    // process real value writes
    else if(type == Type::Real) {
        const auto value = std::stod(valueStr);

        err = confd_set_real(key.data(), value);
        EnsureSuccess(err);

        std::cout << fmt::format("{}:{}={:g}", key, "real", value) << std::endl;
    }
    // process boolean writes
    else if(type == Type::Bool) {
        std::optional<bool> value;

        if(valueStr == "false" || valueStr == "n" || valueStr == "f") {
            value = false;
        }
        else if(valueStr == "true" || valueStr == "y" || valueStr == "t") {
            value = true;
        }

        // try to parse as number if we didn't find a bool literal
        if(!value.has_value()) {
            try {
                value = std::stoul(valueStr);
            }
            // ignore exceptions (if we can't parse it as a base-10 number)
            catch(const std::exception &) {}
        }

        // failed to parse bool value, sad!
        if(!value.has_value()) {
            throw std::invalid_argument(fmt::format("failed to parse bool from `{}`", valueStr));
        }

        err = confd_set_bool(key.data(), *value);
        EnsureSuccess(err);

        std::cout << fmt::format("{}:{}={}", key, "bool", *value) << std::endl;
    }
    // process blob writes
    else if(type == Type::Blob) {
        std::vector<std::byte> buf;
        std::filesystem::path path(valueStr);

        // get file size
        auto size = std::filesystem::file_size(path);
        buf.resize(size);

        std::cout << "Reading " << size << " bytes from " << path << "…" << std::endl;

        // read file
        std::ifstream is(path, std::ios::binary);
        is.exceptions(std::ios_base::badbit | std::ios_base::failbit);

        is.read(reinterpret_cast<char *>(buf.data()), size);

        // write as blob
        err = confd_set_blob(key.data(), buf.data(), buf.size());
        EnsureSuccess(err);

        std::cout << fmt::format("{}:{}=({} bytes)", key, "blob", buf.size()) << std::endl;

        std::span<const std::byte> span{buf.data(), buf.size()};
        HexDump::dumpBuffer(std::cout, span);
    }
    // unsupported type
    else {
        throw std::logic_error("unsupported write type (wtf)");
    }
}


/**
 * @brief Utility entry point
 *
 * The program is driven on the command line by the following switches:
 *
 * - socket: Specify the path to the UNIX domain socket used to communicate with confd
 * - key: Name of key to operate on
 * - read: Read a key
 * - write: Value to write to the key
 * - delete: Delete a key
 * - type: Type of the key's value; required for reads and writes
 *
 * Note that you must always specify one of --read, --write or --delete.
 */
int main(const int argc, char * const *argv) {
    int err;
    std::string socketPath{"/var/run/confd/rpc.sock"};
    // what operation shall we perform
    Operation what{Operation::None};
    // key to operate on (for all operations)
    std::string keyName;
    // type of value (e.g. the type of the value we read, or what we will coerce the written value to)
    std::optional<Type> valueType;
    // raw value string (for writes)
    std::string writeValue;
    // determines return code if we get to the end of main()
    bool success{true};

    // parse command line
    int c;
    while(1) {
        int index{0};
        const static struct option options[] = {
            // path to the socket
            {"socket",                  required_argument, 0, 0},
            // name of key to read, write or delete
            {"key",                     required_argument, 0, 0},
            // read the specified key
            {"read",                    no_argument, 0, 0},
            // write the specified key (the argument value is the value to write)
            {"write",                   required_argument, 0, 0},
            // delete the key
            {"delete",                  no_argument, 0, 0},
            // type for writes
            {"type",                    required_argument, 0, 0},
            // TODO: add value type flag
            {nullptr,                   0, 0, 0},
        };

        c = getopt_long(argc, argv, "", options, &index);

        // end of options
        if(c == -1) {
            break;
        }
        // long option (based on index)
        else if(!c) {
            if(index == 0) {
                socketPath = optarg;
            }
            // key name
            else if(index == 1) {
                keyName = optarg;
            }
            // we'll be reading the key
            else if(index == 2 || index == 3 || index == 4) {
                if(what != Operation::None) {
                    std::cerr << "--read, --write and --delete are mutually exclusive";
                    return 1;
                }

                switch(index) {
                    case 2:
                        what = Operation::Read;
                        break;
                    case 3:
                        what = Operation::Write;
                        writeValue = optarg;
                        break;
                    case 4:
                        what = Operation::Delete;
                        break;
                }
            }
            // value type (parse it)
            else if(index == 5) {
                std::string str(optarg);

                if(str == "string") {
                    valueType = Type::String;
                } else if(str == "int" || str == "integer") {
                    valueType = Type::Integer;
                } else if(str == "real" || str == "float" || str == "double" || str == "decimal") {
                    valueType = Type::Real;
                } else if(str == "blob" || str == "binary" || str == "data") {
                    valueType = Type::Blob;
                } else if(str == "bool" || str == "boolean") {
                    valueType = Type::Bool;
                } else if(str == "null") {
                    valueType = Type::Null;
                } else {
                    std::cerr << "invalid type: `" << str << "`" << std::endl;
                    return 1;
                }
            }
        }
    }

    // validate the args
    if(keyName.empty()) {
        std::cerr << "key name is required (--key)" << std::endl;
        return 1;
    }
    else if((what == Operation::Read || what == Operation::Write) && !valueType) {
        std::cerr << "value type is required (--type)" << std::endl;
        return 1;
    }

    // establish connection
    err = confd_open(socketPath.c_str());
    if(err) {
        std::cerr << "failed to connect to confd: " << err << std::endl;
        return 1;
    }

    // perform request
    try {
        switch(what) {
            // read a key
            case Operation::Read:
                ReadKey(keyName, *valueType);
                break;
            // write a key
            case Operation::Write:
                WriteKey(keyName, writeValue, *valueType);
                break;
            // delete a key
            case Operation::Delete:
                // TODO: implement
                throw std::runtime_error("not yet implemented");
                break;

            default:
                throw std::logic_error("unknown operation");
        }
    } catch(const std::exception &e) {
        std::cerr << fmt::format("operation failed: {}", e.what()) << std::endl;
        success = false;
    }

    // clean up
    confd_close();
    return success ? 0 : 1;
}
