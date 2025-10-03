#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>

namespace violet {

class VulkanContext;
class DescriptorManager;
class Texture;

enum class DefaultTextureType {
    White,
    Black,
    Normal,
    MetallicRoughness
};

// Texture handle for resource management
struct TextureHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    bool isValid() const { return index > 0; }
    bool operator==(const TextureHandle& other) const {
        return index == other.index && generation == other.generation;
    }
};

class TextureManager {
public:
    TextureManager() = default;
    ~TextureManager();

    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    void init(VulkanContext* ctx, DescriptorManager* descMgr);
    void cleanup();

    // Texture ownership management with handles
    TextureHandle addTexture(eastl::unique_ptr<Texture> texture);
    void removeTexture(TextureHandle handle);
    Texture* getTexture(TextureHandle handle);
    const Texture* getTexture(TextureHandle handle) const;

    // Default textures
    void createDefaultResources();
    TextureHandle getDefaultTextureHandle(DefaultTextureType type) const;
    Texture* getDefaultTexture(DefaultTextureType type) const;
    bool hasDefaultResources() const { return defaultResourcesCreated; }

    // Statistics
    size_t getTextureCount() const;

private:
    void createDefaultWhiteTexture();
    void createDefaultBlackTexture();
    void createDefaultNormalTexture();
    void createDefaultMetallicRoughnessTexture();

    bool isValidHandle(TextureHandle handle) const;

    VulkanContext* context = nullptr;
    DescriptorManager* descriptorManager = nullptr;

    struct TextureSlot {
        eastl::unique_ptr<Texture> texture;
        uint32_t generation = 0;
        bool inUse = false;
    };

    eastl::vector<TextureSlot> textureSlots;
    eastl::vector<uint32_t> freeSlots;
    uint32_t nextSlot = 1;  // Start from 1, 0 is invalid

    struct DefaultTextures {
        TextureHandle white;
        TextureHandle black;
        TextureHandle normal;
        TextureHandle metallicRoughness;
    } defaultTextures;

    bool defaultResourcesCreated = false;
};

} // namespace violet
