#include "TextureManager.hpp"
#include "core/Log.hpp"
#include "renderer/core/VulkanContext.hpp"
#include "renderer/descriptor/DescriptorManager.hpp"
#include "resource/Texture.hpp"

namespace violet {

TextureManager::~TextureManager() {
    cleanup();
}

void TextureManager::init(VulkanContext* ctx, DescriptorManager* descMgr) {
    context = ctx;
    descriptorManager = descMgr;

    textureSlots.reserve(128);
    freeSlots.reserve(32);

    violet::Log::info("TextureManager", "Initialized");
}

void TextureManager::cleanup() {
    for (auto& slot : textureSlots) {
        if (slot.inUse && slot.texture) {
            slot.texture.reset();
        }
    }
    textureSlots.clear();
    freeSlots.clear();

    defaultTextures = {};
    defaultResourcesCreated = false;

    violet::Log::info("TextureManager", "Cleaned up all textures");
}

TextureHandle TextureManager::addTexture(eastl::unique_ptr<Texture> texture) {
    if (!texture) {
        return TextureHandle{};
    }

    uint32_t index;
    if (!freeSlots.empty()) {
        index = freeSlots.back();
        freeSlots.pop_back();
    } else {
        index = nextSlot++;
        textureSlots.resize(nextSlot);
    }

    TextureSlot& slot = textureSlots[index];
    slot.texture = eastl::move(texture);
    slot.generation++;
    slot.inUse = true;

    return TextureHandle{index, slot.generation};
}

void TextureManager::removeTexture(TextureHandle handle) {
    if (!isValidHandle(handle)) {
        return;
    }

    TextureSlot& slot = textureSlots[handle.index];
    slot.texture.reset();
    slot.inUse = false;
    freeSlots.push_back(handle.index);
}

Texture* TextureManager::getTexture(TextureHandle handle) {
    if (!isValidHandle(handle)) {
        return nullptr;
    }
    return textureSlots[handle.index].texture.get();
}

const Texture* TextureManager::getTexture(TextureHandle handle) const {
    if (!isValidHandle(handle)) {
        return nullptr;
    }
    return textureSlots[handle.index].texture.get();
}

bool TextureManager::isValidHandle(TextureHandle handle) const {
    if (!handle.isValid() || handle.index >= textureSlots.size()) {
        return false;
    }
    const TextureSlot& slot = textureSlots[handle.index];
    return slot.inUse && slot.generation == handle.generation;
}

size_t TextureManager::getTextureCount() const {
    size_t count = 0;
    for (const auto& slot : textureSlots) {
        if (slot.inUse) {
            count++;
        }
    }
    return count;
}

// Default textures

void TextureManager::createDefaultResources() {
    if (defaultResourcesCreated) {
        return;
    }

    createDefaultWhiteTexture();
    createDefaultBlackTexture();
    createDefaultNormalTexture();
    createDefaultMetallicRoughnessTexture();

    // Register default textures in bindless array if enabled
    if (descriptorManager->isBindlessEnabled()) {
        Texture* white = getTexture(defaultTextures.white);
        if (white) {
            uint32_t whiteTexIndex = descriptorManager->allocateBindlessTexture(white);
            violet::Log::info("TextureManager", "Registered default white texture at bindless index {}", whiteTexIndex);
        }
    }

    defaultResourcesCreated = true;
    violet::Log::info("TextureManager", "Created default resources");
}

void TextureManager::createDefaultWhiteTexture() {
    const uint8_t white[] = {255, 255, 255, 255};
    auto texture = eastl::make_unique<Texture>();
    texture->loadFromMemory(context, white, sizeof(white), 1, 1, 4, false);
    texture->setSampler(descriptorManager->getSampler(SamplerType::Default));
    defaultTextures.white = addTexture(eastl::move(texture));
}

void TextureManager::createDefaultBlackTexture() {
    const uint8_t black[] = {0, 0, 0, 255};
    auto texture = eastl::make_unique<Texture>();
    texture->loadFromMemory(context, black, sizeof(black), 1, 1, 4, false);
    texture->setSampler(descriptorManager->getSampler(SamplerType::Default));
    defaultTextures.black = addTexture(eastl::move(texture));
}

void TextureManager::createDefaultNormalTexture() {
    const uint8_t normal[] = {128, 128, 255, 255};  // R=0.5, G=0.5, B=1.0
    auto texture = eastl::make_unique<Texture>();
    texture->loadFromMemory(context, normal, sizeof(normal), 1, 1, 4, false);
    texture->setSampler(descriptorManager->getSampler(SamplerType::Default));
    defaultTextures.normal = addTexture(eastl::move(texture));
}

void TextureManager::createDefaultMetallicRoughnessTexture() {
    const uint8_t metallicRoughness[] = {255, 128, 0, 255};  // R=1.0 (roughness), G=0.5 (metallic)
    auto texture = eastl::make_unique<Texture>();
    texture->loadFromMemory(context, metallicRoughness, sizeof(metallicRoughness), 1, 1, 4, false);
    texture->setSampler(descriptorManager->getSampler(SamplerType::Default));
    defaultTextures.metallicRoughness = addTexture(eastl::move(texture));
}

TextureHandle TextureManager::getDefaultTextureHandle(DefaultTextureType type) const {
    switch (type) {
        case DefaultTextureType::White:
            return defaultTextures.white;
        case DefaultTextureType::Black:
            return defaultTextures.black;
        case DefaultTextureType::Normal:
            return defaultTextures.normal;
        case DefaultTextureType::MetallicRoughness:
            return defaultTextures.metallicRoughness;
        default:
            return TextureHandle{};
    }
}

Texture* TextureManager::getDefaultTexture(DefaultTextureType type) const {
    TextureHandle handle = getDefaultTextureHandle(type);
    if (!handle.isValid()) {
        return nullptr;
    }
    return const_cast<Texture*>(getTexture(handle));
}

} // namespace violet
