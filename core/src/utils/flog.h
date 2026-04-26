#pragma once
#include <vector>
#include <string>
#include <stdint.h>

namespace flog {
    enum Type {
        TYPE_DEBUG,
        TYPE_INFO,
        TYPE_WARNING,
        TYPE_ERROR,
        _TYPE_COUNT
    };

    // IO functions
    void __log__(Type type, const char* fmt, const std::vector<std::string>& args);

    // Conversion functions
    inline std::string __toString__(bool value) { return value ? "true" : "false"; }
    inline std::string __toString__(char value) { return std::string("") + value; }
    std::string __toString__(const void* value);
    template <class T>
    std::string __toString__(const T& value) {
        if constexpr (std::is_arithmetic<T>::value) {
            return std::to_string(value);
        } else if constexpr (std::is_same<T, std::string>::value) {
            return value;
        } else if constexpr (std::is_same_v<T, char*> || std::is_same_v<T, const char*>) {
            return value ? std::string(value) : std::string();
        } else {
            static_assert(sizeof(T) == 0, "No __toString__ conversion available for this type");
        }
    }

    // Utility to generate a list from arguments
    inline void __genArgList__(std::vector<std::string>& args) {}
    template <typename First, typename... Others>
    inline void __genArgList__(std::vector<std::string>& args, First first, Others... others) {
        // Add argument
        args.push_back(__toString__(first));

        // Recursive call that will be unrolled since the function is inline
        __genArgList__(args, others...);
    }

    // Logging functions
    template <typename... Args>
    void log(Type type, const char* fmt, Args... args) {
        std::vector<std::string> _args;
        _args.reserve(sizeof...(args));
        __genArgList__(_args, args...);
        __log__(type, fmt, _args);
    }

    template <typename... Args>
    inline void debug(const char* fmt, Args... args) {
        log(TYPE_DEBUG, fmt, args...);
    }

    template <typename... Args>
    inline void info(const char* fmt, Args... args) {
        log(TYPE_INFO, fmt, args...);
    }

    template <typename... Args>
    inline void warn(const char* fmt, Args... args) {
        log(TYPE_WARNING, fmt, args...);
    }

    template <typename... Args>
    inline void error(const char* fmt, Args... args) {
        log(TYPE_ERROR, fmt, args...);
    }
}