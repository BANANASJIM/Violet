#include "Material.hpp"
#include "renderer/core/VulkanContext.hpp"
#include "renderer/pipeline/GraphicsPipeline.hpp"
#include "renderer/DescriptorSet.hpp"
#include "renderer/descriptor/DescriptorManager.hpp"
#include "resource/gpu/UniformBuffer.hpp"
#include "resource/Texture.hpp"
#include "core/Log.hpp"
#include <EASTL/vector.h>

namespace violet {

Material::~Material() {
    cleanup();
}

void Material::create(VulkanContext* ctx) {
    context = ctx;
    // NOTE: Descriptor set layout creation removed
    // Layouts are now managed centrally by DescriptorManager
    // Material no longer owns descriptor set layouts
}

void Material::cleanup() {
    // NOTE: Material no longer owns descriptor set layouts
    // Layouts are managed centrally by DescriptorManager

    if (pipeline) {
        pipeline->cleanup();
        pipeline.reset();
    }
}

vk::PipelineLayout Material::getPipelineLayout() const {
    return pipeline ? pipeline->getPipelineLayout() : vk::PipelineLayout{};
}

// PBRMaterialInstance implementation
PBRMaterialInstance::~PBRMaterialInstance() {
    cleanup();
}

void PBRMaterialInstance::create(VulkanContext* ctx, Material* mat, DescriptorManager* descMgr) {
    context = ctx;
    material = mat;
    descriptorManager = descMgr;

    // Initialize material data with defaults
    data = PBRMaterialData{};

    // Allocate material ID in SSBO (all fields initialized to 0/default)
    DescriptorManager::MaterialData materialData{
        .baseColorFactor = data.baseColorFactor,
        .metallicFactor = data.metallicFactor,
        .roughnessFactor = data.roughnessFactor,
        .normalScale = data.normalScale,
        .occlusionStrength = data.occlusionStrength,
        .emissiveFactor = data.emissiveFactor,
        .alphaCutoff = data.alphaCutoff,
        // Texture indices default to 0 (no texture)
        .baseColorTexIndex = 0,
        .metallicRoughnessTexIndex = 0,
        .normalTexIndex = 0,
        .occlusionTexIndex = 0,
        .emissiveTexIndex = 0
    };

    materialID = descriptorManager->allocateMaterialData(materialData);
    if (materialID == 0) {
        violet::Log::error("Renderer", "Failed to allocate material ID for PBRMaterialInstance");
    }
}

void PBRMaterialInstance::cleanup() {
    if (!descriptorManager || materialID == 0) {
        return;
    }

    // Get current material data to retrieve texture indices
    const auto* matData = descriptorManager->getMaterialData(materialID);
    if (matData) {
        // Free all bindless texture indices
        if (matData->baseColorTexIndex != 0) {
            descriptorManager->freeBindlessTexture(matData->baseColorTexIndex);
        }
        if (matData->metallicRoughnessTexIndex != 0) {
            descriptorManager->freeBindlessTexture(matData->metallicRoughnessTexIndex);
        }
        if (matData->normalTexIndex != 0) {
            descriptorManager->freeBindlessTexture(matData->normalTexIndex);
        }
        if (matData->occlusionTexIndex != 0) {
            descriptorManager->freeBindlessTexture(matData->occlusionTexIndex);
        }
        if (matData->emissiveTexIndex != 0) {
            descriptorManager->freeBindlessTexture(matData->emissiveTexIndex);
        }
    }

    // Free material ID slot
    descriptorManager->freeMaterialData(materialID);
    materialID = 0;
}

void PBRMaterialInstance::setBaseColorTexture(Texture* texture) {
    if (baseColorTexture == texture || !descriptorManager || materialID == 0) return;

    // Get current material data
    const auto* matData = descriptorManager->getMaterialData(materialID);
    if (!matData) return;

    // Free old texture index
    if (matData->baseColorTexIndex != 0) {
        descriptorManager->freeBindlessTexture(matData->baseColorTexIndex);
    }

    // Update texture pointer
    baseColorTexture = texture;

    // Allocate new texture index and update SSBO
    DescriptorManager::MaterialData updatedData = *matData;
    updatedData.baseColorTexIndex = texture ? descriptorManager->allocateBindlessTexture(texture) : 0;
    descriptorManager->updateMaterialData(materialID, updatedData);
}

void PBRMaterialInstance::setMetallicRoughnessTexture(Texture* texture) {
    if (metallicRoughnessTexture == texture || !descriptorManager || materialID == 0) return;

    const auto* matData = descriptorManager->getMaterialData(materialID);
    if (!matData) return;

    if (matData->metallicRoughnessTexIndex != 0) {
        descriptorManager->freeBindlessTexture(matData->metallicRoughnessTexIndex);
    }

    metallicRoughnessTexture = texture;

    DescriptorManager::MaterialData updatedData = *matData;
    updatedData.metallicRoughnessTexIndex = texture ? descriptorManager->allocateBindlessTexture(texture) : 0;
    descriptorManager->updateMaterialData(materialID, updatedData);

    violet::Log::debug("Material", "Set metallicRoughness texture for material {}: bindless index = {}, factor = {:.2f}/{:.2f}",
        materialID, updatedData.metallicRoughnessTexIndex, updatedData.metallicFactor, updatedData.roughnessFactor);
}

void PBRMaterialInstance::setNormalTexture(Texture* texture) {
    if (normalTexture == texture || !descriptorManager || materialID == 0) return;

    const auto* matData = descriptorManager->getMaterialData(materialID);
    if (!matData) return;

    if (matData->normalTexIndex != 0) {
        descriptorManager->freeBindlessTexture(matData->normalTexIndex);
    }

    normalTexture = texture;

    DescriptorManager::MaterialData updatedData = *matData;
    updatedData.normalTexIndex = texture ? descriptorManager->allocateBindlessTexture(texture) : 0;
    descriptorManager->updateMaterialData(materialID, updatedData);
}

void PBRMaterialInstance::setOcclusionTexture(Texture* texture) {
    if (occlusionTexture == texture || !descriptorManager || materialID == 0) return;

    const auto* matData = descriptorManager->getMaterialData(materialID);
    if (!matData) return;

    if (matData->occlusionTexIndex != 0) {
        descriptorManager->freeBindlessTexture(matData->occlusionTexIndex);
    }

    occlusionTexture = texture;

    DescriptorManager::MaterialData updatedData = *matData;
    updatedData.occlusionTexIndex = texture ? descriptorManager->allocateBindlessTexture(texture) : 0;
    descriptorManager->updateMaterialData(materialID, updatedData);
}

void PBRMaterialInstance::setEmissiveTexture(Texture* texture) {
    if (emissiveTexture == texture || !descriptorManager || materialID == 0) return;

    const auto* matData = descriptorManager->getMaterialData(materialID);
    if (!matData) return;

    if (matData->emissiveTexIndex != 0) {
        descriptorManager->freeBindlessTexture(matData->emissiveTexIndex);
    }

    emissiveTexture = texture;

    DescriptorManager::MaterialData updatedData = *matData;
    updatedData.emissiveTexIndex = texture ? descriptorManager->allocateBindlessTexture(texture) : 0;
    descriptorManager->updateMaterialData(materialID, updatedData);
}

void PBRMaterialInstance::updateMaterialData() {
    if (!descriptorManager || materialID == 0) {
        return;
    }

    // Get current material data to preserve texture indices
    const auto* matData = descriptorManager->getMaterialData(materialID);
    if (!matData) return;

    // Update only material parameters, keep texture indices unchanged
    DescriptorManager::MaterialData updatedData = *matData;
    updatedData.baseColorFactor = data.baseColorFactor;
    updatedData.metallicFactor = data.metallicFactor;
    updatedData.roughnessFactor = data.roughnessFactor;
    updatedData.normalScale = data.normalScale;
    updatedData.occlusionStrength = data.occlusionStrength;
    updatedData.emissiveFactor = data.emissiveFactor;
    updatedData.alphaCutoff = data.alphaCutoff;

    descriptorManager->updateMaterialData(materialID, updatedData);
}

// UnlitMaterialInstance implementation
UnlitMaterialInstance::~UnlitMaterialInstance() {
    cleanup();
}

void UnlitMaterialInstance::create(VulkanContext* ctx, Material* mat, DescriptorManager* descMgr) {
    context = ctx;
    material = mat;
    descriptorManager = descMgr;

    // Initialize material data with defaults
    data = UnlitMaterialData{};

    // Allocate material ID in SSBO
    DescriptorManager::MaterialData materialData{
        .baseColorFactor = data.baseColor,
        .metallicFactor = 0.0f,
        .roughnessFactor = 1.0f,
        .normalScale = 1.0f,
        .occlusionStrength = 1.0f,
        .emissiveFactor = glm::vec3(0.0f),
        .alphaCutoff = 0.5f,
        // Texture index defaults to 0
        .baseColorTexIndex = 0
    };

    materialID = descriptorManager->allocateMaterialData(materialData);
    if (materialID == 0) {
        violet::Log::error("Renderer", "Failed to allocate material ID for UnlitMaterialInstance");
    }
}

void UnlitMaterialInstance::cleanup() {
    if (!descriptorManager || materialID == 0) {
        return;
    }

    // Get current material data to retrieve texture index
    const auto* matData = descriptorManager->getMaterialData(materialID);
    if (matData && matData->baseColorTexIndex != 0) {
        descriptorManager->freeBindlessTexture(matData->baseColorTexIndex);
    }

    // Free material ID slot
    descriptorManager->freeMaterialData(materialID);
    materialID = 0;
}

void UnlitMaterialInstance::setBaseColorTexture(Texture* texture) {
    if (baseColorTexture == texture || !descriptorManager || materialID == 0) return;

    // Get current material data
    const auto* matData = descriptorManager->getMaterialData(materialID);
    if (!matData) return;

    // Free old texture index
    if (matData->baseColorTexIndex != 0) {
        descriptorManager->freeBindlessTexture(matData->baseColorTexIndex);
    }

    // Update texture pointer
    baseColorTexture = texture;

    // Allocate new texture index and update SSBO
    DescriptorManager::MaterialData updatedData = *matData;
    updatedData.baseColorTexIndex = texture ? descriptorManager->allocateBindlessTexture(texture) : 0;
    descriptorManager->updateMaterialData(materialID, updatedData);
}

void UnlitMaterialInstance::updateMaterialData() {
    if (!descriptorManager || materialID == 0) {
        return;
    }

    // Get current material data to preserve texture index
    const auto* matData = descriptorManager->getMaterialData(materialID);
    if (!matData) return;

    // Update only material parameters, keep texture index unchanged
    DescriptorManager::MaterialData updatedData = *matData;
    updatedData.baseColorFactor = data.baseColor;

    descriptorManager->updateMaterialData(materialID, updatedData);
}

} // namespace violet