#pragma once

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <EASTL/string.h>
#include <EASTL/unordered_set.h>
#include <EASTL/vector.h>
#include <memory>
#include <vector>
#include <format>

namespace violet {

class Log {
public:
    static void init();

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


    // Module filtering configuration
    static void setModuleEnabled(const eastl::string& module, bool enabled);
    static void setGlobalLevel(spdlog::level::level_enum level);
    static bool isModuleEnabled(const eastl::string& module);

private:
    static void loadConfigFromEnvironment();

    static inline std::shared_ptr<spdlog::logger> s_logger;
    static inline eastl::unordered_set<eastl::string> s_disabledModules;
};

} // namespace violet

