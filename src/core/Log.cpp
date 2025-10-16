#include "Log.hpp"
#include "FileSystem.hpp"
#include <cstdlib>

namespace violet {

void Log::init() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::trace);
    console_sink->set_pattern("[%H:%M:%S.%e] [%^%l%$] [thread %t] %v");

    // Use FileSystem to resolve log path relative to project root
    // Rotating file sink: max 5MB per file, keep 3 files
    eastl::string logPath = FileSystem::resolveRelativePath("violet.log");
    constexpr size_t max_file_size = 1024 * 1024 * 5;  // 5MB
    constexpr size_t max_files = 3;  // Keep last 3 log files
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath.c_str(), max_file_size, max_files);
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

void Log::setModuleEnabled(const eastl::string& module, bool enabled) {
    if (enabled) {
        s_disabledModules.erase(module);
    } else {
        s_disabledModules.insert(module);
    }
}

void Log::setGlobalLevel(spdlog::level::level_enum level) {
    if (s_logger) {
        s_logger->set_level(level);
    }
}

bool Log::isModuleEnabled(const eastl::string& module) {
    return s_disabledModules.find(module) == s_disabledModules.end();
}

void Log::loadConfigFromEnvironment() {
    const char* disabledModulesEnv = std::getenv("VIOLET_LOG_DISABLED_MODULES");
    if (disabledModulesEnv) {
        eastl::string modules(disabledModulesEnv);

        size_t start = 0;
        size_t end = 0;
        while ((end = modules.find(',', start)) != eastl::string::npos) {
            eastl::string module = modules.substr(start, end - start);
            if (!module.empty()) {
                s_disabledModules.insert(module);
            }
            start = end + 1;
        }

        eastl::string lastModule = modules.substr(start);
        if (!lastModule.empty()) {
            s_disabledModules.insert(lastModule);
        }
    }

    // Check VIOLET_DEBUG first - if set to 1, enable debug logging
    const char* debugEnv = std::getenv("VIOLET_DEBUG");
    bool debugMode = debugEnv && (eastl::string(debugEnv) == "1");

    const char* logLevelEnv = std::getenv("VIOLET_LOG_LEVEL");
    if (logLevelEnv) {
        // VIOLET_LOG_LEVEL takes priority if explicitly set
        eastl::string level(logLevelEnv);
        if (level == "trace") {
            setGlobalLevel(spdlog::level::trace);
        } else if (level == "debug") {
            setGlobalLevel(spdlog::level::debug);
        } else if (level == "info") {
            setGlobalLevel(spdlog::level::info);
        } else if (level == "warn") {
            setGlobalLevel(spdlog::level::warn);
        } else if (level == "error") {
            setGlobalLevel(spdlog::level::err);
        } else if (level == "critical") {
            setGlobalLevel(spdlog::level::critical);
        }
    } else {
        // If VIOLET_LOG_LEVEL not set, use VIOLET_DEBUG to determine level
        if (debugMode) {
            setGlobalLevel(spdlog::level::debug);
        } else {
            setGlobalLevel(spdlog::level::info);
        }
    }
}

} // namespace violet