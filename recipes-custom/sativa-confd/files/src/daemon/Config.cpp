#include <sys/types.h>
#include <grp.h>
#include <pwd.h>

#include <cerrno>
#include <filesystem>
#include <stdexcept>
#include <unordered_set>

#include <fmt/core.h>
#include <plog/Log.h>
#include <toml++/toml.h>

#include "Config.h"

std::filesystem::path Config::gSocketPath;
mode_t Config::gSocketMode{S_IRWXU | S_IRWXG | S_IRWXO};

std::filesystem::path Config::gStoragePath;
std::vector<Config::AccessDescriptor> Config::gAllowList;

/**
 * @brief Parse configuration file
 *
 * Read the TOML-encoded configuration file at the specified path.
 *
 * @param isRoot Whether this is the root config file, or one that was included
 */
void Config::Read(const std::filesystem::path &path, const bool isRoot) {
    static std::unordered_set<std::string> gOpenedFiles;

    // ensure file hasn't been read before (to avoid loops) before parsing it
    const auto &nativePath = path.native();
    if(gOpenedFiles.contains(nativePath)) {
        throw std::runtime_error(fmt::format("recursion detected (I already parsed '{}'!)",
                    nativePath));
    }
    gOpenedFiles.emplace(nativePath);

    const auto tbl = toml::parse_file(path.native());

    // RPC settings (mandatory in root)
    const auto rpc = tbl["rpc"];
    if(rpc) {
        if(!rpc.is_table()) {
            throw std::runtime_error("invalid `rpc` key");
        }

        ReadRpc(*rpc.as_table());
    } else if(isRoot) {
        throw std::runtime_error("missing `rpc` key");
    }

    // data store settings (mandatory in root)
    const auto storage = tbl["storage"];
    if(storage) {
        if(!storage.is_table()) {
            throw std::runtime_error("invalid `storage` key");
        }

        ReadStorage(*storage.as_table());
    } else if(isRoot) {
        throw std::runtime_error("missing `storage` key");
    }

    // access control (optional in root)
    const auto access = tbl["access"];
    if(access) {
        if(!access.is_table()) {
            throw std::runtime_error("invalid `access` key");
        }

        ReadAccess(*access.as_table());
    }


    // additional include files
    const auto includes = tbl["include"];
    if(includes) {
        if(!includes.is_array_of_tables()) {
            throw std::runtime_error("invalid `include` directives (expected array of tables)");
        }

        const auto &arr = *includes.as_array();
        for(const auto &directive : arr) {
            if(!directive.is_table()) {
                throw std::logic_error("unexpected `include` directive type");
            }

            ReadInclude(*directive.as_table());
        }
    }
}

/**
 * @brief Read RPC configuration
 *
 * This just reads out the listen socket path.
 */
void Config::ReadRpc(const toml::table &tbl) {
    const std::string path = tbl["listen"].value_or("");
    if(path.empty()) {
        throw std::runtime_error("invalid `rpc.listen` key (expected string)");
    }

    gSocketPath = path;

    // access mode, if specified
    const auto mode = tbl["umode"].value_or(-1);
    if(mode >= 0) {
        gSocketMode = mode & (S_IRWXU | S_IRWXG | S_IRWXO);
    }
}

/**
 * @brief Read storage configuration
 *
 * Assemble the full path to the sqlite database file that stores the configuration data, while
 * checking that the containing directory at least exists.
 */
void Config::ReadStorage(const toml::table &tbl) {
    // get directory
    const std::string dir = tbl["dir"].value_or("");
    gStoragePath = dir;

    if(!std::filesystem::is_directory(gStoragePath)) {
        throw std::runtime_error(fmt::format("invalid storage directory '{}'", gStoragePath.string()));
    }

    // append the db name to it
    const std::string name = tbl["db"].value_or("");
    if(name.empty()) {
        throw std::runtime_error("invalid storage file");
    }

    gStoragePath /= name;
}

/**
 * @brief Read access configuration
 *
 * This gets the default access mode, followed by any explicitly allowed keys/key paths.
 */
void Config::ReadAccess(const toml::table &tbl) {
    // default mode
    // TODO: read it

    // allowed accesses
    const auto accesses = tbl["allow"];
    if(!accesses) {
        return;
    } else if(!accesses.is_array_of_tables()) {
        throw std::runtime_error("invalid access.allow key (expected array of tables)");
    }

    auto arr = *accesses.as_array();
    arr.for_each([](auto&& tbl) {
        if(!tbl.is_table()) {
            throw std::runtime_error("invalid access.allow value (expected table)");
        }
        ReadAccessAllow(*tbl.as_table());
    });
}

/**
 * @brief Process a single allow list entry
 *
 * We expect to have some sort of definition of the source (usually, via the `user` key, which can
 * be either an uid or username we look up and convert to uid) and the allowed key paths to access
 * in the form of the `paths` array, which is strings.
 *
 * Key paths can be specified as literal key names, or with a wildcard character to specify all
 * keys under a certain path.
 */
void Config::ReadAccessAllow(const toml::table &tbl) {
    AccessDescriptor desc;

    // get the access specifier
    const auto user = tbl["user"];
    if(user) {
        user.visit([&desc](auto&& n) {
            // look up username
            if constexpr (toml::is_string<decltype(n)>) {
                const auto name = *n;

                errno = 0;
                const auto pwd = getpwnam(name.c_str());
                if(!pwd && errno) {
                    throw std::system_error(errno, std::generic_category(), "getpwnam");
                } else if(!pwd) {
                    PLOG_ERROR << "failed to look up username '" << name << "'!";
                } else {
                    desc.user = pwd->pw_uid;
                }
            }
            // literal uid
            else if constexpr (toml::is_integer<decltype(n)>) {
                desc.user = static_cast<uid_t>(*n);
            } else {
                throw std::runtime_error("invalid access.allow.user value (expected integer or string)");
            }
        });
    }

    const auto group = tbl["group"];
    if(group) {
        group.visit([&desc](auto&& n) {
            // look up group
            if constexpr (toml::is_string<decltype(n)>) {
                const auto name = *n;

                errno = 0;
                const auto grp = getgrnam(name.c_str());
                if(!grp && errno) {
                    throw std::system_error(errno, std::generic_category(), "getpwnam");
                } else if(!grp) {
                    PLOG_ERROR << "failed to look up group '" << name << "'!";
                } else {
                    desc.group = grp->gr_gid;
                }
            }
            // literal gid
            else if constexpr (toml::is_integer<decltype(n)>) {
                desc.group = static_cast<gid_t>(*n);
            } else {
                throw std::runtime_error("invalid access.allow.group value (expected integer or string)");
            }
        });
    }

    if(!desc.user && !desc.group) {
        throw std::runtime_error("invalid access.allow specifier: neither user nor group specified");
    }

    // allowed key paths
    const auto paths = tbl["paths"];
    if(!paths || !paths.is_array()) {
        throw std::runtime_error("invalid access.allow.paths key (expected array)");
    }

    paths.as_array()->for_each([&desc](auto&& el) {
        if(!el.is_string()) {
            throw std::runtime_error("invalid access.allow.paths value (expected string)");
        }
        desc.allowed.emplace(*(el.as_string()));
    });

    // store it in allow list
    gAllowList.emplace_back(std::move(desc));
}



/**
 * @brief Process an include directive
 *
 * Include directives can be used to reference another configuration file (or directory containing
 * files) by path name. Those files are opened and parsed the same as the main configuration
 * file.
 *
 * The following keys can be specified in an `include` section:
 *
 * - `path`: String to a filename or directory containing files to read. If a directory is
 *           specified, all files with the `.toml` extension will be opened and parsed.
 */
void Config::ReadInclude(const toml::table &tbl) {
    // get the path
    const std::string pathStr = tbl["path"].value_or("");
    if(pathStr.empty()) {
        throw std::runtime_error("invalid empty include path");
    }

    std::filesystem::path path(pathStr);

    // first, ensure it exists
    if(!std::filesystem::exists(path)) {
        throw std::runtime_error(fmt::format("include path '{}' does not exist", path.native()));
    }

    // then either recurse into the directory or open the file as-is
    if(std::filesystem::is_directory(path)) {
        ProcessIncludeDirectory(path);
    } else {
        PLOG_VERBOSE << "including config file: " << path;
        Read(path, false);
    }
}

/**
 * @brief Load all config files in the given directory
 *
 * Find all configuration files (those ending with `.toml` extension) in the specified directory
 * and read them.
 *
 * @param path Directory to scan for config files
 */
void Config::ProcessIncludeDirectory(const std::filesystem::path &path) {
    for(auto const &dent : std::filesystem::directory_iterator{path}) {
        // skip files that haven't got the appropriate extension
        if(!dent.is_regular_file()) {
            continue;
        }

        const auto &dentPath = dent.path();
        if(dentPath.extension() != ".toml") {
            continue;
        }

        // process the file as normal
        PLOG_VERBOSE << "including config file: " << dentPath;
        Read(dentPath, false);
    }
}
