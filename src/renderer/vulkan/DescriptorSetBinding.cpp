#include "DescriptorSetBinding.hpp"
#include "ShaderResourceBinding.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "resource/Texture.hpp"
#include "core/Log.hpp"

namespace violet {

DescriptorSetBinding::DescriptorSetBinding(DescriptorManager* mgr, LayoutHandle layout)
    : manager(mgr), layoutHandle(layout) {
    if (!manager) {
        violet::Log::error("DescriptorSetBinding", "Cannot create with null DescriptorManager");
        return;
    }

    if (layout == 0) {
        violet::Log::error("DescriptorSetBinding", "Cannot create with invalid layout handle");
        return;
    }

    // Allocate descriptor set group from manager
    setGroupHandle = manager->allocateSetGroup(layoutHandle);
    if (setGroupHandle == 0) {
        violet::Log::error("DescriptorSetBinding",
            "Failed to allocate descriptor set group for layout {}", layoutHandle);
    } else {
        violet::Log::debug("DescriptorSetBinding",
            "Created with setGroupHandle {} for layout {}", setGroupHandle, layoutHandle);
    }
}

DescriptorSetBinding::~DescriptorSetBinding() {
    cleanup();
}

DescriptorSetBinding::DescriptorSetBinding(DescriptorSetBinding&& other) noexcept
    : manager(other.manager),
      setGroupHandle(other.setGroupHandle),
      layoutHandle(other.layoutHandle) {
    // Transfer ownership
    other.manager = nullptr;
    other.setGroupHandle = 0;
    other.layoutHandle = 0;
}

DescriptorSetBinding& DescriptorSetBinding::operator=(DescriptorSetBinding&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        cleanup();

        // Transfer ownership
        manager = other.manager;
        setGroupHandle = other.setGroupHandle;
        layoutHandle = other.layoutHandle;

        // Clear source
        other.manager = nullptr;
        other.setGroupHandle = 0;
        other.layoutHandle = 0;
    }
    return *this;
}

void DescriptorSetBinding::update(const ShaderResourceBinding& resources, uint32_t frameIndex) {
    if (!isValid()) {
        violet::Log::error("DescriptorSetBinding", "Cannot update invalid binding");
        return;
    }

    if (!manager) {
        violet::Log::error("DescriptorSetBinding", "Manager is null");
        return;
    }

    // Get the descriptor set for this frame
    vk::DescriptorSet descriptorSet = manager->getSet(setGroupHandle, frameIndex);
    if (!descriptorSet) {
        violet::Log::error("DescriptorSetBinding",
            "Failed to get descriptor set for handle {} frame {}", setGroupHandle, frameIndex);
        return;
    }

    // Build write descriptors from resource map
    eastl::vector<vk::WriteDescriptorSet> writes;
    eastl::vector<vk::DescriptorBufferInfo> bufferInfos;
    eastl::vector<vk::DescriptorImageInfo> imageInfos;

    const auto& resourceMap = resources.getResources();

    // Reserve space to avoid reallocation (which would invalidate pointers)
    writes.reserve(resourceMap.size());
    bufferInfos.reserve(resourceMap.size());
    imageInfos.reserve(resourceMap.size());

    for (const auto& [key, handle] : resourceMap) {
        vk::WriteDescriptorSet write;
        write.dstSet = descriptorSet;
        write.dstBinding = key.binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;

        switch (handle.type) {
            case DescriptorResourceHandle::Type::Buffer: {
                vk::DescriptorBufferInfo bufferInfo;
                bufferInfo.buffer = handle.bufferData.buffer;
                bufferInfo.offset = handle.bufferData.offset;
                bufferInfo.range = handle.bufferData.range;
                bufferInfos.push_back(bufferInfo);

                write.descriptorType = vk::DescriptorType::eUniformBuffer;  // TODO: detect storage buffer
                write.pBufferInfo = &bufferInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::Texture: {
                if (!handle.texture) {
                    violet::Log::warn("DescriptorSetBinding", "Texture handle is null for binding ({}, {})",
                        key.set, key.binding);
                    continue;
                }

                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageView = handle.texture->getImageView();
                imageInfo.sampler = handle.texture->getSampler();
                imageInfo.imageLayout = handle.imageLayout;
                imageInfos.push_back(imageInfo);

                write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
                write.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::SampledImage: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageView = handle.imageView;
                imageInfo.sampler = nullptr;
                imageInfo.imageLayout = handle.imageLayout;
                imageInfos.push_back(imageInfo);

                write.descriptorType = vk::DescriptorType::eSampledImage;
                write.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::ImageView: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageView = handle.imageView;
                imageInfo.sampler = nullptr;
                imageInfo.imageLayout = vk::ImageLayout::eGeneral;
                imageInfos.push_back(imageInfo);

                write.descriptorType = vk::DescriptorType::eStorageImage;
                write.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::Sampler: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageView = nullptr;
                imageInfo.sampler = handle.sampler;
                imageInfo.imageLayout = vk::ImageLayout::eUndefined;
                imageInfos.push_back(imageInfo);

                write.descriptorType = vk::DescriptorType::eSampler;
                write.pImageInfo = &imageInfos.back();
                break;
            }

            case DescriptorResourceHandle::Type::CombinedImageSampler: {
                vk::DescriptorImageInfo imageInfo;
                imageInfo.imageView = handle.combinedData.imageView;
                imageInfo.sampler = handle.combinedData.sampler;
                imageInfo.imageLayout = handle.imageLayout;
                imageInfos.push_back(imageInfo);

                write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
                write.pImageInfo = &imageInfos.back();
                break;
            }
        }

        writes.push_back(write);
    }

    if (!writes.empty()) {
        manager->getContext()->getDevice().updateDescriptorSets(writes, {});
        violet::Log::debug("DescriptorSetBinding",
            "Updated {} descriptor(s) for setGroupHandle {} frame {}",
            writes.size(), setGroupHandle, frameIndex);
    }
}

void DescriptorSetBinding::bind(vk::CommandBuffer cmd,
                                vk::PipelineLayout pipelineLayout,
                                uint32_t firstSet,
                                uint32_t frameIndex,
                                vk::PipelineBindPoint bindPoint,
                                const eastl::vector<uint32_t>& dynamicOffsets) {
    if (!isValid()) {
        violet::Log::error("DescriptorSetBinding", "Cannot bind invalid binding");
        return;
    }

    if (!manager) {
        violet::Log::error("DescriptorSetBinding", "Manager is null");
        return;
    }

    vk::DescriptorSet descriptorSet = manager->getSet(setGroupHandle, frameIndex);
    if (!descriptorSet) {
        violet::Log::error("DescriptorSetBinding",
            "Failed to get descriptor set for handle {} frame {}", setGroupHandle, frameIndex);
        return;
    }

    // Bind to command buffer
    if (dynamicOffsets.empty()) {
        cmd.bindDescriptorSets(
            bindPoint,
            pipelineLayout,
            firstSet,
            1,
            &descriptorSet,
            0,
            nullptr
        );
    } else {
        cmd.bindDescriptorSets(
            bindPoint,
            pipelineLayout,
            firstSet,
            1,
            &descriptorSet,
            static_cast<uint32_t>(dynamicOffsets.size()),
            dynamicOffsets.data()
        );
    }

    violet::Log::debug("DescriptorSetBinding",
        "Bound descriptor set to slot {} for frame {} (handle: {})",
        firstSet, frameIndex, setGroupHandle);
}

void DescriptorSetBinding::cleanup() {
    if (setGroupHandle != 0 && manager != nullptr) {
        manager->freeSetGroup(setGroupHandle);
        violet::Log::debug("DescriptorSetBinding",
            "Freed setGroupHandle {} for layout {}", setGroupHandle, layoutHandle);
        setGroupHandle = 0;
    }
}

} // namespace violet
