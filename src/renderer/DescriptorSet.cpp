#include "DescriptorSet.hpp"
#include "VulkanContext.hpp"
#include "UniformBuffer.hpp"
#include "Texture.hpp"
#include "core/Log.hpp"

namespace violet {

DescriptorSet::~DescriptorSet() {
    VT_TRACE("DescriptorSet destructor");
    cleanup();
}

void DescriptorSet::create(VulkanContext* ctx, uint32_t maxFramesInFlight) {
    // 默认创建材质纹理类型的descriptor set
    create(ctx, maxFramesInFlight, DescriptorSetType::MaterialTextures);
}

void DescriptorSet::create(VulkanContext* ctx, uint32_t maxFramesInFlight, DescriptorSetType type) {
    context = ctx;

    eastl::vector<vk::DescriptorSetLayoutBinding> bindings;
    eastl::vector<vk::DescriptorPoolSize> poolSizes;

    if (type == DescriptorSetType::GlobalUniforms) {
        // Global uniform buffer layout
        bindings.resize(1);
        bindings[0].binding = 0;  // CAMERA_UBO_BINDING
        bindings[0].descriptorCount = 1;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].pImmutableSamplers = nullptr;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;

        poolSizes.resize(1);
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = maxFramesInFlight;
    } else if (type == DescriptorSetType::MaterialTextures) {
        // Material texture sampler layout
        bindings.resize(1);
        bindings[0].binding = 0;  // BASE_COLOR_TEXTURE_BINDING
        bindings[0].descriptorCount = 1;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].pImmutableSamplers = nullptr;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

        poolSizes.resize(1);
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = maxFramesInFlight;
    }

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    descriptorSetLayout = ctx->getDevice().createDescriptorSetLayout(layoutInfo);

    // Create descriptor pool
    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight;

    descriptorPool = ctx->getDevice().createDescriptorPool(poolInfo);

    // Allocate descriptor sets
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
            VT_TRACE("Destroying DescriptorPool: {}", (void*)descriptorPool);
            device.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout) {
            VT_TRACE("Destroying DescriptorSetLayout: {}", (void*)descriptorSetLayout);
            device.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }
    }
}

void DescriptorSet::updateBuffer(uint32_t frameIndex, UniformBuffer* uniformBuffer) {
    if (frameIndex >= descriptorSets.size()) return;

    vk::DescriptorBufferInfo bufferInfo = uniformBuffer->getDescriptorInfo();

    vk::WriteDescriptorSet descriptorWrite;
    descriptorWrite.dstSet = descriptorSets[frameIndex];
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    context->getDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

void DescriptorSet::updateTexture(uint32_t frameIndex, Texture* texture) {
    if (frameIndex >= descriptorSets.size()) return;

    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = texture->getImageView();
    imageInfo.sampler = texture->getSampler();

    vk::WriteDescriptorSet descriptorWrite;
    descriptorWrite.dstSet = descriptorSets[frameIndex];
    descriptorWrite.dstBinding = 0;  // BASE_COLOR_TEXTURE_BINDING = 0
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    context->getDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

}