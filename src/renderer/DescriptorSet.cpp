#include "DescriptorSet.hpp"
#include "VulkanContext.hpp"
#include "UniformBuffer.hpp"
#include "Texture.hpp"

namespace violet {

void DescriptorSet::create(VulkanContext* ctx, uint32_t maxFramesInFlight) {
    context = ctx;

    // Create descriptor set layout
    eastl::vector<vk::DescriptorSetLayoutBinding> bindings(2);

    // UBO binding
    bindings[0].binding = 0;
    bindings[0].descriptorCount = 1;
    bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings[0].pImmutableSamplers = nullptr;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;

    // Combined image sampler binding
    bindings[1].binding = 1;
    bindings[1].descriptorCount = 1;
    bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    bindings[1].pImmutableSamplers = nullptr;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    descriptorSetLayout = ctx->getDevice().createDescriptorSetLayout(layoutInfo);

    // Create descriptor pool
    eastl::vector<vk::DescriptorPoolSize> poolSizes(2);
    poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount = maxFramesInFlight;
    poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount = maxFramesInFlight;

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
        if (descriptorPool) device.destroyDescriptorPool(descriptorPool);
        if (descriptorSetLayout) device.destroyDescriptorSetLayout(descriptorSetLayout);
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
    descriptorWrite.dstBinding = 1;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    context->getDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

}