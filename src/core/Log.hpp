#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <format>
#include <source_location>

namespace violet {

class Log {
public:
    static void init() {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::trace);
        console_sink->set_pattern("[%H:%M:%S.%e] [%^%l%$] [thread %t] %v");

        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("violet.log", true);
        file_sink->set_level(spdlog::level::trace);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] [%s:%#] %v");

        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        
        s_logger = std::make_shared<spdlog::logger>("VIOLET", sinks.begin(), sinks.end());
        s_logger->set_level(spdlog::level::trace);
        s_logger->flush_on(spdlog::level::err);
        
        spdlog::register_logger(s_logger);
    }

    static auto getLogger() -> std::shared_ptr<spdlog::logger>& { return s_logger; }

    template<typename... Args>
    static void trace(std::format_string<Args...> fmt, Args&&... args, 
                     const std::source_location& loc = std::source_location::current()) {
        s_logger->trace("[{}:{}] {}", loc.file_name(), loc.line(), 
                       std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void info(std::format_string<Args...> fmt, Args&&... args) {
        s_logger->info(std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void warn(std::format_string<Args...> fmt, Args&&... args) {
        s_logger->warn(std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void error(std::format_string<Args...> fmt, Args&&... args,
                     const std::source_location& loc = std::source_location::current()) {
        s_logger->error("[{}:{}] {}", loc.file_name(), loc.line(),
                       std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void critical(std::format_string<Args...> fmt, Args&&... args,
                        const std::source_location& loc = std::source_location::current()) {
        s_logger->critical("[{}:{}] {}", loc.file_name(), loc.line(),
                          std::format(fmt, std::forward<Args>(args)...));
    }

private:
    static inline std::shared_ptr<spdlog::logger> s_logger;
};

} // namespace violet

// Convenience macros with C++20 features
#define VT_TRACE(...) ::violet::Log::trace(__VA_ARGS__)
#define VT_INFO(...)  ::violet::Log::info(__VA_ARGS__)
#define VT_WARN(...)  ::violet::Log::warn(__VA_ARGS__)
#define VT_ERROR(...) ::violet::Log::error(__VA_ARGS__)
#define VT_CRITICAL(...) ::violet::Log::critical(__VA_ARGS__)

#ifdef VIOLET_DEBUG
    #define VT_ASSERT(x, ...) \
        if(!(x)) { \
            VT_ERROR("Assertion Failed: {}", __VA_ARGS__); \
            std::abort(); \
        }
#else
    #define VT_ASSERT(x, ...)
#endif