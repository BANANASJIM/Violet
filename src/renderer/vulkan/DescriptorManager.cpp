#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "resource/gpu/UniformBuffer.hpp"
#include "resource/Texture.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/shader/Shader.hpp"
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

// ===== DescriptorLayoutDesc Implementation =====

LayoutHandle DescriptorLayoutDesc::hash() const {
    uint32_t h = 0;

    // Hash frequency and flags
    h ^= std::hash<int>{}(static_cast<int>(frequency)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(flags)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(isBindless) + 0x9e3779b9 + (h << 6) + (h >> 2);

    // Hash each binding (包括per-binding flags)
    for (const auto& binding : bindings) {
        h ^= std::hash<uint32_t>{}(binding.binding) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(static_cast<int>(binding.type)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(binding.stages)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(binding.count) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(binding.flags)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }

    return h;
}

PushConstantHandle PushConstantDesc::hash() const {
    uint32_t h = 0;

    // Hash each push constant range
    for (const auto& range : ranges) {
        h ^= std::hash<uint32_t>{}(range.offset) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(range.size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(range.stageFlags)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }

    return h != 0 ? h : 1;  // Ensure we never return 0 (reserved for "no push constants")
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

ResourceBindingDesc ResourceBindingDesc::storageBuffer(uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range) {
    ResourceBindingDesc desc;
    desc.binding = binding;
    desc.type = vk::DescriptorType::eStorageBuffer;
    desc.storageBufferInfo.buffer = buffer;
    desc.storageBufferInfo.offset = offset;
    desc.storageBufferInfo.range = range;
    return desc;
}

ResourceBindingDesc ResourceBindingDesc::texture(uint32_t binding, Texture* texture) {
    ResourceBindingDesc desc;
    desc.binding = binding;
    desc.type = vk::DescriptorType::eCombinedImageSampler;
    desc.usesRawImageView = false;  // Using Texture*
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
    desc.usesRawImageView = true;  // Using raw ImageView/Sampler
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

    for (auto& [hash, sampler] : samplerCache) {
        device.destroySampler(sampler);
    }
    samplerCache.clear();
    predefinedSamplers.clear();

    if (materialDataEnabled && materialDataBuffer.buffer) {
        ResourceFactory::destroyBuffer(context, materialDataBuffer);
        materialDataEnabled = false;
        materialDataMapped = nullptr;
    }

    for (auto& [frequency, pools] : poolsByFrequency) {
        for (auto& poolInfo : pools) {
            if (poolInfo.pool) {
                device.destroyDescriptorPool(poolInfo.pool);
            }
        }
    }
    poolsByFrequency.clear();

    for (auto& [handle, layoutInfo] : layouts) {
        if (layoutInfo.layout) {
            device.destroyDescriptorSetLayout(layoutInfo.layout);
        }
    }
    layouts.clear();
    nameToHandle.clear();

    violet::Log::info("Renderer", "DescriptorManager cleaned up");
}

LayoutHandle DescriptorManager::registerLayout(const DescriptorLayoutDesc& desc) {
    LayoutHandle handle = desc.hash();

    if (layouts.find(handle) != layouts.end()) {
        violet::Log::debug("Renderer", "Descriptor layout '{}' (hash={}) already registered, reusing",
                          desc.name.c_str(), handle);
        return handle;
    }

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

    // Collect per-binding flags
    eastl::vector<vk::DescriptorBindingFlags> bindingFlagsArray;
    bindingFlagsArray.reserve(desc.bindings.size());
    for (const auto& binding : desc.bindings) {
        bindingFlagsArray.push_back(binding.flags);
    }

    // Create layout with per-binding flags
    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.flags = desc.flags;
    layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
    layoutInfo.pBindings = vkBindings.data();

    // Chain binding flags if any bindless bindings exist
    vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo;
    if (desc.isBindless && !bindingFlagsArray.empty()) {
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
    layouts[handle] = info;

    // Store name->handle mapping for legacy API
    if (!desc.name.empty()) {
        nameToHandle[desc.name] = handle;
    }

    violet::Log::info("Renderer", "Registered descriptor layout '{}' (hash={}) with {} bindings",
                     desc.name.c_str(), handle, desc.bindings.size());
    return handle;
}

vk::DescriptorSet DescriptorManager::allocateSet(LayoutHandle handle, uint32_t frameIndex) {
    auto it = layouts.find(handle);
    if (it == layouts.end()) {
        violet::Log::error("Renderer", "Descriptor layout handle {} not found", handle);
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
                // Check usesRawImageView flag to distinguish between Texture* and raw ImageView/Sampler
                if (!binding.usesRawImageView && binding.texturePtr) {
                    // Use Texture*
                    imageInfo.imageLayout = binding.imageLayout;
                    imageInfo.imageView = binding.texturePtr->getImageView();
                    imageInfo.sampler = binding.texturePtr->getSampler();
                } else {
                    // Use raw ImageView/Sampler
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
            case vk::DescriptorType::eStorageBuffer: {
                vk::DescriptorBufferInfo bufferInfo;
                bufferInfo.buffer = binding.storageBufferInfo.buffer;
                bufferInfo.offset = binding.storageBufferInfo.offset;
                bufferInfo.range = binding.storageBufferInfo.range;
                bufferInfos.push_back(bufferInfo);
                write.pBufferInfo = &bufferInfos.back();
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

vk::DescriptorSetLayout DescriptorManager::getLayout(LayoutHandle handle) const {
    auto it = layouts.find(handle);
    if (it == layouts.end()) {
        violet::Log::error("Renderer", "Descriptor layout handle {} not found", handle);
        return nullptr;
    }

    vk::DescriptorSetLayout layout = it->second.layout;
    if (!layout) {
        violet::Log::error("Renderer", "Descriptor layout handle {} found but Vulkan handle is null", handle);
    }

    return layout;
}

bool DescriptorManager::hasLayout(LayoutHandle handle) const {
    return layouts.find(handle) != layouts.end();
}

LayoutHandle DescriptorManager::getLayoutHandle(const eastl::string& name) const {
    auto it = nameToHandle.find(name);
    return it != nameToHandle.end() ? it->second : 0;
}

// @deprecated Legacy String-Based API - Remove once all code migrates to LayoutHandle
eastl::vector<vk::DescriptorSet> DescriptorManager::allocateSets(const eastl::string& layoutName, uint32_t count) {
    auto it = nameToHandle.find(layoutName);
    if (it == nameToHandle.end()) {
        violet::Log::error("Renderer", "Descriptor layout '{}' not found", layoutName.c_str());
        return {};
    }

    LayoutHandle handle = it->second;
    eastl::vector<vk::DescriptorSet> sets;
    sets.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        sets.push_back(allocateSet(handle, i % maxFrames));
    }

    return sets;
}

vk::DescriptorSetLayout DescriptorManager::getLayout(const eastl::string& layoutName) const {
    auto it = nameToHandle.find(layoutName);
    if (it == nameToHandle.end()) {
        violet::Log::error("Renderer", "Descriptor layout '{}' not found", layoutName.c_str());
        return nullptr;
    }

    return getLayout(it->second);
}

bool DescriptorManager::hasLayout(const eastl::string& layoutName) const {
    return nameToHandle.find(layoutName) != nameToHandle.end();
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

    // Allocate bindless descriptor set (using legacy API)
    auto it = nameToHandle.find("Bindless");
    if (it != nameToHandle.end()) {
        bindlessSet = allocateSet(it->second, 0);
        bindlessEnabled = true;
    } else {
        violet::Log::error("Renderer", "Bindless layout not found in nameToHandle mapping");
    }

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
    for (const auto& [handle, layoutInfo] : layouts) {
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
    for (const auto& [handle, layoutInfo] : layouts) {
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

    // Allocate descriptor set (using legacy API)
    auto it = nameToHandle.find("MaterialData");
    if (it == nameToHandle.end()) {
        violet::Log::error("Renderer", "MaterialData layout not found in nameToHandle mapping");
        return false;
    }
    materialDataSet = allocateSet(it->second, 0);
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

void DescriptorManager::setReflection(LayoutHandle handle, const ShaderReflection& reflection) {
    auto it = layouts.find(handle);
    if (it != layouts.end()) {
        it->second.reflection = reflection;
    }
}

const ShaderReflection* DescriptorManager::getReflection(LayoutHandle handle) const {
    auto it = layouts.find(handle);
    if (it != layouts.end()) {
        return &it->second.reflection;
    }
    return nullptr;
}

bool DescriptorManager::hasReflection(LayoutHandle handle) const {
    auto it = layouts.find(handle);
    if (it != layouts.end()) {
        return !it->second.reflection.getBuffers().empty();
    }
    return false;
}

// ===== Reflection-Based Uniform Management Implementation =====

// Helper function to align value up to alignment
static inline uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// UniformHandle implementation
FieldProxy UniformHandle::operator[](const eastl::string& fieldName) {
    if (!info || !manager) {
        return FieldProxy(nullptr, 0, 0, fieldName);
    }

    // Cast void* to correct types
    auto* uniformInfo = static_cast<DescriptorManager::UniformSetInfo*>(info);
    auto* descriptorMgr = static_cast<DescriptorManager*>(manager);

    if (!uniformInfo->buffer.mappedData) {
        return FieldProxy(nullptr, 0, 0, fieldName);
    }

    // Get reflection data from layout
    const ShaderReflection* reflection = descriptorMgr->getReflection(uniformInfo->layoutHandle);
    if (!reflection) {
        violet::Log::error("DescriptorManager", "No reflection data for uniform");
        return FieldProxy(nullptr, 0, 0, fieldName);
    }

    // Find the first buffer (assuming single UBO per set for now)
    const auto& buffers = reflection->getBuffers();
    if (buffers.empty()) {
        violet::Log::error("DescriptorManager", "No buffers found in reflection data");
        return FieldProxy(nullptr, 0, 0, fieldName);
    }

    // Get the first buffer
    const auto& buffer = buffers[0];
    const auto* field = reflection->findField(buffer.name, fieldName);

    if (!field) {
        violet::Log::error("DescriptorManager", "Field '{}' not found in buffer '{}'",
            fieldName.c_str(), buffer.name.c_str());
        return FieldProxy(nullptr, 0, 0, fieldName);
    }

    // Calculate dynamic offset for PerFrame
    uint32_t dynamicOffset = 0;
    if (uniformInfo->frequency == UpdateFrequency::PerFrame) {
        dynamicOffset = descriptorMgr->currentFrame * uniformInfo->alignedSize;
    }

    // Calculate address: base + dynamicOffset + field offset
    void* fieldAddress = static_cast<uint8_t*>(uniformInfo->buffer.mappedData) + dynamicOffset + field->offset;
    return FieldProxy(fieldAddress, field->offset, field->size, fieldName);
}

vk::DescriptorSet UniformHandle::getSet() const {
    if (!info) {
        return vk::DescriptorSet{};
    }
    auto* uniformInfo = static_cast<DescriptorManager::UniformSetInfo*>(info);
    return uniformInfo->descriptorSet;
}

uint32_t UniformHandle::getDynamicOffset() const {
    if (!info || !manager) {
        return 0;
    }

    auto* uniformInfo = static_cast<DescriptorManager::UniformSetInfo*>(info);
    auto* descriptorMgr = static_cast<DescriptorManager*>(manager);

    if (uniformInfo->frequency != UpdateFrequency::PerFrame) {
        return 0;
    }

    return descriptorMgr->currentFrame * uniformInfo->alignedSize;
}

// DescriptorManager implementation
void DescriptorManager::setCurrentFrame(uint32_t frameIndex) {
    if (frameIndex >= maxFrames) {
        violet::Log::warn("DescriptorManager", "Frame index {} exceeds maxFrames {}", frameIndex, maxFrames);
        return;
    }
    currentFrame = frameIndex;
}

UniformHandle DescriptorManager::createUniform(const eastl::string& name, LayoutHandle layout, UpdateFrequency frequency) {
    // Check if layout exists
    auto layoutIt = layouts.find(layout);
    if (layoutIt == layouts.end()) {
        violet::Log::error("DescriptorManager", "Layout handle {} not found", layout);
        return UniformHandle();
    }

    // Check if uniform already exists
    if (uniforms.find(layout) != uniforms.end()) {
        violet::Log::warn("DescriptorManager", "Uniform for layout {} already exists, returning existing", layout);
        return UniformHandle(&uniforms[layout], this);
    }

    const LayoutInfo& layoutInfo = layoutIt->second;

    // Get buffer size from reflection
    if (!hasReflection(layout)) {
        violet::Log::error("DescriptorManager", "No reflection data for layout {}", layout);
        return UniformHandle();
    }

    const ShaderReflection* reflection = getReflection(layout);
    const auto& buffers = reflection->getBuffers();
    if (buffers.empty()) {
        violet::Log::error("DescriptorManager", "No buffers in reflection data for layout {}", layout);
        return UniformHandle();
    }

    // Get size from first buffer (assuming single UBO per set)
    uint32_t bufferSize = buffers[0].totalSize;
    uint32_t alignedStride = bufferSize;
    uint32_t totalSize = bufferSize;

    // For PerFrame: align stride to minUniformBufferOffsetAlignment
    if (frequency == UpdateFrequency::PerFrame) {
        vk::PhysicalDeviceProperties props = context->getPhysicalDevice().getProperties();
        uint32_t minAlignment = static_cast<uint32_t>(props.limits.minUniformBufferOffsetAlignment);

        alignedStride = alignUp(bufferSize, minAlignment);
        totalSize = alignedStride * maxFrames;

        violet::Log::debug("DescriptorManager",
            "PerFrame uniform '{}': bufferSize={}, alignedStride={}, totalSize={} (minAlignment={})",
            name.c_str(), bufferSize, alignedStride, totalSize, minAlignment);
    }

    // Create buffer using ResourceFactory
    BufferInfo bufferInfo{
        .size = totalSize,
        .usage = vk::BufferUsageFlagBits::eUniformBuffer,
        .memoryUsage = MemoryUsage::CPU_TO_GPU,
        .debugName = name
    };
    BufferResource buffer = ResourceFactory::createBuffer(context, bufferInfo);

    if (!buffer.buffer || !buffer.mappedData) {
        violet::Log::error("DescriptorManager", "Failed to create buffer for uniform '{}'", name.c_str());
        return UniformHandle();
    }

    // Allocate descriptor set
    vk::DescriptorSet descriptorSet = allocateSet(layout, 0);
    if (!descriptorSet) {
        violet::Log::error("DescriptorManager", "Failed to allocate descriptor set for uniform '{}'", name.c_str());
        ResourceFactory::destroyBuffer(context, buffer);
        return UniformHandle();
    }

    // Update descriptor set to bind buffer
    vk::DescriptorBufferInfo bufferDescInfo;
    bufferDescInfo.buffer = buffer.buffer;
    bufferDescInfo.offset = 0;
    // For PerFrame with dynamic offset, range is the aligned stride (not total buffer size)
    bufferDescInfo.range = frequency == UpdateFrequency::PerFrame ? alignedStride : bufferSize;

    vk::WriteDescriptorSet write;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;  // Assuming UBO is at binding 0
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eUniformBuffer;
    write.pBufferInfo = &bufferDescInfo;

    context->getDevice().updateDescriptorSets(1, &write, 0, nullptr);

    // Store uniform info
    UniformSetInfo info;
    info.buffer = buffer;
    info.descriptorSet = descriptorSet;
    info.layoutHandle = layout;
    info.frequency = frequency;
    info.bufferSize = bufferSize;      // Original size from reflection
    info.alignedSize = alignedStride;   // Aligned stride for dynamic offset
    info.name = name;

    uniforms[layout] = info;

    violet::Log::info("DescriptorManager",
        "Created uniform '{}' (layout={}, frequency={}, bufferSize={}, alignedStride={})",
        name.c_str(), layout, static_cast<int>(frequency), bufferSize, alignedStride);

    return UniformHandle(&uniforms[layout], this);
}

UniformHandle DescriptorManager::getUniform(const eastl::string& name) {
    // Look up layout handle from name
    auto it = nameToHandle.find(name);
    if (it == nameToHandle.end()) {
        violet::Log::error("DescriptorManager", "Uniform layout '{}' not found in nameToHandle", name.c_str());
        return UniformHandle();
    }

    LayoutHandle handle = it->second;

    // Look up uniform info
    auto uniformIt = uniforms.find(handle);
    if (uniformIt == uniforms.end()) {
        violet::Log::error("DescriptorManager", "Uniform for layout '{}' not created yet", name.c_str());
        return UniformHandle();
    }

    return UniformHandle(&uniformIt->second, this);
}

UniformHandle DescriptorManager::getUniform(const eastl::string& name, uint32_t frameIndex) {
    // Temporarily override currentFrame
    uint32_t savedFrame = currentFrame;
    currentFrame = frameIndex;

    UniformHandle handle = getUniform(name);

    // Restore
    currentFrame = savedFrame;

    return handle;
}

// ===== Push Constant Management =====

PushConstantHandle DescriptorManager::registerPushConstants(const PushConstantDesc& desc) {
    if (desc.ranges.empty()) {
        return 0;  // Return 0 for empty push constants
    }

    PushConstantHandle handle = desc.hash();

    // Check if already registered (deduplication)
    if (pushConstants.find(handle) != pushConstants.end()) {
        Log::debug("DescriptorManager", "Push constants (handle={}) already registered, reusing", handle);
        return handle;
    }

    // Register new push constant layout
    pushConstants[handle] = desc.ranges;

    Log::debug("DescriptorManager", "Registered push constants (handle={}, {} ranges)", handle, desc.ranges.size());

    return handle;
}

const eastl::vector<vk::PushConstantRange>& DescriptorManager::getPushConstants(PushConstantHandle handle) const {
    static const eastl::vector<vk::PushConstantRange> empty;

    if (handle == 0) {
        return empty;
    }

    auto it = pushConstants.find(handle);
    if (it == pushConstants.end()) {
        Log::warn("DescriptorManager", "Push constant handle {} not found", handle);
        return empty;
    }

    return it->second;
}

bool DescriptorManager::hasPushConstants(PushConstantHandle handle) const {
    return handle != 0 && pushConstants.find(handle) != pushConstants.end();
}

// ===== PipelineLayout Cache & Named Binding Implementation =====

PipelineLayoutCacheHandle DescriptorManager::getOrCreatePipelineLayoutCache(
    eastl::shared_ptr<Shader> vertShader,
    eastl::shared_ptr<Shader> fragShader) {

    if (!vertShader) {
        Log::error("DescriptorManager", "Vertex shader is required for pipeline layout cache");
        return 0;
    }

    // 1. Compute hash from shader layout handles (layout combination hash)
    const auto& vertHandles = vertShader->getDescriptorLayoutHandles();
    const auto* fragHandles = fragShader ? &fragShader->getDescriptorLayoutHandles() : nullptr;

    // Determine max set index
    size_t maxSetIndex = vertHandles.size();
    if (fragHandles && fragHandles->size() > maxSetIndex) {
        maxSetIndex = fragHandles->size();
    }

    // Compute layout combination hash
    uint32_t hash = 0;
    for (size_t setIndex = 0; setIndex < maxSetIndex; ++setIndex) {
        LayoutHandle vertHandle = (setIndex < vertHandles.size()) ? vertHandles[setIndex] : 0;
        LayoutHandle fragHandle = (fragHandles && setIndex < fragHandles->size()) ? (*fragHandles)[setIndex] : 0;

        // Prefer non-zero handle (they should be identical if both non-zero due to deduplication)
        LayoutHandle handle = (vertHandle != 0) ? vertHandle : fragHandle;

        // Hash the layout handle into the combination hash
        hash ^= std::hash<uint32_t>{}(handle) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }

    // Include push constant handles in the hash
    PushConstantHandle vertPC = vertShader->getPushConstantHandle();
    PushConstantHandle fragPC = fragShader ? fragShader->getPushConstantHandle() : 0;
    hash ^= std::hash<uint32_t>{}(vertPC) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    hash ^= std::hash<uint32_t>{}(fragPC) + 0x9e3779b9 + (hash << 6) + (hash >> 2);

    // Ensure we never return 0 (reserved for error)
    if (hash == 0) hash = 1;

    // 2. Check if cache already exists (cache hit)
    if (pipelineLayoutCache.find(hash) != pipelineLayoutCache.end()) {
        Log::debug("DescriptorManager", "PipelineLayoutCache hit for hash {}", hash);
        return hash;
    }

    // 3. Create new cache
    PipelineLayoutCache cache;

    // Store layout handles (preserving sparsity)
    cache.layoutHandles.resize(maxSetIndex, 0);
    for (size_t setIndex = 0; setIndex < maxSetIndex; ++setIndex) {
        LayoutHandle vertHandle = (setIndex < vertHandles.size()) ? vertHandles[setIndex] : 0;
        LayoutHandle fragHandle = (fragHandles && setIndex < fragHandles->size()) ? (*fragHandles)[setIndex] : 0;
        cache.layoutHandles[setIndex] = (vertHandle != 0) ? vertHandle : fragHandle;
    }

    // Store push constant handle (prefer non-zero)
    cache.pushConstantHandle = (vertPC != 0) ? vertPC : fragPC;

    // Extract resource names and bindless sets from shader reflection
    auto extractFromShader = [&](eastl::shared_ptr<Shader> shader) {
        if (!shader || !shader->hasReflection()) return;

        const auto& layoutHandles = shader->getDescriptorLayoutHandles();

        // 遍历每个set
        for (size_t setIndex = 0; setIndex < layoutHandles.size(); ++setIndex) {
            LayoutHandle handle = layoutHandles[setIndex];
            if (handle == 0) continue;  // 跳过空set

            // Get layout info
            auto it = layouts.find(handle);
            if (it != layouts.end()) {
                const ShaderReflection& reflection = it->second.reflection;

                // 从reflection提取buffer名称映射
                for (const auto& buffer : reflection.getBuffers()) {
                    cache.resourceNameToSet[buffer.name] = buffer.set;
                }

                // 检查是否为bindless (从descriptor flags推断)
                if (it->second.createFlags & vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool) {
                    cache.bindlessSets.insert(static_cast<uint32_t>(setIndex));
                }
            }
        }
    };

    extractFromShader(vertShader);
    extractFromShader(fragShader);

    // 4. Store and return
    pipelineLayoutCache[hash] = cache;

    Log::info("DescriptorManager", "Created PipelineLayoutCache (hash={}, {} resources, {} bindless sets)",
              hash, cache.resourceNameToSet.size(), cache.bindlessSets.size());

    return hash;
}

void DescriptorManager::bindDescriptors(
    vk::CommandBuffer cmd,
    PipelineLayoutCacheHandle cacheHandle,
    vk::PipelineLayout pipelineLayout,
    vk::PipelineBindPoint bindPoint,
    const eastl::vector<NamedDescriptor>& descriptors) {

    // 查找pipeline layout cache
    auto cacheIt = pipelineLayoutCache.find(cacheHandle);
    if (cacheIt == pipelineLayoutCache.end()) {
        Log::warn("DescriptorManager", "PipelineLayoutCache handle {} not found - call getOrCreatePipelineLayoutCache first", cacheHandle);
        return;
    }

    const PipelineLayoutCache& cache = cacheIt->second;

    for (const auto& desc : descriptors) {
        // 从cache查找set index
        auto setIt = cache.resourceNameToSet.find(desc.name);
        if (setIt == cache.resourceNameToSet.end()) {
            Log::warn("DescriptorManager", "Resource '{}' not found in pipeline layout cache", desc.name);
            continue;
        }

        uint32_t setIndex = setIt->second;
        bool isBindless = cache.bindlessSets.find(setIndex) != cache.bindlessSets.end();

        // Bindless特殊处理
        if (isBindless) {
            if (desc.descriptorSet) {
                cmd.bindDescriptorSets(bindPoint, pipelineLayout, setIndex,
                    1, &desc.descriptorSet, 0, nullptr);
                Log::debug("DescriptorManager", "Bound bindless set '{}' at index {}", desc.name, setIndex);
            }
        } else {
            // 普通descriptor：必须存在
            if (!desc.descriptorSet) {
                Log::error("DescriptorManager", "Descriptor '{}' is required but not provided", desc.name);
                continue;
            }

            if (desc.dynamicOffset > 0) {
                cmd.bindDescriptorSets(bindPoint, pipelineLayout, setIndex,
                    1, &desc.descriptorSet, 1, &desc.dynamicOffset);
            } else {
                cmd.bindDescriptorSets(bindPoint, pipelineLayout, setIndex,
                    1, &desc.descriptorSet, 0, nullptr);
            }
        }
    }
}

} // namespace violet
