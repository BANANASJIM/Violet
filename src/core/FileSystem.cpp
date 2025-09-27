#include "FileSystem.hpp"
#include "Log.hpp"
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

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
        VT_ERROR("Failed to open file: {}", path.c_str());
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
        VT_ERROR("Failed to open file: {}", path.c_str());
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

eastl::string FileSystem::getExecutableDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    fs::path exePath(buffer);
    return exePath.parent_path().string().c_str();
#elif defined(__APPLE__)
    char buffer[1024];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        char realPath[1024];
        if (realpath(buffer, realPath)) {
            fs::path exePath(realPath);
            return exePath.parent_path().string().c_str();
        }
    }
    return "";
#else
    char buffer[1024];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        fs::path exePath(buffer);
        return exePath.parent_path().string().c_str();
    }
    return "";
#endif
}

eastl::string FileSystem::getProjectRootDirectory() {
    static eastl::string projectRoot;
    if (!projectRoot.empty()) {
        return projectRoot;
    }

    // 从可执行文件目录开始向上查找项目根目录
    fs::path currentPath = fs::path(getExecutableDirectory().c_str());

    // 查找包含 CMakeLists.txt 或 vcpkg.json 的目录作为项目根目录
    while (!currentPath.empty() && currentPath != currentPath.root_path()) {
        fs::path cmakeFile = currentPath / "CMakeLists.txt";
        fs::path vcpkgFile = currentPath / "vcpkg.json";

        if (fs::exists(cmakeFile) || fs::exists(vcpkgFile)) {
            projectRoot = currentPath.string().c_str();
            return projectRoot;
        }

        currentPath = currentPath.parent_path();
    }

    // 如果找不到项目根目录，使用可执行文件目录
    projectRoot = getExecutableDirectory();
    return projectRoot;
}

eastl::string FileSystem::resolveRelativePath(const eastl::string& relativePath) {
    if (relativePath.empty()) {
        return "";
    }

    // 如果已经是绝对路径，直接返回
    fs::path path(relativePath.c_str());
    if (path.is_absolute()) {
        return relativePath;
    }

    // 相对于项目根目录解析路径
    fs::path projectRoot(getProjectRootDirectory().c_str());
    fs::path resolved = projectRoot / path;
    return resolved.string().c_str();
}

}
