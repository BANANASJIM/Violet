#include "Log.hpp"
#include <cstdlib>

namespace violet {

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

bool Log::isModuleEnabled(const std::string& module) {
    return s_disabledModules.find(eastl::string(module.c_str())) == s_disabledModules.end();
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

    const char* logLevelEnv = std::getenv("VIOLET_LOG_LEVEL");
    if (logLevelEnv) {
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
    }
}

} // namespace violet