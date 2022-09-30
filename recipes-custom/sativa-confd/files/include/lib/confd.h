#ifndef LIBCONFD_H
#define LIBCONFD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Status codes for confd library
 *
 * These positive status codes may be returned from various confd methods (namely, query and
 * setters) to indicate confd-specific issues, as compared to negative return codes which usually
 * correspond to system errors.
 */
enum confd_status {
    kConfdStatusSuccess                 = 0,
    /// The key exists, but its value is not of the specified type
    kConfdTypeMismatch                  = 1,
    /// Access to the key is denied
    kConfdAccessDenied                  = 2,
    /// Key does not exist
    kConfdNotFound                      = 3,
    /// Operation is not supported
    kConfdNotSupported                  = 4,
    /// Failed to decode response
    kConfdInvalidResponse               = 5,
    /// The value of the requested variable is null
    kConfdNullValue                     = 6,
    /// Out of memory/resources
    kConfdNoMemory                      = 7,
    /// Invalid arguments to call
    kConfdInvalidArguments              = 8,
};


/**
 * @brief Get library version
 *
 * Returns a string (in SemVer format) that indicates the current version of this library.
 *
 * @return Version string
 *
 * @remark You should not try to deallocate or modify the returned string; it is stored in a
 *         static read-only area.
 */
const char *confd_version_string();

/**
 * @brief Convert a confd error to the associated text representation
 *
 * @param error An error code returned from a confd routine
 *
 * @return String corresponding to the error message, or NULL if unknown
 *
 * @remark The returned string is part of a static data area of the library. Do not modify or
 *         attempt to free it.
 */
const char *confd_strerror(int error);



/**
 * @brief Establish connection to confd
 *
 * Set up the initial socket connection to confd. This must be invoked before any other calls in
 * this API. The connection will remain valid until it is explicitly closed with confd_close.
 *
 * @param socketPath Location of the UNIX domain socket confd is listening on, or `nullptr` to
 *        use the default
 *
 * @return 0 on success, or a negative error code.
 */
int confd_open(const char *socketPath);

/**
 * @brief Terminate confd connection
 *
 * Closes down any previously initialized connection.
 *
 * @remark It's valid to call this more than once; if the connection is already closed when this
 *         call is made, it does nothing.
 *
 * @return 0 on success, or a negative error code.
 */
int confd_close();



/**
 * @brief Read config key (string)
 *
 * Reads a configuration key, whose value is a string.
 *
 * @param key Config key to query
 * @param outStr Buffer to hold the string value
 * @param outStrLen Size of the output string buffer, in bytes
 * @param outActualLen Variable to receive the actual string length (in bytes; may be NULL)
 *
 * @return Negative error code or one of the confd_status values.
 */
int confd_get_string(const char *key, char *outStr, const size_t outStrLen, size_t *outActualLen);

/**
 * @brief Read config key (blob)
 *
 * Reads a configuration key, whose value is a blob.
 *
 * @param key Config key to query
 * @param outBlob Buffer to hold the blob value
 * @param outBlobLen Size of the output blob buffer, in bytes
 * @param outActualLen Variable to receive the actual blob length (in bytes; may be NULL)
 *
 * @return Negative error code or one of the confd_status values.
 */
int confd_get_blob(const char *key, void *outBlob, const size_t outBlobLen, size_t *outActualLen);

/**
 * @brief Read config key (integer)
 *
 * Read a configuration key as an integer value.
 *
 * @param key Config key to query
 * @param outValue Variable to receive the integer value
 *
 * @return Negative error code or one of the confd_status values.
 */
int confd_get_int(const char *key, int64_t *outValue);

/**
 * @brief Read config key (floating point)
 *
 * Read a configuration key as a real number value.
 *
 * @param key Config key to query
 * @param outValue Variable to receive the real value
 *
 * @return Negative error code or one of the confd_status values.
 */
int confd_get_real(const char *key, double *outValue);

/**
 * @brief Read config key (boolean)
 *
 * Read a configuration key as a boolean.
 *
 * @param key Config key to query
 * @param outValue Variable to receive the bool value
 *
 * @return Negative error code or one of the confd_status values.
 */
int confd_get_bool(const char *key, bool *outValue);



/**
 * @brief Write config key (string)
 *
 * Set the value of a configuration key to be a specified string.
 *
 * @param key Config key to update
 * @param str Buffer containing the string value; it need not be zero terminated.
 * @param strLen Length of string (characters)
 *
 * @return Negative error code or one of the confd_status values.
 */
int confd_set_string(const char *key, const char *str, const size_t strLen);

/**
 * @brief Write config key (blob)
 *
 * Set the value of a configuration key to be a specified blob.
 *
 * @param key Config key to update
 * @param blob Buffer containing the binary value to set
 * @param blobLen Length of blob (bytes)
 *
 * @return Negative error code or one of the confd_status values.
 */
int confd_set_blob(const char *key, const void *blob, const size_t blobLen);

/**
 * @brief Write config key (integer)
 *
 * Set the value of a configuration key to be a specified integer value.
 *
 * @param key Config key to update
 * @param value Value to set
 *
 * @return Negative error code or one of the confd_status values.
 */
int confd_set_int(const char *key, const int64_t value);

/**
 * @brief Write config key (floating point)
 *
 * Set the value of a configuration key to be a specified real value.
 *
 * @param key Config key to update
 * @param value Value to set
 *
 * @return Negative error code or one of the confd_status values.
 */
int confd_set_real(const char *key, const double value);

/**
 * @brief Write config key (boolean)
 *
 * Set the value of a configuration key to be a specified bool value.
 *
 * @param key Config key to update
 * @param value Value to set
 *
 * @return Negative error code or one of the confd_status values.
 */
int confd_set_bool(const char *key, const bool value);

/**
 * @brief Write config key (null)
 *
 * Set the value of the configuration key to be null.
 *
 * @param key Config key to update
 *
 * @return Negative error code or one of the confd_status values.
 */
int confd_set_null(const char *key);



/**
 * @brief Delete a config key
 *
 * Deletes the specified config key.
 *
 * @param key Config key to delete
 */
int confd_delete(const char *key);

#ifdef __cplusplus
}
#endif

#endif
