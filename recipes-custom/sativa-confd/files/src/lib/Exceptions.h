/**
 * @file
 *
 * @brief Custom exception types
 */
#ifndef LIBCONFD_EXCEPTIONS_H
#define LIBCONFD_EXCEPTIONS_H

#include <confd.h>

#include <stdexcept>

/**
 * @brief Confd error
 *
 * Indicates an error from the confd component, for which we have an associated status code.
 */
class ConfdError: public std::runtime_error {
    public:
        /// Make a new exception with string and error code
        ConfdError(const std::string &what, const enum confd_status status) :
            std::runtime_error(what), statusCode(status) {}

        /// Get the status code
        constexpr const auto status() const {
            return this->statusCode;
        }

    protected:
        enum confd_status statusCode;
};

#endif
