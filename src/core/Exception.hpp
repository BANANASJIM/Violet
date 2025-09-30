#pragma once

#include <EASTL/string.h>

namespace violet {

class Exception {
public:
    explicit Exception(const eastl::string& message) : message_(message) {}
    explicit Exception(const char* message) : message_(message) {}

    const eastl::string& what() const { return message_; }
    const char* what_c_str() const { return message_.c_str(); }

protected:
    eastl::string message_;
};

class RuntimeError : public Exception {
public:
    explicit RuntimeError(const eastl::string& message) : Exception(message) {}
    explicit RuntimeError(const char* message) : Exception(message) {}
};

} // namespace violet