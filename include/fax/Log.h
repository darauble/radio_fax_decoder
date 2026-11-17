// fax library: logging and errors
#pragma once

#include <functional>
#include <stdexcept>
#include <string>

namespace fax {

enum class LogLevel {
    Debug,      // per-line detail (header detection, phasing positions...), very verbose
    Info,       // configuration, progress, results
    Warning,
    Error,
};

// Receives every message of the library, without a trailing newline.
// Without a handler the library prints nothing.
using LogHandler = std::function<void(LogLevel, const std::string &)>;

// Thrown for problems the caller has to handle: unreadable or unsupported input, unwritable output.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace fax
