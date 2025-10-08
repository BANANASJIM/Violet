#include "DescriptorSet.hpp"

#include "resource/Texture.hpp"
#include "resource/gpu/UniformBuffer.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "core/Log.hpp"

namespace violet {

DescriptorSet::~DescriptorSet() {
    // DescriptorSet destructor
    cleanup();
}

void DescriptorSet::init(VulkanContext* ctx, const eastl::vector<vk::DescriptorSet>& sets) {
    context = ctx;
    descriptorSets = sets;
    // No pool/layout ownership - managed by DescriptorManager
}

void DescriptorSet::create(VulkanContext* ctx, uint32_t maxFramesInFlight) {
    // WARNING: This method is COMPUTE SHADER ONLY
    // Graphics pipelines should use DescriptorManager::allocateSets() instead
    violet::Log::warn("Renderer", "DescriptorSet::create() called - this is a legacy compute-only API");
    create(ctx, maxFramesInFlight, DescriptorSetType::EquirectToCubemap);
}

void DescriptorSet::create(VulkanContext* ctx, uint32_t maxFramesInFlight, DescriptorSetType type) {
    // WARNING: This method is COMPUTE SHADER ONLY - kept for ComputePipeline compatibility
    // Graphics pipelines must use DescriptorManager for centralized descriptor management

    context = ctx;

    // Only EquirectToCubemap (compute shader) is supported
    // Graphics pipelines should use DescriptorManager instead
    if (type != DescriptorSetType::EquirectToCubemap) {
        violet::Log::error("Renderer", "DescriptorSet::create() - Only EquirectToCubemap (compute) is supported. Graphics pipelines must use DescriptorManager!");
        return;
    }

    // COMPUTE SHADER ONLY: EquirectToCubemap layout
    eastl::vector<vk::DescriptorSetLayoutBinding> bindings = {
        {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute}
    };
    eastl::vector<vk::DescriptorPoolSize> poolSizes = {
        {vk::DescriptorType::eCombinedImageSampler, maxFramesInFlight},
        {vk::DescriptorType::eStorageImage, maxFramesInFlight}
    };

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    descriptorSetLayout = ctx->getDevice().createDescriptorSetLayout(layoutInfo);

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight;
    descriptorPool = ctx->getDevice().createDescriptorPool(poolInfo);

    eastl::vector<vk::DescriptorSetLayout> layouts(maxFramesInFlight, descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    auto stdSets = ctx->getDevice().allocateDescriptorSets(allocInfo);
    descriptorSets.resize(stdSets.size());
    for (size_t i = 0; i < stdSets.size(); i++) {
        descriptorSets[i] = stdSets[i];
    }
}

void DescriptorSet::cleanup() {
    if (context) {
        auto device = context->getDevice();
        if (descriptorPool) {
            device.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout) {
            device.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }
    }
}

void DescriptorSet::updateBuffer(uint32_t frameIndex, UniformBuffer* uniformBuffer) {
    if (frameIndex >= descriptorSets.size())
        return;

    vk::DescriptorBufferInfo bufferInfo = uniformBuffer->getDescriptorInfo();

    vk::WriteDescriptorSet descriptorWrite;
    descriptorWrite.dstSet          = descriptorSets[frameIndex];
    descriptorWrite.dstBinding      = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType  = vk::DescriptorType::eUniformBuffer;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo     = &bufferInfo;

    context->getDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

void DescriptorSet::updateTexture(uint32_t frameIndex, Texture* texture) {
    if (frameIndex >= descriptorSets.size())
        return;

    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView   = texture->getImageView();
    imageInfo.sampler     = texture->getSampler();

    vk::WriteDescriptorSet descriptorWrite;
    descriptorWrite.dstSet          = descriptorSets[frameIndex];
    descriptorWrite.dstBinding      = 0; // BASE_COLOR_TEXTURE_BINDING = 0
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo      = &imageInfo;

    context->getDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

void DescriptorSet::updateUniformBuffer(uint32_t frameIndex, UniformBuffer* uniformBuffer, uint32_t binding) {
    if (frameIndex >= descriptorSets.size() || !uniformBuffer)
        return;

    vk::DescriptorBufferInfo bufferInfo = uniformBuffer->getDescriptorInfo();

    vk::WriteDescriptorSet descriptorWrite;
    descriptorWrite.dstSet          = descriptorSets[frameIndex];
    descriptorWrite.dstBinding      = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType  = vk::DescriptorType::eUniformBuffer;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo     = &bufferInfo;

    context->getDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

void DescriptorSet::updateTexture(uint32_t frameIndex, Texture* texture, uint32_t binding) {
    if (frameIndex >= descriptorSets.size()) {
        violet::Log::error(
            "Renderer",
            "Invalid frameIndex {} >= descriptorSets.size() {}",
            frameIndex,
            descriptorSets.size()
        );
        return;
    }

    if (!texture) {
        violet::Log::error(
            "Renderer",
            "Texture is null for binding {} frameIndex {} - cannot update descriptor",
            binding,
            frameIndex
        );
        return;
    }

    // Validate descriptor set before updating
    if (!descriptorSets[frameIndex]) {
        violet::Log::error(
            "Renderer",
            "Descriptor set is null for frameIndex {} binding {} - cannot update",
            frameIndex,
            binding
        );
        return;
    }

    vk::ImageView imageView = texture->getImageView();
    vk::Sampler   sampler   = texture->getSampler();

    if (!imageView || !sampler) {
        violet::Log::error(
            "Renderer",
            "Texture has invalid imageView or sampler for binding {} frameIndex {}",
            binding,
            frameIndex
        );
        return;
    }

    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView   = imageView;
    imageInfo.sampler     = sampler;

    vk::WriteDescriptorSet descriptorWrite;
    descriptorWrite.dstSet          = descriptorSets[frameIndex];
    descriptorWrite.dstBinding      = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo      = &imageInfo;

    context->getDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

void DescriptorSet::updateStorageImage(uint32_t frameIndex, Texture* texture, uint32_t binding) {
    if (frameIndex >= descriptorSets.size()) {
        violet::Log::error(
            "Renderer",
            "Invalid frameIndex {} >= descriptorSets.size() {}",
            frameIndex,
            descriptorSets.size()
        );
        return;
    }

    if (!texture) {
        violet::Log::error(
            "Renderer",
            "Texture is null for storage image binding {} frameIndex {} - cannot update descriptor",
            binding,
            frameIndex
        );
        return;
    }

    vk::ImageView imageView = texture->getImageView();

    if (!imageView) {
        violet::Log::error(
            "Renderer",
            "Texture has invalid imageView for storage image binding {} frameIndex {}",
            binding,
            frameIndex
        );
        return;
    }

    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eGeneral;  // Storage images use eGeneral layout
    imageInfo.imageView   = imageView;
    imageInfo.sampler     = nullptr;  // Storage images don't use samplers

    vk::WriteDescriptorSet descriptorWrite;
    descriptorWrite.dstSet          = descriptorSets[frameIndex];
    descriptorWrite.dstBinding      = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType  = vk::DescriptorType::eStorageImage;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo      = &imageInfo;

    context->getDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

} // namespace violet
