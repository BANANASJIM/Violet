#pragma once

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include "GLTFAsset.hpp"

namespace violet {

// Asset loader for glTF files (no Vulkan dependencies)
class AssetLoader {
public:
    // Load glTF file and parse into intermediate representation
    static eastl::unique_ptr<GLTFAsset> loadGLTF(const eastl::string& filePath);

private:
    // Helper methods for parsing glTF components
    static void loadMeshes(void* model, GLTFAsset* asset);
    static void loadTextures(void* model, GLTFAsset* asset);
    static void loadMaterials(void* model, GLTFAsset* asset);
    static void loadNodes(void* model, GLTFAsset* asset);
    static Transform extractTransform(const void* nodePtr);
};

} // namespace violet
