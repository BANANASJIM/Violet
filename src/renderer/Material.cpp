#include "Material.hpp"
#include "renderer/VulkanContext.hpp"
#include "renderer/Pipeline.hpp"
#include "renderer/DescriptorSet.hpp"
#include "renderer/UniformBuffer.hpp"
#include "renderer/Texture.hpp"

namespace violet {

Material::~Material() {
    cleanup();
}

void Material::create(VulkanContext* ctx) {
    context = ctx;
    createDescriptorSetLayout();
}

void Material::createDescriptorSetLayout() {
    // 创建材质的descriptor set layout (set = 1)
    vk::DescriptorSetLayoutBinding textureBinding;
    textureBinding.binding = 0;  // BASE_COLOR_TEXTURE_BINDING
    textureBinding.descriptorCount = 1;
    textureBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    textureBinding.pImmutableSamplers = nullptr;
    textureBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &textureBinding;

    materialDescriptorSetLayout = context->getDevice().createDescriptorSetLayout(layoutInfo);
}

void Material::cleanup() {
    if (materialDescriptorSetLayout) {
        context->getDevice().destroyDescriptorSetLayout(materialDescriptorSetLayout);
        materialDescriptorSetLayout = nullptr;
    }

    if (pipeline) {
        pipeline->cleanup();
        delete pipeline;
        pipeline = nullptr;
    }
}

vk::PipelineLayout Material::getPipelineLayout() const {
    return pipeline ? pipeline->getLayout() : vk::PipelineLayout{};
}

vk::DescriptorSetLayout Material::getDescriptorSetLayout() const {
    return materialDescriptorSetLayout;
}

MaterialInstance::~MaterialInstance() {
    cleanup();
}

void MaterialInstance::create(VulkanContext* ctx, Material* mat) {
    context = ctx;
    material = mat;

    // Initialize material instance data
    data = PBRMaterialData{};

    // MaterialInstance只需要管理纹理，不需要uniform buffer
}

void MaterialInstance::createDescriptorSet(uint32_t maxFramesInFlight) {
    // 使用Material的descriptor set layout创建descriptor set实例
    descriptorSet = new DescriptorSet();
    descriptorSet->create(context, maxFramesInFlight, DescriptorSetType::MaterialTextures);
}

void MaterialInstance::cleanup() {
    if (descriptorSet) {
        descriptorSet->cleanup();
        delete descriptorSet;
        descriptorSet = nullptr;
    }
}

void MaterialInstance::updateDescriptorSet(uint32_t frameIndex) {
    if (!descriptorSet || !dirty) {
        return;
    }

    // Update textures only - MaterialInstance只管理纹理
    if (baseColorTexture) {
        descriptorSet->updateTexture(frameIndex, baseColorTexture);
    }

    dirty = false;
}

} // namespace violet