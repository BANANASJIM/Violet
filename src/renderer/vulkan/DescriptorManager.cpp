#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/ShaderResourceBinding.hpp"
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

DescriptorResourceHandle DescriptorResourceHandle::fromBuffer(vk::Buffer buf, vk::DeviceSize offset, vk::DeviceSize range) {
    DescriptorResourceHandle handle;
    handle.type = Type::Buffer;
    handle.bufferData.buffer = buf;
    handle.bufferData.offset = offset;
    handle.bufferData.range = range;
    return handle;
}

DescriptorResourceHandle DescriptorResourceHandle::fromBuffer(const BufferResource& buf) {
    return fromBuffer(buf.buffer, 0, VK_WHOLE_SIZE);
}

DescriptorResourceHandle DescriptorResourceHandle::fromTexture(Texture* tex) {
    DescriptorResourceHandle handle;
    handle.type = Type::Texture;
    handle.texture = tex;
    return handle;
}

DescriptorResourceHandle DescriptorResourceHandle::fromSampledImage(vk::ImageView view, vk::ImageLayout layout) {
    DescriptorResourceHandle handle;
    handle.type = Type::SampledImage;
    handle.imageView = view;
    handle.imageLayout = layout;
    return handle;
}

DescriptorResourceHandle DescriptorResourceHandle::fromStorageImage(vk::ImageView view) {
    DescriptorResourceHandle handle;
    handle.type = Type::ImageView;
    handle.imageView = view;
    handle.imageLayout = vk::ImageLayout::eGeneral;  // Storage images use General layout
    return handle;
}

DescriptorResourceHandle DescriptorResourceHandle::fromSampler(vk::Sampler samp) {
    DescriptorResourceHandle handle;
    handle.type = Type::Sampler;
    handle.sampler = samp;
    return handle;
}

DescriptorResourceHandle DescriptorResourceHandle::fromCombinedImageSampler(vk::ImageView view, vk::Sampler samp, vk::ImageLayout layout) {
    DescriptorResourceHandle handle;
    handle.type = Type::CombinedImageSampler;
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

            // Chain binding flags if any bindless bindings exist
            vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo;
            if (desc.isBindless && !bindingFlagsArray.empty()) {
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

    // Chain binding flags if any bindless bindings exist
    vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo;
    if (desc.isBindless && !bindingFlagsArray.empty()) {
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

// ===== Set Group Management (frequency-based allocation) =====

SetGroupHandle DescriptorManager::allocateSetGroup(LayoutHandle layoutHandle) {
    auto it = layouts.find(layoutHandle);
    if (it == layouts.end()) {
        violet::Log::error("DescriptorManager", "Layout handle {} not found", layoutHandle);
        return 0;
    }

    const LayoutInfo& layoutInfo = it->second;
    UpdateFrequency frequency = layoutInfo.frequency;

    // Determine how many sets to allocate based on frequency
    uint32_t setCount = (frequency == UpdateFrequency::PerFrame) ? maxFrames : 1;

    // Get pool for this frequency
    vk::DescriptorPool pool = getOrCreatePool(frequency);

    // Allocate all sets in a single batch from the pool
    eastl::vector<vk::DescriptorSetLayout> layouts(setCount, layoutInfo.layout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = setCount;
    allocInfo.pSetLayouts = layouts.data();

    eastl::vector<vk::DescriptorSet> sets;
    try {
        auto stdSets = context->getDevice().allocateDescriptorSets(allocInfo);
        // Manually copy std::vector to eastl::vector (iterator tags incompatible)
        sets.reserve(stdSets.size());
        for (const auto& set : stdSets) {
            sets.push_back(set);
        }
    } catch (const vk::SystemError& e) {
        violet::Log::error("DescriptorManager", "Failed to allocate {} descriptor sets: {}", setCount, e.what());
        return 0;
    }

    // Update pool remaining sets count
    auto& pools = poolsByFrequency[frequency];
    for (auto& poolInfo : pools) {
        if (poolInfo.pool == pool) {
            poolInfo.remainingSets -= setCount;
            break;
        }
    }

    // Create set group and store it
    DescriptorSetGroup group;
    group.sets = eastl::move(sets);
    group.layoutHandle = layoutHandle;
    group.frequency = frequency;

    SetGroupHandle handle = nextSetGroupHandle++;
    setGroups[handle] = eastl::move(group);

    violet::Log::debug("DescriptorManager", "Allocated set group {} with {} set(s) from pool (frequency: {})",
                      handle, setCount, static_cast<int>(frequency));

    return handle;
}

vk::DescriptorSet DescriptorManager::getSet(SetGroupHandle handle, uint32_t frameIndex) const {
    auto it = setGroups.find(handle);
    if (it == setGroups.end()) {
        violet::Log::error("DescriptorManager", "Set group handle {} not found", handle);
        return nullptr;
    }

    const DescriptorSetGroup& group = it->second;

    // For PerFrame, return the set for the specified frame
    if (group.frequency == UpdateFrequency::PerFrame) {
        if (frameIndex >= group.sets.size()) {
            violet::Log::error("DescriptorManager", "Frame index {} out of range for set group {} (size: {})",
                              frameIndex, handle, group.sets.size());
            return nullptr;
        }
        return group.sets[frameIndex];
    }

    // For other frequencies, always return the single set
    return group.sets[0];
}

void DescriptorManager::freeSetGroup(SetGroupHandle handle) {
    auto it = setGroups.find(handle);
    if (it == setGroups.end()) {
        violet::Log::warn("DescriptorManager", "Attempted to free non-existent set group {}", handle);
        return;
    }

    // Note: Descriptor sets are pool-allocated and will be freed when pool is destroyed
    // We just remove the group from our tracking map
    setGroups.erase(it);

    violet::Log::debug("DescriptorManager", "Freed set group {}", handle);
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

// ===== High-Level Automatic Binding Interface =====

void DescriptorManager::bindResources(vk::CommandBuffer cmd, ShaderResourceBinding& binding, uint32_t frameIndex) {
    // 1. Validate binding
    if (!binding.isInitialized()) {
        violet::Log::error("DescriptorManager", "Cannot bind uninitialized ShaderResourceBinding");
        return;
    }

    LayoutHandle layoutHandle = binding.getLayoutHandle();
    if (layoutHandle == 0) {
        violet::Log::error("DescriptorManager", "ShaderResourceBinding has invalid layout handle");
        return;
    }

    // 2. Allocate set group if not already allocated
    SetGroupHandle setGroupHandle = binding.getSetGroupHandle();
    if (setGroupHandle == 0) {
        setGroupHandle = allocateSetGroup(layoutHandle);
        binding.setSetGroupHandle(setGroupHandle);
        violet::Log::debug("DescriptorManager", "Allocated set group {} for binding (layout {})",
                          setGroupHandle, layoutHandle);
    }

    // 3. Update descriptor set if dirty
    if (binding.isDirty()) {
        const ShaderReflection* reflection = binding.getShaderReflection();
        if (!reflection) {
            violet::Log::error("DescriptorManager", "Cannot update binding: shader reflection not available");
            return;
        }

        updateSetFromBinding(setGroupHandle, binding, *reflection, frameIndex);
        binding.clearDirty();
    }

    // 4. Bind descriptor set to command buffer
    vk::DescriptorSet set = getSet(setGroupHandle, frameIndex);
    vk::PipelineLayout pipelineLayout = binding.getPipelineLayout();
    uint32_t setIndex = binding.getSetIndex();

    // Auto-detect compute vs graphics
    vk::PipelineBindPoint bindPoint = binding.getComputePipeline() ?
        vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics;

    cmd.bindDescriptorSets(
        bindPoint,
        pipelineLayout,
        setIndex,
        1,
        &set,
        0, nullptr
    );

    violet::Log::debug("DescriptorManager", "Bound descriptor set for binding at set index {} ({})",
                      setIndex, bindPoint == vk::PipelineBindPoint::eCompute ? "compute" : "graphics");
}

// ===== Reflection-Driven Resource Binding =====

void DescriptorManager::updateSetFromBinding(SetGroupHandle handle, const ShaderResourceBinding& binding,
                                              const ShaderReflection& reflection, uint32_t frameIndex) {
    // Get the descriptor set for this frame from the set group
    vk::DescriptorSet set = getSet(handle, frameIndex);
    if (!set) {
        violet::Log::error("DescriptorManager", "Failed to get descriptor set from group {} for frame {}",
                          handle, frameIndex);
        return;
    }

    // Delegate to existing updateSet implementation
    updateSet(set, reflection, binding.getResources());
}

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

void DescriptorManager::bindBuffer(vk::DescriptorSet set, const eastl::string& resourceName,
                                   const BufferResource& buffer, const ShaderReflection& reflection) {
    const auto* resource = reflection.findResource(resourceName);
    if (!resource) {
        violet::Log::error("DescriptorManager", "Resource '{}' not found in shader reflection", resourceName.c_str());
        return;
    }

    if (resource->type != vk::DescriptorType::eUniformBuffer &&
        resource->type != vk::DescriptorType::eStorageBuffer) {
        violet::Log::error("DescriptorManager", "Resource '{}' is not a buffer (type={})",
                          resourceName.c_str(), vk::to_string(resource->type).c_str());
        return;
    }

    bindBuffer(set, resource->binding, buffer, resource->type);
}

void DescriptorManager::bindTexture(vk::DescriptorSet set, const eastl::string& resourceName,
                                   Texture* texture, const ShaderReflection& reflection) {
    const auto* resource = reflection.findResource(resourceName);
    if (!resource) {
        violet::Log::error("DescriptorManager", "Resource '{}' not found in shader reflection", resourceName.c_str());
        return;
    }

    if (resource->type != vk::DescriptorType::eCombinedImageSampler &&
        resource->type != vk::DescriptorType::eSampledImage) {
        violet::Log::error("DescriptorManager", "Resource '{}' is not a texture (type={})",
                          resourceName.c_str(), vk::to_string(resource->type).c_str());
        return;
    }

    bindTexture(set, resource->binding, texture);
}

void DescriptorManager::bindStorageImage(vk::DescriptorSet set, const eastl::string& resourceName,
                                         vk::ImageView imageView, const ShaderReflection& reflection) {
    const auto* resource = reflection.findResource(resourceName);
    if (!resource) {
        violet::Log::error("DescriptorManager", "Resource '{}' not found in shader reflection", resourceName.c_str());
        return;
    }

    if (resource->type != vk::DescriptorType::eStorageImage) {
        violet::Log::error("DescriptorManager", "Resource '{}' is not a storage image (type={})",
                          resourceName.c_str(), vk::to_string(resource->type).c_str());
        return;
    }

    bindStorageImage(set, resource->binding, imageView);
}

// ===== Direct Resource Binding =====

void DescriptorManager::bindBuffer(vk::DescriptorSet set, uint32_t binding,
                                   const BufferResource& buffer, vk::DescriptorType type,
                                   vk::DeviceSize offset, vk::DeviceSize range) {
    if (!set) {
        violet::Log::error("DescriptorManager", "Cannot bind buffer to null descriptor set");
        return;
    }

    if (type != vk::DescriptorType::eUniformBuffer && type != vk::DescriptorType::eStorageBuffer) {
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

    bindSampler(bindlessSet, 2, linearSampler);
    bindSampler(bindlessSet, 3, nearestSampler);
    bindSampler(bindlessSet, 4, shadowSampler);

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

} // namespace violet
