#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace violet {

class FileSystem {
public:
    static bool exists(const eastl::string& path);
    static bool isDirectory(const eastl::string& path);
    static bool isFile(const eastl::string& path);
    
    static eastl::vector<uint8_t> readBinary(const eastl::string& path);
    static eastl::string readText(const eastl::string& path);
    
    static eastl::vector<eastl::string> listDirectory(const eastl::string& path, bool recursive = false);
    static eastl::vector<eastl::string> listFiles(const eastl::string& path, const eastl::string& extension = "", bool recursive = false);
    
    static eastl::string getExtension(const eastl::string& path);
    static eastl::string getFilename(const eastl::string& path);
    static eastl::string getDirectory(const eastl::string& path);
    
    static eastl::string join(const eastl::string& path1, const eastl::string& path2);

    static eastl::string getExecutableDirectory();
    static eastl::string getProjectRootDirectory();
    static eastl::string resolveRelativePath(const eastl::string& relativePath);
};

}
