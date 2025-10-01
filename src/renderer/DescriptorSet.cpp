#include "DescriptorSet.hpp"

#include "Texture.hpp"
#include "UniformBuffer.hpp"
#include "VulkanContext.hpp"
#include "core/Log.hpp"

namespace violet {

DescriptorSet::~DescriptorSet() {
    // DescriptorSet destructor
    cleanup();
}

void DescriptorSet::create(VulkanContext* ctx, uint32_t maxFramesInFlight) {
    // 默认创建材质纹理类型的descriptor set
    create(ctx, maxFramesInFlight, DescriptorSetType::MaterialTextures);
}

void DescriptorSet::create(VulkanContext* ctx, uint32_t maxFramesInFlight, DescriptorSetType type) {
    context = ctx;

    eastl::vector<vk::DescriptorSetLayoutBinding> bindings;
    eastl::vector<vk::DescriptorPoolSize>         poolSizes;

    if (type == DescriptorSetType::GlobalUniforms) {
        // Global uniform buffer layout
        bindings.resize(2);

        // Binding 0: Global UBO
        bindings[0].binding            = 0; // CAMERA_UBO_BINDING
        bindings[0].descriptorCount    = 1;
        bindings[0].descriptorType     = vk::DescriptorType::eUniformBuffer;
        bindings[0].pImmutableSamplers = nullptr;
        bindings[0].stageFlags         = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        // Binding 1: Environment map (skybox cubemap)
        bindings[1].binding            = 1;
        bindings[1].descriptorCount    = 1;
        bindings[1].descriptorType     = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].pImmutableSamplers = nullptr;
        bindings[1].stageFlags         = vk::ShaderStageFlagBits::eFragment;

        poolSizes.resize(2);
        poolSizes[0].type            = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = maxFramesInFlight;
        poolSizes[1].type            = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = maxFramesInFlight;
    } else if (type == DescriptorSetType::MaterialTextures) {
        // Material descriptor layout: 1 UBO + 5 texture samplers
        bindings.resize(6);

        // Binding 0: Material UBO
        bindings[0].binding            = 0;
        bindings[0].descriptorCount    = 1;
        bindings[0].descriptorType     = vk::DescriptorType::eUniformBuffer;
        bindings[0].pImmutableSamplers = nullptr;
        bindings[0].stageFlags         = vk::ShaderStageFlagBits::eFragment;

        // Bindings 1-5: Textures
        for (uint32_t i = 1; i <= 5; ++i) {
            bindings[i].binding            = i;
            bindings[i].descriptorCount    = 1;
            bindings[i].descriptorType     = vk::DescriptorType::eCombinedImageSampler;
            bindings[i].pImmutableSamplers = nullptr;
            bindings[i].stageFlags         = vk::ShaderStageFlagBits::eFragment;
        }

        poolSizes.resize(2);
        poolSizes[0].type            = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = maxFramesInFlight;
        poolSizes[1].type            = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = maxFramesInFlight * 5;
    } else if (type == DescriptorSetType::UnlitMaterialTextures) {
        // Unlit material descriptor layout: 1 UBO + 1 texture sampler
        bindings.resize(2);

        // Binding 0: Material UBO
        bindings[0].binding            = 0;
        bindings[0].descriptorCount    = 1;
        bindings[0].descriptorType     = vk::DescriptorType::eUniformBuffer;
        bindings[0].pImmutableSamplers = nullptr;
        bindings[0].stageFlags         = vk::ShaderStageFlagBits::eFragment;

        // Binding 1: Base color texture
        bindings[1].binding            = 1;
        bindings[1].descriptorCount    = 1;
        bindings[1].descriptorType     = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].pImmutableSamplers = nullptr;
        bindings[1].stageFlags         = vk::ShaderStageFlagBits::eFragment;

        poolSizes.resize(2);
        poolSizes[0].type            = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = maxFramesInFlight;
        poolSizes[1].type            = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = maxFramesInFlight;
    } else if (type == DescriptorSetType::EquirectToCubemap) {
        // Compute shader descriptor layout: input sampler2D + output imageCube
        bindings.resize(2);

        // Binding 0: Equirectangular input texture (sampler2D)
        bindings[0].binding            = 0;
        bindings[0].descriptorCount    = 1;
        bindings[0].descriptorType     = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].pImmutableSamplers = nullptr;
        bindings[0].stageFlags         = vk::ShaderStageFlagBits::eCompute;

        // Binding 1: Cubemap output (storage image)
        bindings[1].binding            = 1;
        bindings[1].descriptorCount    = 1;
        bindings[1].descriptorType     = vk::DescriptorType::eStorageImage;
        bindings[1].pImmutableSamplers = nullptr;
        bindings[1].stageFlags         = vk::ShaderStageFlagBits::eCompute;

        poolSizes.resize(2);
        poolSizes[0].type            = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = maxFramesInFlight;
        poolSizes[1].type            = vk::DescriptorType::eStorageImage;
        poolSizes[1].descriptorCount = maxFramesInFlight;
    } else if (type == DescriptorSetType::None) {
        // 不创建任何descriptor set - 仅使用全局descriptor set
        return;
    }

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings    = bindings.data();

    descriptorSetLayout = ctx->getDevice().createDescriptorSetLayout(layoutInfo);

    // Create descriptor pool
    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes    = poolSizes.data();
    poolInfo.maxSets       = maxFramesInFlight;

    descriptorPool = ctx->getDevice().createDescriptorPool(poolInfo);

    // Allocate descriptor sets
    eastl::vector<vk::DescriptorSetLayout> layouts(maxFramesInFlight, descriptorSetLayout);
    vk::DescriptorSetAllocateInfo          allocInfo;
    allocInfo.descriptorPool     = descriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts        = layouts.data();

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
