#include "FileSystem.hpp"
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace violet {

namespace fs = std::filesystem;

bool FileSystem::exists(const eastl::string& path) {
    return fs::exists(path.c_str());
}

bool FileSystem::isDirectory(const eastl::string& path) {
    return fs::is_directory(path.c_str());
}

bool FileSystem::isFile(const eastl::string& path) {
    return fs::is_regular_file(path.c_str());
}

eastl::vector<uint8_t> FileSystem::readBinary(const eastl::string& path) {
    std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
    
    if (!file.is_open()) {
        spdlog::error("Failed to open file: {}", path.c_str());
        return {};
    }
    
    size_t fileSize = file.tellg();
    eastl::vector<uint8_t> buffer(fileSize);
    
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();
    
    return buffer;
}

eastl::string FileSystem::readText(const eastl::string& path) {
    std::ifstream file(path.c_str());
    
    if (!file.is_open()) {
        spdlog::error("Failed to open file: {}", path.c_str());
        return "";
    }
    
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    return eastl::string(content.c_str());
}

eastl::vector<eastl::string> FileSystem::listDirectory(const eastl::string& path, bool recursive) {
    eastl::vector<eastl::string> files;
    
    if (!exists(path) || !isDirectory(path)) {
        return files;
    }
    
    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(path.c_str())) {
            files.push_back(entry.path().string().c_str());
        }
    } else {
        for (const auto& entry : fs::directory_iterator(path.c_str())) {
            files.push_back(entry.path().string().c_str());
        }
    }
    
    return files;
}

eastl::vector<eastl::string> FileSystem::listFiles(const eastl::string& path, const eastl::string& extension, bool recursive) {
    eastl::vector<eastl::string> files;
    
    if (!exists(path) || !isDirectory(path)) {
        return files;
    }
    
    auto addIfMatches = [&](const fs::path& filePath) {
        if (fs::is_regular_file(filePath)) {
            if (extension.empty() || filePath.extension().string() == extension.c_str()) {
                files.push_back(filePath.string().c_str());
            }
        }
    };
    
    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(path.c_str())) {
            addIfMatches(entry.path());
        }
    } else {
        for (const auto& entry : fs::directory_iterator(path.c_str())) {
            addIfMatches(entry.path());
        }
    }
    
    return files;
}

eastl::string FileSystem::getExtension(const eastl::string& path) {
    fs::path p(path.c_str());
    return p.extension().string().c_str();
}

eastl::string FileSystem::getFilename(const eastl::string& path) {
    fs::path p(path.c_str());
    return p.filename().string().c_str();
}

eastl::string FileSystem::getDirectory(const eastl::string& path) {
    fs::path p(path.c_str());
    return p.parent_path().string().c_str();
}

eastl::string FileSystem::join(const eastl::string& path1, const eastl::string& path2) {
    fs::path p1(path1.c_str());
    fs::path p2(path2.c_str());
    return (p1 / p2).string().c_str();
}

}
