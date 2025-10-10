#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "resource/gpu/UniformBuffer.hpp"
#include "resource/Texture.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "core/Log.hpp"
#include <functional>
#include <EASTL/algorithm.h>

namespace violet {

// ===== SamplerConfig Implementation =====

size_t SamplerConfig::hash() const {
    size_t h = 0;
    h ^= std::hash<int>{}(static_cast<int>(magFilter)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(minFilter)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(addressModeU)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(addressModeV)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(addressModeW)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(mipmapMode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(minLod) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(maxLod) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(anisotropyEnable) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(maxAnisotropy) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(borderColor)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(compareEnable) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(compareOp)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

bool SamplerConfig::operator==(const SamplerConfig& other) const {
    return magFilter == other.magFilter &&
           minFilter == other.minFilter &&
           addressModeU == other.addressModeU &&
           addressModeV == other.addressModeV &&
           addressModeW == other.addressModeW &&
           mipmapMode == other.mipmapMode &&
           minLod == other.minLod &&
           maxLod == other.maxLod &&
           mipLodBias == other.mipLodBias &&
           anisotropyEnable == other.anisotropyEnable &&
           maxAnisotropy == other.maxAnisotropy &&
           borderColor == other.borderColor &&
           compareEnable == other.compareEnable &&
           compareOp == other.compareOp;
}

SamplerConfig SamplerConfig::getDefault(float maxAnisotropy) {
    SamplerConfig config;
    config.magFilter = vk::Filter::eLinear;
    config.minFilter = vk::Filter::eLinear;
    config.addressModeU = vk::SamplerAddressMode::eRepeat;
    config.addressModeV = vk::SamplerAddressMode::eRepeat;
    config.addressModeW = vk::SamplerAddressMode::eRepeat;
    config.mipmapMode = vk::SamplerMipmapMode::eLinear;
    config.anisotropyEnable = true;
    config.maxAnisotropy = maxAnisotropy;
    return config;
}

SamplerConfig SamplerConfig::getClampToEdge() {
    SamplerConfig config;
    config.magFilter = vk::Filter::eLinear;
    config.minFilter = vk::Filter::eLinear;
    config.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    config.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    config.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    config.mipmapMode = vk::SamplerMipmapMode::eLinear;
    config.anisotropyEnable = false;
    config.maxAnisotropy = 1.0f;
    return config;
}

SamplerConfig SamplerConfig::getNearest() {
    SamplerConfig config;
    config.magFilter = vk::Filter::eNearest;
    config.minFilter = vk::Filter::eNearest;
    config.addressModeU = vk::SamplerAddressMode::eRepeat;
    config.addressModeV = vk::SamplerAddressMode::eRepeat;
    config.addressModeW = vk::SamplerAddressMode::eRepeat;
    config.mipmapMode = vk::SamplerMipmapMode::eNearest;
    config.anisotropyEnable = false;
    config.maxAnisotropy = 1.0f;
    return config;
}

SamplerConfig SamplerConfig::getShadow() {
    SamplerConfig config;
    config.magFilter = vk::Filter::eLinear;
    config.minFilter = vk::Filter::eLinear;
    config.addressModeU = vk::SamplerAddressMode::eClampToBorder;
    config.addressModeV = vk::SamplerAddressMode::eClampToBorder;
    config.addressModeW = vk::SamplerAddressMode::eClampToBorder;
    config.borderColor = vk::BorderColor::eFloatOpaqueWhite;
    config.compareEnable = true;
    config.compareOp = vk::CompareOp::eLessOrEqual;
    config.anisotropyEnable = false;
    config.maxAnisotropy = 1.0f;
    return config;
}

SamplerConfig SamplerConfig::getCubemap() {
    SamplerConfig config;
    config.magFilter = vk::Filter::eLinear;
    config.minFilter = vk::Filter::eLinear;
    config.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    config.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    config.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    config.mipmapMode = vk::SamplerMipmapMode::eLinear;
    config.anisotropyEnable = false;
    config.maxAnisotropy = 1.0f;
    return config;
}

SamplerConfig SamplerConfig::getNearestClamp() {
    SamplerConfig config;
    config.magFilter = vk::Filter::eNearest;
    config.minFilter = vk::Filter::eNearest;
    config.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    config.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    config.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    config.mipmapMode = vk::SamplerMipmapMode::eNearest;
    config.anisotropyEnable = false;
    config.maxAnisotropy = 1.0f;
    return config;
}

// Helper constructors for ResourceBindingDesc
ResourceBindingDesc ResourceBindingDesc::uniformBuffer(uint32_t binding, UniformBuffer* buffer) {
    ResourceBindingDesc desc;
    desc.binding = binding;
    desc.type = vk::DescriptorType::eUniformBuffer;
    desc.bufferPtr = buffer;
    return desc;
}

ResourceBindingDesc ResourceBindingDesc::texture(uint32_t binding, Texture* texture) {
    ResourceBindingDesc desc;
    desc.binding = binding;
    desc.type = vk::DescriptorType::eCombinedImageSampler;
    desc.texturePtr = texture;
    desc.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    return desc;
}

ResourceBindingDesc ResourceBindingDesc::storageImage(uint32_t binding, vk::ImageView imageView) {
    ResourceBindingDesc desc;
    desc.binding = binding;
    desc.type = vk::DescriptorType::eStorageImage;
    desc.storageImageView = imageView;
    desc.imageLayout = vk::ImageLayout::eGeneral;
    return desc;
}

ResourceBindingDesc ResourceBindingDesc::sampledImage(uint32_t binding, vk::ImageView imageView, vk::Sampler sampler) {
    ResourceBindingDesc desc;
    desc.binding = binding;
    desc.type = vk::DescriptorType::eCombinedImageSampler;
    desc.imageInfo.imageView = imageView;
    desc.imageInfo.sampler = sampler;
    desc.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    return desc;
}

// DescriptorManager implementation
void DescriptorManager::init(VulkanContext* ctx, uint32_t maxFramesInFlight) {
    context = ctx;
    maxFrames = maxFramesInFlight;

    violet::Log::info("Renderer", "DescriptorManager initialized with {} frames", maxFrames);
}

void DescriptorManager::cleanup() {
    auto device = context->getDevice();

    // Clean up samplers
    for (auto& [hash, sampler] : samplerCache) {
        device.destroySampler(sampler);
    }
    samplerCache.clear();
    predefinedSamplers.clear();
    violet::Log::info("Renderer", "Destroyed {} cached samplers", samplerCache.size());

    // Clean up material data buffer
    if (materialDataEnabled && materialDataBuffer.buffer) {
        ResourceFactory::destroyBuffer(context, materialDataBuffer);
        materialDataEnabled = false;
        materialDataMapped = nullptr;
        violet::Log::info("Renderer", "MaterialDataBuffer destroyed");
    }

    // Clean up all pools
    for (auto& [frequency, pools] : poolsByFrequency) {
        for (auto& poolInfo : pools) {
            if (poolInfo.pool) {
                device.destroyDescriptorPool(poolInfo.pool);
            }
        }
    }
    poolsByFrequency.clear();

    // Clean up all layouts
    for (auto& [name, layoutInfo] : layouts) {
        if (layoutInfo.layout) {
            device.destroyDescriptorSetLayout(layoutInfo.layout);
        }
    }
    layouts.clear();

    violet::Log::info("Renderer", "DescriptorManager cleaned up");
}

void DescriptorManager::registerLayout(const DescriptorLayoutDesc& desc) {
    if (layouts.find(desc.name) != layouts.end()) {
        violet::Log::warn("Renderer", "Descriptor layout '{}' already registered, skipping", desc.name.c_str());
        return;
    }

    // Convert bindings to Vulkan format
    eastl::vector<vk::DescriptorSetLayoutBinding> vkBindings;
    eastl::vector<vk::DescriptorPoolSize> poolSizes;

    for (const auto& binding : desc.bindings) {
        vk::DescriptorSetLayoutBinding vkBinding;
        vkBinding.binding = binding.binding;
        vkBinding.descriptorType = binding.type;
        vkBinding.descriptorCount = binding.count;
        vkBinding.stageFlags = binding.stages;
        vkBinding.pImmutableSamplers = nullptr;
        vkBindings.push_back(vkBinding);

        // Calculate pool sizes
        bool found = false;
        for (auto& poolSize : poolSizes) {
            if (poolSize.type == binding.type) {
                poolSize.descriptorCount += binding.count;
                found = true;
                break;
            }
        }
        if (!found) {
            poolSizes.push_back({binding.type, binding.count});
        }
    }

    // Create layout
    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.flags = desc.flags;
    layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
    layoutInfo.pBindings = vkBindings.data();

    // Add binding flags if specified (for bindless)
    vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo;
    eastl::vector<vk::DescriptorBindingFlags> bindingFlagsArray;
    if (desc.bindingFlags) {
        bindingFlagsArray.resize(vkBindings.size(), desc.bindingFlags);
        bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlagsArray.size());
        bindingFlagsInfo.pBindingFlags = bindingFlagsArray.data();
        layoutInfo.pNext = &bindingFlagsInfo;
    }

    vk::DescriptorSetLayout layout = context->getDevice().createDescriptorSetLayout(layoutInfo);

    // Store layout info
    LayoutInfo info;
    info.layout = layout;
    info.frequency = desc.frequency;
    info.poolSizes = poolSizes;
    info.createFlags = desc.flags;
    layouts[desc.name] = info;

    violet::Log::info("Renderer", "Registered descriptor layout '{}' with {} bindings", desc.name.c_str(), desc.bindings.size());
}

vk::DescriptorSet DescriptorManager::allocateSet(const eastl::string& layoutName, uint32_t frameIndex) {
    auto it = layouts.find(layoutName);
    if (it == layouts.end()) {
        violet::Log::error("Renderer", "Descriptor layout '{}' not found", layoutName.c_str());
        return nullptr;
    }

    const LayoutInfo& layoutInfo = it->second;
    vk::DescriptorPool pool = getOrCreatePool(layoutInfo.frequency);

    // Allocate descriptor set
    vk::DescriptorSetLayout layoutArray[] = {layoutInfo.layout};
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = layoutArray;

    auto sets = context->getDevice().allocateDescriptorSets(allocInfo);

    // Update remaining sets count
    auto& pools = poolsByFrequency[layoutInfo.frequency];
    for (auto& poolInfo : pools) {
        if (poolInfo.pool == pool) {
            poolInfo.remainingSets--;
            break;
        }
    }

    return sets[0];
}

eastl::vector<vk::DescriptorSet> DescriptorManager::allocateSets(const eastl::string& layoutName, uint32_t count) {
    auto it = layouts.find(layoutName);
    if (it == layouts.end()) {
        violet::Log::error("Renderer", "Descriptor layout '{}' not found", layoutName.c_str());
        return {};
    }

    const LayoutInfo& layoutInfo = it->second;
    vk::DescriptorPool pool = getOrCreatePool(layoutInfo.frequency);

    // Allocate multiple descriptor sets
    eastl::vector<vk::DescriptorSetLayout> layoutArray(count, layoutInfo.layout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = count;
    allocInfo.pSetLayouts = layoutArray.data();

    auto stdSets = context->getDevice().allocateDescriptorSets(allocInfo);

    // Update remaining sets count
    auto& pools = poolsByFrequency[layoutInfo.frequency];
    for (auto& poolInfo : pools) {
        if (poolInfo.pool == pool) {
            poolInfo.remainingSets -= count;
            break;
        }
    }

    // Convert std::vector to eastl::vector
    eastl::vector<vk::DescriptorSet> sets;
    sets.reserve(stdSets.size());
    for (const auto& set : stdSets) {
        sets.push_back(set);
    }

    return sets;
}

void DescriptorManager::updateSet(vk::DescriptorSet set, const eastl::vector<ResourceBindingDesc>& bindings) {
    if (!set) {
        violet::Log::error("Renderer", "Cannot update null descriptor set");
        return;
    }

    eastl::vector<vk::WriteDescriptorSet> writes;
    eastl::vector<vk::DescriptorBufferInfo> bufferInfos;
    eastl::vector<vk::DescriptorImageInfo> imageInfos;

    // Reserve space to avoid reallocation
    bufferInfos.reserve(bindings.size());
    imageInfos.reserve(bindings.size());
    writes.reserve(bindings.size());

    for (const auto& binding : bindings) {
        vk::WriteDescriptorSet write;
        write.dstSet = set;
        write.dstBinding = binding.binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = binding.type;

        switch (binding.type) {
            case vk::DescriptorType::eUniformBuffer: {
                if (binding.bufferPtr) {
                    bufferInfos.push_back(binding.bufferPtr->getDescriptorInfo());
                    write.pBufferInfo = &bufferInfos.back();
                }
                break;
            }
            case vk::DescriptorType::eCombinedImageSampler: {
                vk::DescriptorImageInfo imageInfo;
                if (binding.texturePtr) {
                    imageInfo.imageLayout = binding.imageLayout;
                    imageInfo.imageView = binding.texturePtr->getImageView();
                    imageInfo.sampler = binding.texturePtr->getSampler();
                } else {
                    imageInfo.imageLayout = binding.imageLayout;
                    imageInfo.imageView = binding.imageInfo.imageView;
                    imageInfo.sampler = binding.imageInfo.sampler;
                }
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
                break;
            }
            case vk::DescriptorType::eStorageImage: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = binding.imageLayout;
                imageInfo.imageView = binding.storageImageView;
                imageInfo.sampler = nullptr;
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
                break;
            }
            default:
                violet::Log::warn("Renderer", "Unsupported descriptor type in updateSet");
                continue;
        }

        writes.push_back(write);
    }

    if (!writes.empty()) {
        context->getDevice().updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

vk::DescriptorSetLayout DescriptorManager::getLayout(const eastl::string& layoutName) const {
    auto it = layouts.find(layoutName);
    if (it == layouts.end()) {
        violet::Log::error("Renderer", "Descriptor layout '{}' not found", layoutName.c_str());
        return nullptr;
    }

    vk::DescriptorSetLayout layout = it->second.layout;
    if (!layout) {
        violet::Log::error("Renderer", "Descriptor layout '{}' found but handle is null", layoutName.c_str());
    }

    return layout;
}

bool DescriptorManager::hasLayout(const eastl::string& layoutName) const {
    return layouts.find(layoutName) != layouts.end();
}

void DescriptorManager::initBindless(uint32_t maxTextures) {
    if (!hasLayout("Bindless")) {
        violet::Log::error("Renderer", "Bindless layout not registered - call registerLayout() first");
        return;
    }

    bindlessMaxTextures = maxTextures;
    bindlessTextureSlots.resize(maxTextures, nullptr);

    // 预留前5个索引给默认纹理:
    // Index 0: 保留为nullptr标记（shader中的"无纹理"检测）
    // Index 1: White texture
    // Index 2: Black texture
    // Index 3: Normal texture
    // Index 4: MetallicRoughness texture
    // Index 5+: 动态分配
    bindlessFreeIndices.reserve(maxTextures - 5);
    for (uint32_t i = 5; i < maxTextures; ++i) {
        bindlessFreeIndices.push_back(i);
    }

    // Initialize cubemap arrays (binding 1)
    bindlessCubemapSlots.resize(bindlessMaxCubemaps, nullptr);
    bindlessCubemapFreeIndices.reserve(bindlessMaxCubemaps);
    for (uint32_t i = 0; i < bindlessMaxCubemaps; ++i) {
        bindlessCubemapFreeIndices.push_back(i);
    }

    // Allocate bindless descriptor set
    bindlessSet = allocateSet("Bindless", 0);
    bindlessEnabled = true;

    violet::Log::info("Renderer", "DescriptorManager bindless initialized with {} max 2D textures and {} max cubemaps", maxTextures, bindlessMaxCubemaps);
}

uint32_t DescriptorManager::allocateBindlessTexture(Texture* texture) {
    if (!bindlessEnabled) {
        violet::Log::error("Renderer", "Bindless not enabled - call initBindless() first");
        return 0;
    }

    if (bindlessFreeIndices.empty()) {
        violet::Log::error("Renderer", "Bindless texture array is full (max: {})", bindlessMaxTextures);
        return 0;
    }

    // Get a free index
    uint32_t index = bindlessFreeIndices.back();
    bindlessFreeIndices.pop_back();

    bindlessTextureSlots[index] = texture;

    // Update descriptor set
    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = texture->getImageView();
    imageInfo.sampler = texture->getSampler();

    vk::WriteDescriptorSet write;
    write.dstSet = bindlessSet;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &imageInfo;

    context->getDevice().updateDescriptorSets(1, &write, 0, nullptr);

    return index;
}

uint32_t DescriptorManager::allocateBindlessTextureAt(Texture* texture, uint32_t index) {
    if (!bindlessEnabled) {
        violet::Log::error("Renderer", "Bindless not enabled - call initBindless() first");
        return 0;
    }

    if (index >= bindlessMaxTextures) {
        violet::Log::error("Renderer", "Invalid bindless texture index: {}", index);
        return 0;
    }

    if (bindlessTextureSlots[index] != nullptr) {
        violet::Log::warn("Renderer", "Bindless texture slot {} already occupied, overwriting", index);
    }

    bindlessTextureSlots[index] = texture;

    // 从freeIndices中移除这个索引（如果存在）
    auto it = eastl::find(bindlessFreeIndices.begin(), bindlessFreeIndices.end(), index);
    if (it != bindlessFreeIndices.end()) {
        bindlessFreeIndices.erase(it);
    }

    // 更新descriptor set
    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = texture->getImageView();
    imageInfo.sampler = texture->getSampler();

    vk::WriteDescriptorSet write;
    write.dstSet = bindlessSet;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &imageInfo;

    context->getDevice().updateDescriptorSets(1, &write, 0, nullptr);

    return index;
}

void DescriptorManager::freeBindlessTexture(uint32_t index) {
    if (index >= bindlessMaxTextures) {
        violet::Log::error("Renderer", "Invalid bindless texture index: {}", index);
        return;
    }

    if (bindlessTextureSlots[index] == nullptr) {
        violet::Log::warn("Renderer", "Attempting to free already-freed bindless texture at index {}", index);
        return;
    }

    bindlessTextureSlots[index] = nullptr;
    bindlessFreeIndices.push_back(index);
}

uint32_t DescriptorManager::allocateBindlessCubemap(Texture* cubemapTexture) {
    if (!bindlessEnabled) {
        violet::Log::error("Renderer", "Bindless not enabled - call initBindless() first");
        return 0;
    }

    if (bindlessCubemapFreeIndices.empty()) {
        violet::Log::error("Renderer", "Bindless cubemap array is full (max: {})", bindlessMaxCubemaps);
        return 0;
    }

    // Get a free index
    uint32_t index = bindlessCubemapFreeIndices.back();
    bindlessCubemapFreeIndices.pop_back();

    bindlessCubemapSlots[index] = cubemapTexture;

    // Update descriptor set (binding 1 for cubemaps)
    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = cubemapTexture->getImageView();
    imageInfo.sampler = cubemapTexture->getSampler();

    vk::WriteDescriptorSet write;
    write.dstSet = bindlessSet;
    write.dstBinding = 1;  // Cubemaps use binding 1
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &imageInfo;

    context->getDevice().updateDescriptorSets(1, &write, 0, nullptr);

    return index;
}

void DescriptorManager::freeBindlessCubemap(uint32_t index) {
    if (index >= bindlessMaxCubemaps) {
        violet::Log::error("Renderer", "Invalid bindless cubemap index: {}", index);
        return;
    }

    if (bindlessCubemapSlots[index] == nullptr) {
        violet::Log::warn("Renderer", "Attempting to free already-freed bindless cubemap at index {}", index);
        return;
    }

    bindlessCubemapSlots[index] = nullptr;
    bindlessCubemapFreeIndices.push_back(index);
}

vk::DescriptorSet DescriptorManager::getBindlessSet() const {
    return bindlessSet;
}

void DescriptorManager::createPool(UpdateFrequency frequency) {
    // Determine pool size based on frequency
    uint32_t poolSizeMultiplier;
    switch (frequency) {
        case UpdateFrequency::PerFrame:   poolSizeMultiplier = POOL_SIZE_PER_FRAME; break;
        case UpdateFrequency::PerPass:    poolSizeMultiplier = POOL_SIZE_PER_PASS; break;
        case UpdateFrequency::PerMaterial: poolSizeMultiplier = POOL_SIZE_PER_MATERIAL; break;
        case UpdateFrequency::Static:     poolSizeMultiplier = POOL_SIZE_STATIC; break;
        default:                          poolSizeMultiplier = POOL_SIZE_PER_MATERIAL; break;
    }

    // Collect pool sizes from all layouts with this frequency
    eastl::vector<vk::DescriptorPoolSize> poolSizes;
    for (const auto& [name, layoutInfo] : layouts) {
        if (layoutInfo.frequency == frequency) {
            for (const auto& size : layoutInfo.poolSizes) {
                bool found = false;
                for (auto& poolSize : poolSizes) {
                    if (poolSize.type == size.type) {
                        poolSize.descriptorCount += size.descriptorCount * poolSizeMultiplier * maxFrames;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    poolSizes.push_back({size.type, size.descriptorCount * poolSizeMultiplier * maxFrames});
                }
            }
        }
    }

    if (poolSizes.empty()) {
        violet::Log::warn("Renderer", "No layouts registered for frequency {}, skipping pool creation", static_cast<int>(frequency));
        return;
    }

    // Determine pool flags based on frequency and layout flags
    vk::DescriptorPoolCreateFlags poolFlags = {};
    for (const auto& [name, layoutInfo] : layouts) {
        if (layoutInfo.frequency == frequency) {
            if (layoutInfo.createFlags & vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool) {
                poolFlags |= vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
            }
        }
    }

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = poolFlags;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = poolSizeMultiplier * maxFrames;

    vk::DescriptorPool pool = context->getDevice().createDescriptorPool(poolInfo);

    PoolInfo info;
    info.pool = pool;
    info.maxSets = poolSizeMultiplier * maxFrames;
    info.remainingSets = info.maxSets;

    poolsByFrequency[frequency].push_back(info);

    violet::Log::info("Renderer", "Created descriptor pool for frequency {} with {} max sets", static_cast<int>(frequency), info.maxSets);
}

void DescriptorManager::growPool(UpdateFrequency frequency) {
    violet::Log::info("Renderer", "Growing descriptor pool for frequency {}", static_cast<int>(frequency));
    createPool(frequency);
}

vk::DescriptorPool DescriptorManager::getOrCreatePool(UpdateFrequency frequency) {
    auto it = poolsByFrequency.find(frequency);

    // No pool exists for this frequency
    if (it == poolsByFrequency.end()) {
        createPool(frequency);
        return poolsByFrequency[frequency].back().pool;
    }

    // Find a pool with available sets
    for (auto& poolInfo : it->second) {
        if (poolInfo.remainingSets > 0) {
            return poolInfo.pool;
        }
    }

    // All pools are full, grow
    growPool(frequency);
    return poolsByFrequency[frequency].back().pool;
}

// Material data SSBO management
bool DescriptorManager::initMaterialDataBuffer(uint32_t maxMaterials) {
    if (!hasLayout("MaterialData")) {
        violet::Log::error("Renderer", "MaterialData layout not registered - call registerLayout() first");
        return false;
    }

    maxMaterialData = maxMaterials;
    materialDataSlots.resize(maxMaterials);
    materialDataFreeIndices.reserve(maxMaterials);
    for (uint32_t i = 0; i < maxMaterials; ++i) {
        materialDataFreeIndices.push_back(i);
    }

    // Create SSBO using ResourceFactory
    BufferInfo bufferInfo{
        .size = sizeof(MaterialData) * maxMaterials,
        .usage = vk::BufferUsageFlagBits::eStorageBuffer,
        .memoryUsage = MemoryUsage::CPU_TO_GPU,
        .debugName = "MaterialDataSSBO"
    };
    materialDataBuffer = ResourceFactory::createBuffer(context, bufferInfo);

    // Get persistent mapped pointer from VMA
    materialDataMapped = materialDataBuffer.mappedData;
    if (!materialDataMapped) {
        violet::Log::error("Renderer", "Failed to get mapped pointer for MaterialDataSSBO");
        return false;
    }

    // Allocate descriptor set
    materialDataSet = allocateSet("MaterialData", 0);
    if (!materialDataSet) {
        violet::Log::error("Renderer", "Failed to allocate MaterialData descriptor set");
        return false;
    }

    // Update descriptor set with SSBO
    vk::DescriptorBufferInfo bufferDescInfo;
    bufferDescInfo.buffer = materialDataBuffer.buffer;
    bufferDescInfo.offset = 0;
    bufferDescInfo.range = sizeof(MaterialData) * maxMaterials;

    vk::WriteDescriptorSet write;
    write.dstSet = materialDataSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eStorageBuffer;
    write.pBufferInfo = &bufferDescInfo;

    context->getDevice().updateDescriptorSets(1, &write, 0, nullptr);

    materialDataEnabled = true;
    violet::Log::info("Renderer", "MaterialDataBuffer initialized with {} max materials", maxMaterials);
    return true;
}

uint32_t DescriptorManager::allocateMaterialData(const MaterialData& data) {
    if (!materialDataEnabled) {
        violet::Log::error("Renderer", "Material data buffer not enabled - call initMaterialDataBuffer() first");
        return 0;
    }

    if (materialDataFreeIndices.empty()) {
        violet::Log::error("Renderer", "Material data buffer is full (max: {})", maxMaterialData);
        return 0;
    }

    // Get a free index (start from 1, 0 is reserved for error)
    uint32_t index = materialDataFreeIndices.back();
    materialDataFreeIndices.pop_back();

    // Store data in CPU cache
    materialDataSlots[index] = data;

    // Write to GPU mapped memory
    MaterialData* gpuData = static_cast<MaterialData*>(materialDataMapped);
    gpuData[index] = data;

    return index;
}

bool DescriptorManager::updateMaterialData(uint32_t index, const MaterialData& data) {
    if (index >= maxMaterialData) {
        violet::Log::error("Renderer", "Invalid material data index: {}", index);
        return false;
    }

    if (!materialDataEnabled || !materialDataMapped) {
        violet::Log::error("Renderer", "Material data buffer not initialized");
        return false;
    }

    // Update CPU cache
    materialDataSlots[index] = data;

    // Write to GPU mapped memory
    MaterialData* gpuData = static_cast<MaterialData*>(materialDataMapped);
    gpuData[index] = data;

    return true;
}

bool DescriptorManager::freeMaterialData(uint32_t index) {
    if (index >= maxMaterialData) {
        violet::Log::error("Renderer", "Invalid material data index: {}", index);
        return false;
    }

    // Reset to default
    materialDataSlots[index] = MaterialData{};
    materialDataFreeIndices.push_back(index);

    return true;
}

const DescriptorManager::MaterialData* DescriptorManager::getMaterialData(uint32_t index) const {
    if (index >= maxMaterialData) {
        return nullptr;
    }
    return &materialDataSlots[index];
}

// ===== Sampler Management =====

vk::Sampler DescriptorManager::createSampler(const SamplerConfig& config) {
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = config.magFilter;
    samplerInfo.minFilter = config.minFilter;
    samplerInfo.addressModeU = config.addressModeU;
    samplerInfo.addressModeV = config.addressModeV;
    samplerInfo.addressModeW = config.addressModeW;
    samplerInfo.mipmapMode = config.mipmapMode;
    samplerInfo.minLod = config.minLod;
    samplerInfo.maxLod = config.maxLod;
    samplerInfo.mipLodBias = config.mipLodBias;
    samplerInfo.anisotropyEnable = config.anisotropyEnable;
    samplerInfo.maxAnisotropy = config.maxAnisotropy;
    samplerInfo.borderColor = config.borderColor;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = config.compareEnable;
    samplerInfo.compareOp = config.compareOp;

    return context->getDevice().createSampler(samplerInfo);
}

vk::Sampler DescriptorManager::getOrCreateSampler(const SamplerConfig& config) {
    size_t configHash = config.hash();

    // Check cache first
    auto it = samplerCache.find(configHash);
    if (it != samplerCache.end()) {
        return it->second;
    }

    // Create new sampler and cache it
    vk::Sampler sampler = createSampler(config);
    samplerCache[configHash] = sampler;

    violet::Log::debug("Renderer", "Created and cached new sampler (hash: {}, total: {})",
                      configHash, samplerCache.size());

    return sampler;
}

vk::Sampler DescriptorManager::getSampler(SamplerType type) {
    // Check if predefined sampler already exists
    auto it = predefinedSamplers.find(type);
    if (it != predefinedSamplers.end()) {
        return it->second;
    }

    // Create sampler based on type
    SamplerConfig config;
    vk::PhysicalDeviceProperties properties = context->getPhysicalDevice().getProperties();

    switch (type) {
        case SamplerType::Default:
            config = SamplerConfig::getDefault(properties.limits.maxSamplerAnisotropy);
            break;
        case SamplerType::ClampToEdge:
            config = SamplerConfig::getClampToEdge();
            break;
        case SamplerType::Nearest:
            config = SamplerConfig::getNearest();
            break;
        case SamplerType::Shadow:
            config = SamplerConfig::getShadow();
            break;
        case SamplerType::Cubemap:
            config = SamplerConfig::getCubemap();
            break;
        case SamplerType::NearestClamp:
            config = SamplerConfig::getNearestClamp();
            break;
        default:
            violet::Log::warn("Renderer", "Unknown sampler type, using default");
            config = SamplerConfig::getDefault(properties.limits.maxSamplerAnisotropy);
            break;
    }

    vk::Sampler sampler = getOrCreateSampler(config);
    predefinedSamplers[type] = sampler;

    violet::Log::info("Renderer", "Created predefined sampler type {}", static_cast<int>(type));

    return sampler;
}

} // namespace violet
