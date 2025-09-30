#include "Material.hpp"
#include "renderer/VulkanContext.hpp"
#include "renderer/GraphicsPipeline.hpp"
#include "renderer/DescriptorSet.hpp"
#include "renderer/UniformBuffer.hpp"
#include "renderer/Texture.hpp"
#include "core/Log.hpp"
#include <EASTL/vector.h>

namespace violet {

Material::~Material() {
    cleanup();
}

void Material::create(VulkanContext* ctx) {
    context = ctx;
    createDescriptorSetLayout();
}

void Material::create(VulkanContext* ctx, DescriptorSetType materialType) {
    context = ctx;
    createDescriptorSetLayout(materialType);
}

void Material::createDescriptorSetLayout() {
    // Default to PBR material layout
    createDescriptorSetLayout(DescriptorSetType::MaterialTextures);
}

void Material::createDescriptorSetLayout(DescriptorSetType materialType) {
    eastl::vector<vk::DescriptorSetLayoutBinding> bindings;

    if (materialType == DescriptorSetType::MaterialTextures) {
        // PBR Material layout: UBO + 5 textures
        // Binding 0: Material UBO
        vk::DescriptorSetLayoutBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.descriptorCount = 1;
        uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboBinding.pImmutableSamplers = nullptr;
        uboBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings.push_back(uboBinding);

        // Binding 1-5: Textures (Base color, Metallic-roughness, Normal, Occlusion, Emissive)
        for (uint32_t i = 1; i <= 5; ++i) {
            vk::DescriptorSetLayoutBinding textureBinding;
            textureBinding.binding = i;
            textureBinding.descriptorCount = 1;
            textureBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            textureBinding.pImmutableSamplers = nullptr;
            textureBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
            bindings.push_back(textureBinding);
        }
    } else if (materialType == DescriptorSetType::UnlitMaterialTextures) {
        // Unlit Material layout: UBO + 1 texture
        // Binding 0: Material UBO
        vk::DescriptorSetLayoutBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.descriptorCount = 1;
        uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboBinding.pImmutableSamplers = nullptr;
        uboBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings.push_back(uboBinding);

        // Binding 1: Base color texture
        vk::DescriptorSetLayoutBinding textureBinding;
        textureBinding.binding = 1;
        textureBinding.descriptorCount = 1;
        textureBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        textureBinding.pImmutableSamplers = nullptr;
        textureBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings.push_back(textureBinding);
    } else if (materialType == DescriptorSetType::None) {
        // 不创建任何descriptor set layout - 仅使用全局set
        materialDescriptorSetLayout = nullptr;
        return;
    }

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

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
    return pipeline ? pipeline->getPipelineLayout() : vk::PipelineLayout{};
}

vk::DescriptorSetLayout Material::getDescriptorSetLayout() const {
    return materialDescriptorSetLayout;
}

// PBRMaterialInstance implementation
PBRMaterialInstance::~PBRMaterialInstance() {
    cleanup();
}

void PBRMaterialInstance::create(VulkanContext* ctx, Material* mat) {
    context = ctx;
    material = mat;

    // Initialize material instance data
    data = PBRMaterialData{};

    // Create uniform buffer for material data
    uniformBuffer = new UniformBuffer();
    uniformBuffer->create(context, sizeof(PBRMaterialData));
}

void PBRMaterialInstance::createDescriptorSet(uint32_t maxFramesInFlight) {
    // 使用Material的descriptor set layout创建descriptor set实例
    descriptorSet = new DescriptorSet();
    descriptorSet->create(context, maxFramesInFlight, DescriptorSetType::MaterialTextures);
}

void PBRMaterialInstance::cleanup() {
    if (descriptorSet) {
        descriptorSet->cleanup();
        delete descriptorSet;
        descriptorSet = nullptr;
    }

    if (uniformBuffer) {
        uniformBuffer->cleanup();
        delete uniformBuffer;
        uniformBuffer = nullptr;
    }
}

void PBRMaterialInstance::updateDescriptorSet(uint32_t frameIndex) {
    if (!descriptorSet) {
        violet::Log::error("Renderer", "PBRMaterialInstance: descriptorSet is null - cannot update for frameIndex {}", frameIndex);
        return;
    }

    if (!dirty) {
        return;
    }

    // Descriptor set updates happen every frame, no need to log

    // Update uniform buffer
    if (uniformBuffer) {
        uniformBuffer->update(&data, sizeof(PBRMaterialData));
        descriptorSet->updateUniformBuffer(frameIndex, uniformBuffer, 0);
    } else {
        violet::Log::error("Renderer", "PBRMaterialInstance: uniformBuffer is null for frameIndex {}", frameIndex);
        return; // Cannot continue without uniform buffer
    }

    // Force binding all texture slots to avoid descriptor set inconsistencies
    // This ensures every binding has a valid texture even if some are null
    descriptorSet->updateTexture(frameIndex, baseColorTexture, 1);
    descriptorSet->updateTexture(frameIndex, metallicRoughnessTexture, 2);
    descriptorSet->updateTexture(frameIndex, normalTexture, 3);
    descriptorSet->updateTexture(frameIndex, occlusionTexture, 4);
    descriptorSet->updateTexture(frameIndex, emissiveTexture, 5);

    dirty = false;
    // Update completed
}

// UnlitMaterialInstance implementation
UnlitMaterialInstance::~UnlitMaterialInstance() {
    cleanup();
}

void UnlitMaterialInstance::create(VulkanContext* ctx, Material* mat) {
    context = ctx;
    material = mat;

    // Initialize material instance data
    data = UnlitMaterialData{};

    // Create uniform buffer for material data
    uniformBuffer = new UniformBuffer();
    uniformBuffer->create(context, sizeof(UnlitMaterialData));
}

void UnlitMaterialInstance::createDescriptorSet(uint32_t maxFramesInFlight) {
    // 使用Unlit材质的descriptor set layout创建descriptor set实例
    descriptorSet = new DescriptorSet();
    descriptorSet->create(context, maxFramesInFlight, DescriptorSetType::UnlitMaterialTextures);
}

void UnlitMaterialInstance::cleanup() {
    if (descriptorSet) {
        descriptorSet->cleanup();
        delete descriptorSet;
        descriptorSet = nullptr;
    }

    if (uniformBuffer) {
        uniformBuffer->cleanup();
        delete uniformBuffer;
        uniformBuffer = nullptr;
    }
}

void UnlitMaterialInstance::updateDescriptorSet(uint32_t frameIndex) {
    if (!descriptorSet) {
        violet::Log::error("Renderer", "UnlitMaterialInstance: descriptorSet is null - cannot update for frameIndex {}", frameIndex);
        return;
    }

    if (!dirty) {
        return;
    }

    // Descriptor set updates happen every frame, no need to log

    // Update uniform buffer
    if (uniformBuffer) {
        uniformBuffer->update(&data, sizeof(UnlitMaterialData));
        descriptorSet->updateUniformBuffer(frameIndex, uniformBuffer, 0);
    } else {
        violet::Log::error("Renderer", "UnlitMaterialInstance: uniformBuffer is null for frameIndex {}", frameIndex);
        return;
    }

    // Update base color texture only
    if (baseColorTexture) {
        descriptorSet->updateTexture(frameIndex, baseColorTexture, 1);
    } else {
        // Using default texture
    }

    dirty = false;
    // Update completed
}

} // namespace violet