#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/ShaderResourceBinding.hpp"
#include "renderer/vulkan/PipelineBase.hpp"
#include "resource/Texture.hpp"
#include "core/Log.hpp"
#include <functional>
#include <EASTL/algorithm.h>

namespace violet {

// Pool size multipliers
constexpr uint32_t POOL_SIZE_TRANSIENT = 200;
constexpr uint32_t POOL_SIZE_STATIC = 10;

LayoutHandle DescriptorLayoutDesc::hash() const {
    uint32_t h = 0;

    // Hash frequency and flags
    h ^= std::hash<int>{}(static_cast<int>(frequency)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(flags)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(isBindless) + 0x9e3779b9 + (h << 6) + (h >> 2);

    // Hash each binding (excluding stages to allow merging for vertex+fragment shaders)
    for (const auto& binding : bindings) {
        h ^= std::hash<uint32_t>{}(binding.binding) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(static_cast<int>(binding.type)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        // NOTE: stages excluded from hash to allow stage merging for graphics pipelines
        h ^= std::hash<uint32_t>{}(binding.count) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(binding.flags)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }

    return h;
}

PushConstantHandle PushConstantDesc::hash() const {
    uint32_t h = 0;

    // Hash each push constant range (excluding stageFlags to allow merging for vertex+fragment shaders)
    for (const auto& range : ranges) {
        h ^= std::hash<uint32_t>{}(range.offset) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(range.size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        // NOTE: stageFlags excluded from hash to allow stage merging for graphics pipelines
    }

    return h != 0 ? h : 1;
}

// ===== DescriptorResourceHandle Factory Methods =====

DescriptorResourceHandle DescriptorResourceHandle::fromBuffer(vk::Buffer buf, vk::DeviceSize offset, vk::DeviceSize range, UpdateFrequency freq) {
    DescriptorResourceHandle handle;
    handle.type = Type::Buffer;
    handle.frequency = freq;
    handle.bufferData.buffer = buf;
    handle.bufferData.offset = offset;
    handle.bufferData.range = range;
    return handle;
}

DescriptorResourceHandle DescriptorResourceHandle::fromBuffer(const BufferResource& buf, UpdateFrequency freq) {
    return fromBuffer(buf.buffer, 0, VK_WHOLE_SIZE, freq);
}

DescriptorResourceHandle DescriptorResourceHandle::fromTexture(Texture* tex, UpdateFrequency freq) {
    DescriptorResourceHandle handle;
    handle.type = Type::Texture;
    handle.frequency = freq;
    handle.texture = tex;
    return handle;
}

DescriptorResourceHandle DescriptorResourceHandle::fromSampledImage(vk::ImageView view, vk::ImageLayout layout, UpdateFrequency freq) {
    DescriptorResourceHandle handle;
    handle.type = Type::SampledImage;
    handle.frequency = freq;
    handle.imageView = view;
    handle.imageLayout = layout;
    return handle;
}

DescriptorResourceHandle DescriptorResourceHandle::fromStorageImage(vk::ImageView view, UpdateFrequency freq) {
    DescriptorResourceHandle handle;
    handle.type = Type::ImageView;
    handle.frequency = freq;
    handle.imageView = view;
    handle.imageLayout = vk::ImageLayout::eGeneral;
    return handle;
}

DescriptorResourceHandle DescriptorResourceHandle::fromSampler(vk::Sampler samp, UpdateFrequency freq) {
    DescriptorResourceHandle handle;
    handle.type = Type::Sampler;
    handle.frequency = freq;
    handle.sampler = samp;
    return handle;
}

DescriptorResourceHandle DescriptorResourceHandle::fromCombinedImageSampler(vk::ImageView view, vk::Sampler samp, vk::ImageLayout layout, UpdateFrequency freq) {
    DescriptorResourceHandle handle;
    handle.type = Type::CombinedImageSampler;
    handle.frequency = freq;
    handle.combinedData.imageView = view;
    handle.combinedData.sampler = samp;
    handle.imageLayout = layout;
    return handle;
}

// DescriptorManager implementation
void DescriptorManager::init(VulkanContext* ctx, uint32_t maxFramesInFlight) {
    context = ctx;
    maxFrames = maxFramesInFlight;

    samplerManager.init(context);

    violet::Log::info("Renderer", "DescriptorManager initialized with {} frames", maxFrames);
}

void DescriptorManager::cleanup() {
    auto device = context->getDevice();

    samplerManager.cleanup();

    for (auto& [frequency, pools] : poolsByFrequency) {
        for (auto& poolInfo : pools) {
            device.destroyDescriptorPool(poolInfo.pool);
        }
    }
    poolsByFrequency.clear();

    for (auto& [handle, layoutInfo] : layouts) {
        device.destroyDescriptorSetLayout(layoutInfo.layout);
    }
    layouts.clear();
    nameToHandle.clear();

    violet::Log::info("Renderer", "DescriptorManager cleaned up");
}

LayoutHandle DescriptorManager::registerLayout(const DescriptorLayoutDesc& desc) {
    LayoutHandle handle = desc.hash();

    auto existingIt = layouts.find(handle);
    if (existingIt != layouts.end()) {
        // Layout exists, check if we need to merge stage flags
        LayoutInfo& existingInfo = existingIt->second;
        bool needsUpdate = false;

        // Merge stage flags for each binding
        for (const auto& newBinding : desc.bindings) {
            for (auto& existingBinding : existingInfo.bindings) {
                if (existingBinding.binding == newBinding.binding) {
                    vk::ShaderStageFlags oldStages = existingBinding.stages;
                    existingBinding.stages |= newBinding.stages;  // Merge stages
                    if (oldStages != existingBinding.stages) {
                        needsUpdate = true;
                    }
                    break;
                }
            }
        }

        if (needsUpdate) {
            // Destroy old layout and create new one with merged stages
            context->getDevice().destroyDescriptorSetLayout(existingInfo.layout);

            // Create new Vulkan layout with merged stages
            eastl::vector<vk::DescriptorSetLayoutBinding> vkBindings;
            for (const auto& binding : existingInfo.bindings) {
                vk::DescriptorSetLayoutBinding vkBinding;
                vkBinding.binding = binding.binding;
                vkBinding.descriptorType = binding.type;
                vkBinding.descriptorCount = binding.count;
                vkBinding.stageFlags = binding.stages;  // Use merged stages
                vkBinding.pImmutableSamplers = nullptr;
                vkBindings.push_back(vkBinding);
            }

            // Collect per-binding flags
            eastl::vector<vk::DescriptorBindingFlags> bindingFlagsArray;
            bindingFlagsArray.reserve(existingInfo.bindings.size());
            for (const auto& binding : existingInfo.bindings) {
                bindingFlagsArray.push_back(binding.flags);
            }

            // Create layout with per-binding flags
            vk::DescriptorSetLayoutCreateInfo layoutInfo;
            layoutInfo.flags = existingInfo.createFlags;
            layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
            layoutInfo.pBindings = vkBindings.data();

            // Chain binding flags if any bindings have flags
            vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo;
            if (!bindingFlagsArray.empty()) {
                bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlagsArray.size());
                bindingFlagsInfo.pBindingFlags = bindingFlagsArray.data();
                layoutInfo.pNext = &bindingFlagsInfo;
            }

            existingInfo.layout = context->getDevice().createDescriptorSetLayout(layoutInfo);

            violet::Log::info("Renderer", "Merged stage flags for descriptor layout '{}' (hash={})", desc.name.c_str(), handle);
        } else {
            violet::Log::debug("Renderer", "Descriptor layout '{}' (hash={}) already registered with same stages, reusing",
                              desc.name.c_str(), handle);
        }

        // Store name->handle mapping even when reusing (for legacy API)
        if (!desc.name.empty()) {
            nameToHandle[desc.name] = handle;
        }

        return handle;
    }

    // New layout - create from scratch
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

    // Chain binding flags if any bindings have flags
    vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo;
    if (!bindingFlagsArray.empty()) {
        bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlagsArray.size());
        bindingFlagsInfo.pBindingFlags = bindingFlagsArray.data();
        layoutInfo.pNext = &bindingFlagsInfo;
    }

    vk::DescriptorSetLayout layout = context->getDevice().createDescriptorSetLayout(layoutInfo);

    // Store layout info including original bindings for future merging
    LayoutInfo info;
    info.layout = layout;
    info.frequency = desc.frequency;
    info.poolSizes = poolSizes;
    info.createFlags = desc.flags;
    info.bindings = desc.bindings;  // Store bindings for stage merging
    layouts[handle] = info;

    // Store name->handle mapping for legacy API
    if (!desc.name.empty()) {
        nameToHandle[desc.name] = handle;
    }

    violet::Log::info("Renderer", "Registered descriptor layout '{}' (hash={}) with {} bindings",
                     desc.name.c_str(), handle, desc.bindings.size());
    return handle;
}

vk::DescriptorSet DescriptorManager::allocateSet(LayoutHandle handle) {
    auto it = layouts.find(handle);
    if (it == layouts.end()) {
        violet::Log::error("Renderer", "Descriptor layout handle {} not found", handle);
        return nullptr;
    }

    const LayoutInfo& layoutInfo = it->second;
    vk::DescriptorPool pool = getOrCreatePool(layoutInfo.frequency);

    // Allocate single descriptor set
    // Note: For PerFrame resources, ShaderResources manages dynamic offsets at bind time
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

// ===== Reflection-Driven Resource Binding =====

// Unified API: updateSet with (set, binding) keys
void DescriptorManager::updateSet(vk::DescriptorSet set,
                                   const ShaderReflection& reflection,
                                   const eastl::unordered_map<BindingKey, DescriptorResourceHandle>& resources) {
    if (!set) {
        violet::Log::error("DescriptorManager", "Cannot update null descriptor set");
        return;
    }

    eastl::vector<vk::WriteDescriptorSet> writes;
    eastl::vector<vk::DescriptorBufferInfo> bufferInfos;
    eastl::vector<vk::DescriptorImageInfo> imageInfos;

    // Reserve space to avoid reallocation
    writes.reserve(resources.size());
    bufferInfos.reserve(resources.size());
    imageInfos.reserve(resources.size());

    for (const auto& [key, resource] : resources) {
        // 1. Look up reflection info by (set, binding)
        const auto* reflectedRes = reflection.findResourceByBinding(key.set, key.binding);
        if (!reflectedRes) {
            violet::Log::warn("DescriptorManager", "Resource at set={}, binding={} not found in shader reflection",
                            key.set, key.binding);
            continue;
        }

        // Debug: Log reflected resource details
        violet::Log::debug("DescriptorManager", "Found reflected resource '{}': set={}, binding={}, type={} ({})",
                          reflectedRes->name.c_str(), key.set, key.binding,
                          static_cast<uint32_t>(reflectedRes->type),
                          vk::to_string(reflectedRes->type).c_str());

        // 2. Verify resource type matches reflection
        vk::DescriptorType expectedType = reflectedRes->type;
        vk::DescriptorType actualType = expectedType;

        // Map DescriptorResourceHandle type to DescriptorType
        switch (resource.type) {
            case DescriptorResourceHandle::Type::Buffer:
                // expectedType already set from reflection (eUniformBuffer or eStorageBuffer)
                break;
            case DescriptorResourceHandle::Type::Texture:
                actualType = vk::DescriptorType::eCombinedImageSampler;
                break;
            case DescriptorResourceHandle::Type::SampledImage:
                actualType = vk::DescriptorType::eSampledImage;
                break;
            case DescriptorResourceHandle::Type::ImageView:
                actualType = vk::DescriptorType::eStorageImage;
                break;
            case DescriptorResourceHandle::Type::Sampler:
                actualType = vk::DescriptorType::eSampler;
                break;
            case DescriptorResourceHandle::Type::CombinedImageSampler:
                actualType = vk::DescriptorType::eCombinedImageSampler;
                break;
        }

        // For buffers, we don't know the exact type from DescriptorResourceHandle alone, trust reflection
        if (resource.type != DescriptorResourceHandle::Type::Buffer && actualType != expectedType) {
            violet::Log::error("DescriptorManager", "Resource at set={}, binding={} type mismatch: expected {}, got {}",
                              key.set, key.binding, vk::to_string(expectedType).c_str(), vk::to_string(actualType).c_str());
            continue;
        }

        // 3. Create WriteDescriptorSet
        vk::WriteDescriptorSet write;
        write.dstSet = set;
        write.dstBinding = key.binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = expectedType;

        // 4. Fill descriptor info based on resource type
        switch (resource.type) {
            case DescriptorResourceHandle::Type::Buffer: {
                vk::DescriptorBufferInfo bufferInfo;
                bufferInfo.buffer = resource.bufferData.buffer;
                bufferInfo.offset = resource.bufferData.offset;
                bufferInfo.range = resource.bufferData.range;
                bufferInfos.push_back(bufferInfo);
                write.pBufferInfo = &bufferInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::Texture: {
                if (!resource.texture) {
                    violet::Log::error("DescriptorManager", "Resource at set={}, binding={} has null texture",
                                      key.set, key.binding);
                    continue;
                }
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = resource.imageLayout;
                imageInfo.imageView = resource.texture->getImageView();
                imageInfo.sampler = resource.texture->getSampler();
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::SampledImage: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = resource.imageLayout;
                imageInfo.imageView = resource.imageView;
                imageInfo.sampler = nullptr;  // Separate sampler binding
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::ImageView: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = resource.imageLayout;
                imageInfo.imageView = resource.imageView;
                imageInfo.sampler = nullptr;
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::Sampler: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = vk::ImageLayout::eUndefined;
                imageInfo.imageView = nullptr;
                imageInfo.sampler = resource.sampler;
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::CombinedImageSampler: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = resource.imageLayout;
                imageInfo.imageView = resource.combinedData.imageView;
                imageInfo.sampler = resource.combinedData.sampler;
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
                break;
            }

            default:
                violet::Log::warn("DescriptorManager", "Unsupported resource handle type at set={}, binding={}",
                                key.set, key.binding);
                continue;
        }

        writes.push_back(write);
    }

    // 5. Batch update all resources in one Vulkan call
    if (!writes.empty()) {
        context->getDevice().updateDescriptorSets(writes, {});
        violet::Log::debug("DescriptorManager", "Batched update of {} descriptor(s)", writes.size());
    }
}

// Legacy API: updateSet with name-based keys
void DescriptorManager::updateSet(vk::DescriptorSet set,
                                   const ShaderReflection& reflection,
                                   const eastl::unordered_map<eastl::string, DescriptorResourceHandle>& resources) {
    if (!set) {
        violet::Log::error("DescriptorManager", "Cannot update null descriptor set");
        return;
    }

    eastl::vector<vk::WriteDescriptorSet> writes;
    eastl::vector<vk::DescriptorBufferInfo> bufferInfos;
    eastl::vector<vk::DescriptorImageInfo> imageInfos;

    // Reserve space to avoid reallocation
    writes.reserve(resources.size());
    bufferInfos.reserve(resources.size());
    imageInfos.reserve(resources.size());

    for (const auto& [name, resource] : resources) {
        // 1. Look up reflection info by name
        const auto* reflectedRes = reflection.findResource(name);
        if (!reflectedRes) {
            violet::Log::warn("DescriptorManager", "Resource '{}' not found in shader reflection", name.c_str());
            continue;
        }

        // Debug: Log reflected resource details
        violet::Log::debug("DescriptorManager", "Found reflected resource '{}': binding={}, type={} ({})",
                          name.c_str(), reflectedRes->binding, static_cast<uint32_t>(reflectedRes->type),
                          vk::to_string(reflectedRes->type).c_str());

        // 2. Verify resource type matches reflection
        vk::DescriptorType expectedType = reflectedRes->type;
        vk::DescriptorType actualType = expectedType;

        // Map DescriptorResourceHandle type to DescriptorType
        switch (resource.type) {
            case DescriptorResourceHandle::Type::Buffer:
                // expectedType already set from reflection (eUniformBuffer or eStorageBuffer)
                break;
            case DescriptorResourceHandle::Type::Texture:
                actualType = vk::DescriptorType::eCombinedImageSampler;
                break;
            case DescriptorResourceHandle::Type::SampledImage:
                actualType = vk::DescriptorType::eSampledImage;
                break;
            case DescriptorResourceHandle::Type::ImageView:
                actualType = vk::DescriptorType::eStorageImage;
                break;
            case DescriptorResourceHandle::Type::Sampler:
                actualType = vk::DescriptorType::eSampler;
                break;
            case DescriptorResourceHandle::Type::CombinedImageSampler:
                actualType = vk::DescriptorType::eCombinedImageSampler;
                break;
        }

        // For buffers, we don't know the exact type from DescriptorResourceHandle alone, trust reflection
        if (resource.type != DescriptorResourceHandle::Type::Buffer && actualType != expectedType) {
            violet::Log::error("DescriptorManager", "Resource '{}' type mismatch: expected {}, got {}",
                              name.c_str(), vk::to_string(expectedType).c_str(), vk::to_string(actualType).c_str());
            continue;
        }

        // 3. Create WriteDescriptorSet
        vk::WriteDescriptorSet write;
        write.dstSet = set;
        write.dstBinding = reflectedRes->binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = expectedType;

        // 4. Fill descriptor info based on resource type
        switch (resource.type) {
            case DescriptorResourceHandle::Type::Buffer: {
                vk::DescriptorBufferInfo bufferInfo;
                bufferInfo.buffer = resource.bufferData.buffer;
                bufferInfo.offset = resource.bufferData.offset;
                bufferInfo.range = resource.bufferData.range;
                bufferInfos.push_back(bufferInfo);
                write.pBufferInfo = &bufferInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::Texture: {
                if (!resource.texture) {
                    violet::Log::error("DescriptorManager", "Resource '{}' has null texture", name.c_str());
                    continue;
                }
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = resource.imageLayout;
                imageInfo.imageView = resource.texture->getImageView();
                imageInfo.sampler = resource.texture->getSampler();
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::SampledImage: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = resource.imageLayout;
                imageInfo.imageView = resource.imageView;
                imageInfo.sampler = nullptr;  // Separate sampler binding
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::ImageView: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = resource.imageLayout;
                imageInfo.imageView = resource.imageView;
                imageInfo.sampler = nullptr;
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::Sampler: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = vk::ImageLayout::eUndefined;
                imageInfo.imageView = nullptr;
                imageInfo.sampler = resource.sampler;
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::CombinedImageSampler: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageLayout = resource.imageLayout;
                imageInfo.imageView = resource.combinedData.imageView;
                imageInfo.sampler = resource.combinedData.sampler;
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
                break;
            }

            default:
                violet::Log::warn("DescriptorManager", "Unsupported resource handle type for '{}'", name.c_str());
                continue;
        }

        writes.push_back(write);
    }

    // 5. Batch update all resources in one Vulkan call
    if (!writes.empty()) {
        context->getDevice().updateDescriptorSets(writes, {});
        violet::Log::debug("DescriptorManager", "Batched update of {} descriptor(s)", writes.size());
    }
}

// ===== One-off Descriptor Updates =====

void DescriptorManager::updateSingleDescriptor(vk::DescriptorSet set, uint32_t binding,
                                                const DescriptorResourceHandle& resource) {
    if (!set) {
        violet::Log::error("DescriptorManager", "Cannot update null descriptor set");
        return;
    }

    // Dispatch to type-specific internal helpers
    switch (resource.type) {
        case DescriptorResourceHandle::Type::Buffer: {
            // For buffers, need to construct BufferResource for bindBuffer
            // Note: We don't have mappedData/size/alignment in DescriptorResourceHandle,
            // so create a minimal BufferResource for descriptor update only
            BufferResource bufRes{resource.bufferData.buffer, nullptr, 0, 0};
            bindBuffer(set, binding, bufRes,
                      vk::DescriptorType::eStorageBuffer, // Assume storage buffer for simplicity
                      resource.bufferData.offset, resource.bufferData.range);
            break;
        }

        case DescriptorResourceHandle::Type::Texture:
            bindTexture(set, binding, resource.texture);
            break;

        case DescriptorResourceHandle::Type::ImageView:
            bindStorageImage(set, binding, resource.imageView);
            break;

        case DescriptorResourceHandle::Type::Sampler:
            bindSampler(set, binding, resource.sampler);
            break;

        case DescriptorResourceHandle::Type::SampledImage:
        case DescriptorResourceHandle::Type::CombinedImageSampler:
            violet::Log::error("DescriptorManager", "SampledImage/CombinedImageSampler not supported in updateSingleDescriptor, use dedicated methods");
            break;

        default:
            violet::Log::error("DescriptorManager", "Unsupported resource type in updateSingleDescriptor");
            break;
    }
}

// ===== Internal Descriptor Binding Helpers =====

void DescriptorManager::bindBuffer(vk::DescriptorSet set, uint32_t binding,
                                   const BufferResource& buffer, vk::DescriptorType type,
                                   vk::DeviceSize offset, vk::DeviceSize range) {
    if (!set) {
        violet::Log::error("DescriptorManager", "Cannot bind buffer to null descriptor set");
        return;
    }

    if (type != vk::DescriptorType::eUniformBuffer &&
        type != vk::DescriptorType::eStorageBuffer &&
        type != vk::DescriptorType::eUniformBufferDynamic &&
        type != vk::DescriptorType::eStorageBufferDynamic) {
        violet::Log::error("DescriptorManager", "Invalid descriptor type for buffer binding");
        return;
    }

    vk::DescriptorBufferInfo bufferInfo;
    bufferInfo.buffer = buffer.buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = range;

    vk::WriteDescriptorSet write;
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = &bufferInfo;

    context->getDevice().updateDescriptorSets({write}, {});
}

void DescriptorManager::bindTexture(vk::DescriptorSet set, uint32_t binding, Texture* texture) {
    if (!set) {
        violet::Log::error("DescriptorManager", "Cannot bind texture to null descriptor set");
        return;
    }

    if (!texture) {
        violet::Log::error("DescriptorManager", "Cannot bind null texture");
        return;
    }

    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = texture->getImageView();
    imageInfo.sampler = texture->getSampler();

    vk::WriteDescriptorSet write;
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &imageInfo;

    context->getDevice().updateDescriptorSets({write}, {});
}

void DescriptorManager::bindStorageImage(vk::DescriptorSet set, uint32_t binding, vk::ImageView imageView) {
    if (!set) {
        violet::Log::error("DescriptorManager", "Cannot bind storage image to null descriptor set");
        return;
    }

    if (!imageView) {
        violet::Log::error("DescriptorManager", "Cannot bind null image view");
        return;
    }

    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eGeneral;
    imageInfo.imageView = imageView;
    imageInfo.sampler = nullptr;

    vk::WriteDescriptorSet write;
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eStorageImage;
    write.pImageInfo = &imageInfo;

    context->getDevice().updateDescriptorSets({write}, {});
}

void DescriptorManager::bindSampler(vk::DescriptorSet set, uint32_t binding, vk::Sampler sampler) {
    if (!set) {
        violet::Log::error("DescriptorManager", "Cannot bind sampler to null descriptor set");
        return;
    }

    if (!sampler) {
        violet::Log::error("DescriptorManager", "Cannot bind null sampler");
        return;
    }

    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eUndefined;
    imageInfo.imageView = nullptr;
    imageInfo.sampler = sampler;

    vk::WriteDescriptorSet write;
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eSampler;
    write.pImageInfo = &imageInfo;

    context->getDevice().updateDescriptorSets({write}, {});
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
        sets.push_back(allocateSet(handle));
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

void DescriptorManager::initBindless(uint32_t maxTextures, LayoutHandle bindlessLayoutHandle) {
    if (!hasLayout(bindlessLayoutHandle)) {
        violet::Log::error("Renderer", "Bindless layout handle {} not registered", bindlessLayoutHandle);
        return;
    }

    bindlessMaxTextures = maxTextures;
    bindlessTextureSlots.resize(maxTextures, nullptr);

    // 预留前10个索引给全局纹理:
    // Index 0: 保留为nullptr标记（shader中的"无纹理"检测）
    // Index 1: White texture
    // Index 2: Black texture
    // Index 3: Normal texture
    // Index 4: MetallicRoughness texture
    // Index 5: Shadow atlas (全局固定)
    // Index 6-9: 保留给未来的全局纹理 (IBL, lightmaps, etc.)
    // Index 10+: 材质纹理动态分配
    bindlessFreeIndices.reserve(maxTextures - 10);
    for (uint32_t i = 10; i < maxTextures; ++i) {
        bindlessFreeIndices.push_back(i);
    }

    // Initialize cubemap arrays (binding 1)
    bindlessCubemapSlots.resize(bindlessMaxCubemaps, nullptr);
    bindlessCubemapFreeIndices.reserve(bindlessMaxCubemaps);
    for (uint32_t i = 0; i < bindlessMaxCubemaps; ++i) {
        bindlessCubemapFreeIndices.push_back(i);
    }

    // Allocate bindless descriptor set using the provided layout handle
    bindlessSet = allocateSet(bindlessLayoutHandle);
    bindlessEnabled = true;

    violet::Log::info("Renderer", "DescriptorManager bindless initialized with {} max 2D textures and {} max cubemaps", maxTextures, bindlessMaxCubemaps);
}

void DescriptorManager::initBindlessSamplers() {
    if (!bindlessEnabled) {
        violet::Log::error("Renderer", "Bindless not enabled - call initBindless() first");
        return;
    }

    // Bind global samplers to bindless set (Set 1)
    // Based on pbr_bindless.slang bindings:
    // - Binding 2: linearSampler
    // - Binding 3: nearestSampler
    // - Binding 4: shadowSampler

    vk::Sampler linearSampler = samplerManager.getSampler(SamplerType::Default);
    vk::Sampler nearestSampler = samplerManager.getSampler(SamplerType::Nearest);
    vk::Sampler shadowSampler = samplerManager.getSampler(SamplerType::Shadow);

    // Use public API for descriptor updates
    updateSingleDescriptor(bindlessSet, 2, DescriptorResourceHandle::fromSampler(linearSampler));
    updateSingleDescriptor(bindlessSet, 3, DescriptorResourceHandle::fromSampler(nearestSampler));
    updateSingleDescriptor(bindlessSet, 4, DescriptorResourceHandle::fromSampler(shadowSampler));

    violet::Log::info("Renderer", "Initialized bindless global samplers (linear, nearest, shadow)");
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
    updateBindlessDescriptor(texture, 0, index);
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

    updateBindlessDescriptor(texture, 0, index);
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
    updateBindlessDescriptor(cubemapTexture, 1, index);
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
    // Two pool size categories: transient (frequently updated) and static (rarely updated)
    uint32_t poolSizeMultiplier = (frequency == UpdateFrequency::Static)
        ? POOL_SIZE_STATIC
        : POOL_SIZE_TRANSIENT;

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

    // Pre-allocate common descriptor types to handle late shader registration
    // This ensures pools can accommodate compute shaders that register during pipeline init
    const eastl::vector<vk::DescriptorType> commonTypes = {
        vk::DescriptorType::eUniformBuffer,
        vk::DescriptorType::eStorageBuffer,
        vk::DescriptorType::eUniformBufferDynamic,  // For PerFrame triple-buffered resources
        vk::DescriptorType::eStorageBufferDynamic,  // For PerFrame triple-buffered resources
        vk::DescriptorType::eSampledImage,         // For separate samplers (Slang default)
        vk::DescriptorType::eSampler,              // For separate samplers
        vk::DescriptorType::eStorageImage,         // For compute shaders
        vk::DescriptorType::eCombinedImageSampler  // For legacy/transition period
    };

    // Add common types if not already present, with conservative allocation
    uint32_t commonTypeCount = poolSizeMultiplier * maxFrames * 4;  // 4 descriptors per type as baseline
    for (auto commonType : commonTypes) {
        bool found = false;
        for (const auto& poolSize : poolSizes) {
            if (poolSize.type == commonType) {
                found = true;
                break;
            }
        }
        if (!found) {
            poolSizes.push_back({commonType, commonTypeCount});
            violet::Log::debug("DescriptorManager", "Pre-allocated {} descriptors of type {} for frequency {}",
                              commonTypeCount, static_cast<int>(commonType), static_cast<int>(frequency));
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

// Helper: Update bindless descriptor array element
void DescriptorManager::updateBindlessDescriptor(Texture* texture, uint32_t binding, uint32_t arrayIndex) {
    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = texture->getImageView();
    imageInfo.sampler = texture->getSampler();

    vk::WriteDescriptorSet write;
    write.dstSet = bindlessSet;
    write.dstBinding = binding;
    write.dstArrayElement = arrayIndex;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &imageInfo;

    context->getDevice().updateDescriptorSets(1, &write, 0, nullptr);
}

void DescriptorManager::setCurrentFrame(uint32_t frameIndex) {
    if (frameIndex >= maxFrames) {
        violet::Log::warn("DescriptorManager", "Frame index {} exceeds maxFrames {}", frameIndex, maxFrames);
        return;
    }
    currentFrame = frameIndex;
}

// ===== Push Constant Management =====

PushConstantHandle DescriptorManager::registerPushConstants(const PushConstantDesc& desc) {
    if (desc.ranges.empty()) {
        return 0;  // Return 0 for empty push constants
    }

    PushConstantHandle handle = desc.hash();

    auto existingIt = pushConstants.find(handle);
    if (existingIt != pushConstants.end()) {
        // Push constants exist, check if we need to merge stage flags
        auto& existingRanges = existingIt->second;
        bool needsUpdate = false;

        // Merge stage flags for each range
        for (const auto& newRange : desc.ranges) {
            for (auto& existingRange : existingRanges) {
                // Match by offset and size
                if (existingRange.offset == newRange.offset && existingRange.size == newRange.size) {
                    vk::ShaderStageFlags oldStages = existingRange.stageFlags;
                    existingRange.stageFlags |= newRange.stageFlags;  // Merge stages
                    if (oldStages != existingRange.stageFlags) {
                        needsUpdate = true;
                    }
                    break;
                }
            }
        }

        if (needsUpdate) {
            Log::info("DescriptorManager", "Merged stage flags for push constants (handle={})", handle);
        } else {
            Log::debug("DescriptorManager", "Push constants (handle={}) already registered with same stages, reusing", handle);
        }

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

// ===== New Unified API Implementation =====

UpdateFrequency DescriptorManager::getMaxFrequencyForSet(const eastl::unordered_map<BindingKey, DescriptorResourceHandle>& resources, uint32_t setIndex) {
    UpdateFrequency maxFreq = UpdateFrequency::Static;
    for (const auto& [key, resource] : resources) {
        if (key.set == setIndex && resource.frequency > maxFreq) {
            maxFreq = resource.frequency;
        }
    }
    return maxFreq;
}

size_t DescriptorManager::computeOwnerID(const ShaderResourceBinding& srb, uint32_t setIndex, UpdateFrequency maxFreq) {
    if (maxFreq == UpdateFrequency::PerFrame) {
        return currentFrame;
    }
    return srb.hashForSet(setIndex);
}

vk::DescriptorSet DescriptorManager::getOrCreateCachedSet(LayoutHandle handle, size_t ownerID) {
    SetCacheKey cacheKey{handle, ownerID};

    auto it = setCache.find(cacheKey);
    if (it != setCache.end()) {
        return it->second;
    }

    vk::DescriptorSet newSet = allocateSet(handle);
    setCache[cacheKey] = newSet;
    return newSet;
}

void DescriptorManager::updateDescriptorInternal(vk::DescriptorSet set, const BindingKey& key,
                                                   const DescriptorResourceHandle& resource,
                                                   const ShaderReflection* reflection) {
    vk::WriteDescriptorSet write;
    write.dstSet = set;
    write.dstBinding = key.binding;
    write.dstArrayElement = key.arrayIndex;
    write.descriptorCount = 1;

    vk::DescriptorBufferInfo bufferInfo;
    vk::DescriptorImageInfo imageInfo;

    switch (resource.type) {
        case DescriptorResourceHandle::Type::Buffer: {
            // Use reflection to get correct buffer type (UNIFORM_BUFFER or STORAGE_BUFFER)
            const auto* reflectedRes = reflection->findResourceByBinding(key.set, key.binding);
            if (reflectedRes) {
                write.descriptorType = reflectedRes->type;
            } else {
                // Fallback to STORAGE_BUFFER if not found (conservative choice)
                Log::warn("DescriptorManager", "Buffer at set={}, binding={} not found in reflection, using STORAGE_BUFFER",
                         key.set, key.binding);
                write.descriptorType = vk::DescriptorType::eStorageBuffer;
            }
            bufferInfo.buffer = resource.bufferData.buffer;
            bufferInfo.offset = resource.bufferData.offset;
            bufferInfo.range = resource.bufferData.range;
            write.pBufferInfo = &bufferInfo;
            break;
        }

        case DescriptorResourceHandle::Type::Texture:
            if (resource.texture) {
                write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
                imageInfo.imageView = resource.texture->getImageView();
                imageInfo.sampler = resource.texture->getSampler();
                imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                write.pImageInfo = &imageInfo;
            }
            break;

        case DescriptorResourceHandle::Type::SampledImage:
            write.descriptorType = vk::DescriptorType::eSampledImage;
            imageInfo.imageView = resource.imageView;
            imageInfo.imageLayout = resource.imageLayout;
            write.pImageInfo = &imageInfo;
            break;

        case DescriptorResourceHandle::Type::ImageView:
            write.descriptorType = vk::DescriptorType::eStorageImage;
            imageInfo.imageView = resource.imageView;
            imageInfo.imageLayout = resource.imageLayout;
            write.pImageInfo = &imageInfo;
            break;

        case DescriptorResourceHandle::Type::Sampler:
            write.descriptorType = vk::DescriptorType::eSampler;
            imageInfo.sampler = resource.sampler;
            write.pImageInfo = &imageInfo;
            break;

        case DescriptorResourceHandle::Type::CombinedImageSampler:
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            imageInfo.imageView = resource.combinedData.imageView;
            imageInfo.sampler = resource.combinedData.sampler;
            imageInfo.imageLayout = resource.imageLayout;
            write.pImageInfo = &imageInfo;
            break;
    }

    context->getDevice().updateDescriptorSets(1, &write, 0, nullptr);
}

void DescriptorManager::update(const ShaderResourceBinding& srb, const PipelineBase* pipeline) {
    if (!pipeline || !pipeline->getReflection()) {
        Log::error("DescriptorManager", "Invalid pipeline or reflection");
        return;
    }

    const ShaderReflection* reflection = pipeline->getReflection();
    eastl::unordered_map<BindingKey, DescriptorResourceHandle> allResources = srb.getResources();

    for (const auto& [name, handle] : srb.getNameBindings()) {
        auto bindingKey = pipeline->getBindingKey(name);
        if (bindingKey.has_value()) {
            allResources[bindingKey.value()] = handle;
        }
    }

    // Batch all descriptor writes per set for efficiency
    eastl::vector<vk::WriteDescriptorSet> allWrites;
    eastl::vector<vk::DescriptorBufferInfo> allBufferInfos;
    eastl::vector<vk::DescriptorImageInfo> allImageInfos;

    allWrites.reserve(allResources.size());
    allBufferInfos.reserve(allResources.size());
    allImageInfos.reserve(allResources.size());

    for (uint32_t setIndex = 0; setIndex < 32; ++setIndex) {
        LayoutHandle layoutHandle = pipeline->getLayoutHandle(setIndex);
        if (layoutHandle == 0) break;

        UpdateFrequency maxFreq = getMaxFrequencyForSet(allResources, setIndex);
        size_t ownerID = computeOwnerID(srb, setIndex, maxFreq);
        vk::DescriptorSet descriptorSet = getOrCreateCachedSet(layoutHandle, ownerID);

        // Collect all writes for this set
        for (const auto& [key, resource] : allResources) {
            if (key.set != setIndex) continue;

            // Prepare write descriptor
            vk::WriteDescriptorSet write;
            write.dstSet = descriptorSet;
            write.dstBinding = key.binding;
            write.dstArrayElement = key.arrayIndex;
            write.descriptorCount = 1;

            switch (resource.type) {
                case DescriptorResourceHandle::Type::Buffer: {
                    // Use reflection to get correct buffer type
                    const auto* reflectedRes = reflection->findResourceByBinding(key.set, key.binding);
                    if (reflectedRes) {
                        write.descriptorType = reflectedRes->type;
                    } else {
                        Log::warn("DescriptorManager", "Buffer at set={}, binding={} not found in reflection, using STORAGE_BUFFER",
                                 key.set, key.binding);
                        write.descriptorType = vk::DescriptorType::eStorageBuffer;
                    }

                    vk::DescriptorBufferInfo bufferInfo;
                    bufferInfo.buffer = resource.bufferData.buffer;
                    bufferInfo.offset = resource.bufferData.offset;
                    bufferInfo.range = resource.bufferData.range;
                    allBufferInfos.push_back(bufferInfo);
                    write.pBufferInfo = &allBufferInfos.back();
                    break;
                }

                case DescriptorResourceHandle::Type::Texture:
                    if (resource.texture) {
                        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;

                        vk::DescriptorImageInfo imageInfo;
                        imageInfo.imageView = resource.texture->getImageView();
                        imageInfo.sampler = resource.texture->getSampler();
                        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                        allImageInfos.push_back(imageInfo);
                        write.pImageInfo = &allImageInfos.back();
                    }
                    break;

                case DescriptorResourceHandle::Type::SampledImage:
                    write.descriptorType = vk::DescriptorType::eSampledImage;
                    {
                        vk::DescriptorImageInfo imageInfo;
                        imageInfo.imageView = resource.imageView;
                        imageInfo.imageLayout = resource.imageLayout;
                        allImageInfos.push_back(imageInfo);
                        write.pImageInfo = &allImageInfos.back();
                    }
                    break;

                case DescriptorResourceHandle::Type::ImageView:
                    write.descriptorType = vk::DescriptorType::eStorageImage;
                    {
                        vk::DescriptorImageInfo imageInfo;
                        imageInfo.imageView = resource.imageView;
                        imageInfo.imageLayout = resource.imageLayout;
                        allImageInfos.push_back(imageInfo);
                        write.pImageInfo = &allImageInfos.back();
                    }
                    break;

                case DescriptorResourceHandle::Type::Sampler:
                    write.descriptorType = vk::DescriptorType::eSampler;
                    {
                        vk::DescriptorImageInfo imageInfo;
                        imageInfo.sampler = resource.sampler;
                        allImageInfos.push_back(imageInfo);
                        write.pImageInfo = &allImageInfos.back();
                    }
                    break;

                case DescriptorResourceHandle::Type::CombinedImageSampler:
                    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
                    {
                        vk::DescriptorImageInfo imageInfo;
                        imageInfo.imageView = resource.combinedData.imageView;
                        imageInfo.sampler = resource.combinedData.sampler;
                        imageInfo.imageLayout = resource.imageLayout;
                        allImageInfos.push_back(imageInfo);
                        write.pImageInfo = &allImageInfos.back();
                    }
                    break;
            }

            allWrites.push_back(write);
        }
    }

    // Single batched update for all descriptor writes
    if (!allWrites.empty()) {
        context->getDevice().updateDescriptorSets(allWrites, {});
    }
}

void DescriptorManager::bind(vk::CommandBuffer cmd, const ShaderResourceBinding& srb, const PipelineBase* pipeline) {
    if (!pipeline || !pipeline->getReflection()) return;

    eastl::vector<vk::DescriptorSet> sets;
    eastl::unordered_map<BindingKey, DescriptorResourceHandle> allResources = srb.getResources();

    for (const auto& [name, handle] : srb.getNameBindings()) {
        auto bindingKey = pipeline->getBindingKey(name);
        if (bindingKey.has_value()) {
            allResources[bindingKey.value()] = handle;
        }
    }

    for (uint32_t setIndex = 0; setIndex < 32; ++setIndex) {
        LayoutHandle layoutHandle = pipeline->getLayoutHandle(setIndex);
        if (layoutHandle == 0) break;

        // Auto-bind Set 1 (bindless set) if enabled
        if (setIndex == 1 && bindlessEnabled) {
            sets.push_back(bindlessSet);
            continue;  // bindless set is already populated in initBindless(), just bind it
        }

        UpdateFrequency maxFreq = getMaxFrequencyForSet(allResources, setIndex);
        size_t ownerID = computeOwnerID(srb, setIndex, maxFreq);

        vk::DescriptorSet descriptorSet = getOrCreateCachedSet(layoutHandle, ownerID);
        sets.push_back(descriptorSet);
    }

    //todo perframe ubo need dynamic offset
    if (!sets.empty()) {
        cmd.bindDescriptorSets(
            pipeline->getBindPoint(),
            pipeline->getPipelineLayout(),
            0,
            static_cast<uint32_t>(sets.size()),
            sets.data(),
            0, nullptr
        );
    }
}

} // namespace violet
