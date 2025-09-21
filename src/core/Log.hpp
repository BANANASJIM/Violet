#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <EASTL/string.h>
#include <EASTL/unordered_set.h>
#include <memory>
#include <vector>
#include <format>

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

        // Load configuration from environment
        loadConfigFromEnvironment();
    }

    static auto getLogger() -> std::shared_ptr<spdlog::logger>& { return s_logger; }

    // Modular logging methods
    template <typename... Args>
    static void trace(const eastl::string& module, const eastl::string& fmt, Args&&... args) {
        if (isModuleEnabled(module)) {
            s_logger->trace("[{}] {}", module.c_str(), std::vformat(fmt.c_str(), std::make_format_args(args...)));
        }
    }

    template <typename... Args>
    static void debug(const eastl::string& module, const eastl::string& fmt, Args&&... args) {
        if (isModuleEnabled(module)) {
            s_logger->debug("[{}] {}", module.c_str(), std::vformat(fmt.c_str(), std::make_format_args(args...)));
        }
    }

    template <typename... Args>
    static void info(const eastl::string& module, const eastl::string& fmt, Args&&... args) {
        if (isModuleEnabled(module)) {
            s_logger->info("[{}] {}", module.c_str(), std::vformat(fmt.c_str(), std::make_format_args(args...)));
        }
    }

    template <typename... Args>
    static void warn(const eastl::string& module, const eastl::string& fmt, Args&&... args) {
        if (isModuleEnabled(module)) {
            s_logger->warn("[{}] {}", module.c_str(), std::vformat(fmt.c_str(), std::make_format_args(args...)));
        }
    }

    template <typename... Args>
    static void error(const eastl::string& module, const eastl::string& fmt, Args&&... args) {
        if (isModuleEnabled(module)) {
            s_logger->error("[{}] {}", module.c_str(), std::vformat(fmt.c_str(), std::make_format_args(args...)));
        }
    }

    template <typename... Args>
    static void critical(const eastl::string& module, const eastl::string& fmt, Args&&... args) {
        if (isModuleEnabled(module)) {
            s_logger->critical("[{}] {}", module.c_str(), std::vformat(fmt.c_str(), std::make_format_args(args...)));
        }
    }

    // Legacy methods (backward compatibility) - default to "Core" module
    template <typename... Args> static void trace(const std::string& fmt, Args&&... args) {
        if (isModuleEnabled(eastl::string("Core"))) {
            s_logger->trace("[Core] {}", std::vformat(fmt, std::make_format_args(args...)));
        }
    }

    template <typename... Args> static void debug(const std::string& fmt, Args&&... args) {
        if (isModuleEnabled(eastl::string("Core"))) {
            s_logger->debug("[Core] {}", std::vformat(fmt, std::make_format_args(args...)));
        }
    }

    template <typename... Args> static void info(const std::string& fmt, Args&&... args) {
        if (isModuleEnabled(eastl::string("Core"))) {
            s_logger->info("[Core] {}", std::vformat(fmt, std::make_format_args(args...)));
        }
    }

    template <typename... Args> static void warn(const std::string& fmt, Args&&... args) {
        if (isModuleEnabled(eastl::string("Core"))) {
            s_logger->warn("[Core] {}", std::vformat(fmt, std::make_format_args(args...)));
        }
    }

    template <typename... Args> static void error(const std::string& fmt, Args&&... args) {
        if (isModuleEnabled(eastl::string("Core"))) {
            s_logger->error("[Core] {}", std::vformat(fmt, std::make_format_args(args...)));
        }
    }

    template <typename... Args> static void critical(const std::string& fmt, Args&&... args) {
        if (isModuleEnabled(eastl::string("Core"))) {
            s_logger->critical("[Core] {}", std::vformat(fmt, std::make_format_args(args...)));
        }
    }

    // Module filtering configuration
    static void setModuleEnabled(const eastl::string& module, bool enabled);
    static void setGlobalLevel(spdlog::level::level_enum level);
    static bool isModuleEnabled(const eastl::string& module);
    static bool isModuleEnabled(const std::string& module);

private:
    static void loadConfigFromEnvironment();

    static inline std::shared_ptr<spdlog::logger> s_logger;
    static inline eastl::unordered_set<eastl::string> s_disabledModules;
};

} // namespace violet

// Convenience macros that directly use spdlog (legacy support)
#define VT_TRACE(...) ::violet::Log::getLogger()->trace(__VA_ARGS__)
#define VT_DEBUG(...) ::violet::Log::getLogger()->debug(__VA_ARGS__)
#define VT_INFO(...) ::violet::Log::getLogger()->info(__VA_ARGS__)
#define VT_WARN(...) ::violet::Log::getLogger()->warn(__VA_ARGS__)
#define VT_ERROR(...) ::violet::Log::getLogger()->error(__VA_ARGS__)
#define VT_CRITICAL(...) ::violet::Log::getLogger()->critical(__VA_ARGS__)

// Modular logging macros (recommended for new code)
#define VT_LOG_TRACE(module, fmt, ...) ::violet::Log::trace(eastl::string(module), eastl::string(fmt), ##__VA_ARGS__)
#define VT_LOG_DEBUG(module, fmt, ...) ::violet::Log::debug(eastl::string(module), eastl::string(fmt), ##__VA_ARGS__)
#define VT_LOG_INFO(module, fmt, ...) ::violet::Log::info(eastl::string(module), eastl::string(fmt), ##__VA_ARGS__)
#define VT_LOG_WARN(module, fmt, ...) ::violet::Log::warn(eastl::string(module), eastl::string(fmt), ##__VA_ARGS__)
#define VT_LOG_ERROR(module, fmt, ...) ::violet::Log::error(eastl::string(module), eastl::string(fmt), ##__VA_ARGS__)
#define VT_LOG_CRITICAL(module, fmt, ...) ::violet::Log::critical(eastl::string(module), eastl::string(fmt), ##__VA_ARGS__)

// Module-specific convenience macros
#define VT_RENDERER_TRACE(fmt, ...) VT_LOG_TRACE("Renderer", fmt, ##__VA_ARGS__)
#define VT_RENDERER_DEBUG(fmt, ...) VT_LOG_DEBUG("Renderer", fmt, ##__VA_ARGS__)
#define VT_RENDERER_INFO(fmt, ...) VT_LOG_INFO("Renderer", fmt, ##__VA_ARGS__)
#define VT_RENDERER_WARN(fmt, ...) VT_LOG_WARN("Renderer", fmt, ##__VA_ARGS__)
#define VT_RENDERER_ERROR(fmt, ...) VT_LOG_ERROR("Renderer", fmt, ##__VA_ARGS__)

#define VT_SCENE_TRACE(fmt, ...) VT_LOG_TRACE("Scene", fmt, ##__VA_ARGS__)
#define VT_SCENE_DEBUG(fmt, ...) VT_LOG_DEBUG("Scene", fmt, ##__VA_ARGS__)
#define VT_SCENE_INFO(fmt, ...) VT_LOG_INFO("Scene", fmt, ##__VA_ARGS__)
#define VT_SCENE_WARN(fmt, ...) VT_LOG_WARN("Scene", fmt, ##__VA_ARGS__)
#define VT_SCENE_ERROR(fmt, ...) VT_LOG_ERROR("Scene", fmt, ##__VA_ARGS__)

#define VT_UI_TRACE(fmt, ...) VT_LOG_TRACE("UI", fmt, ##__VA_ARGS__)
#define VT_UI_DEBUG(fmt, ...) VT_LOG_DEBUG("UI", fmt, ##__VA_ARGS__)
#define VT_UI_INFO(fmt, ...) VT_LOG_INFO("UI", fmt, ##__VA_ARGS__)
#define VT_UI_WARN(fmt, ...) VT_LOG_WARN("UI", fmt, ##__VA_ARGS__)
#define VT_UI_ERROR(fmt, ...) VT_LOG_ERROR("UI", fmt, ##__VA_ARGS__)

#ifdef VIOLET_DEBUG
    #define VT_ASSERT(x, ...)                                                                                          \
        if (!(x)) {                                                                                                    \
            VT_ERROR("Assertion Failed: {}", __VA_ARGS__);                                                             \
            ::abort();                                                                                                 \
        }
#else
    #define VT_ASSERT(x, ...)
#endif