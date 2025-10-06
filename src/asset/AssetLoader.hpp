#pragma once

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/functional.h>
#include "GLTFAsset.hpp"

namespace violet {

class ResourceManager;

// Asset loader for glTF files (no Vulkan dependencies)
class AssetLoader {
public:
    // Load glTF file and parse into intermediate representation (synchronous)
    static eastl::unique_ptr<GLTFAsset> loadGLTF(const eastl::string& filePath);

    // Async version: loads on worker thread, calls callback on main thread
    static void loadGLTFAsync(
        const eastl::string& filePath,
        ResourceManager* resourceManager,
        eastl::function<void(eastl::unique_ptr<GLTFAsset>, eastl::string)> callback
    );

private:
    // Helper methods for parsing glTF components
    static void loadMeshes(void* model, GLTFAsset* asset);
    static void loadTextures(void* model, GLTFAsset* asset);
    static void loadMaterials(void* model, GLTFAsset* asset);
    static void loadNodes(void* model, GLTFAsset* asset);
    static Transform extractTransform(const void* nodePtr);
};

} // namespace violet
