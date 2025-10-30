#include "Material.hpp"
#include "core/Log.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/GraphicsPipeline.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/vulkan/ShaderResources.hpp"
#include "resource/MaterialManager.hpp"
#include "resource/Texture.hpp"

namespace violet {

// Helper: Write material data to all frames (materials buffer is PerFrame but data is static)
template<typename Func>
static void writeToAllFrames(ShaderResources* materialsBuffer, Func&& writeFunc) {
    if (!materialsBuffer) return;
    uint32_t savedFrame = materialsBuffer->getCurrentFrame();
    for (uint32_t frame = 0; frame < 3; ++frame) {
        materialsBuffer->setCurrentFrame(frame);
        writeFunc();
    }
    materialsBuffer->setCurrentFrame(savedFrame);
}

// ===== Material Implementation =====

Material::~Material() {
    cleanup();
}

void Material::create(VulkanContext* ctx) {
    context = ctx;
}

void Material::cleanup() {
    if (pipeline) {
        pipeline->cleanup();
        pipeline.reset();
    }
}

vk::PipelineLayout Material::getPipelineLayout() const {
    if (pipeline) {
        return pipeline->getPipelineLayout();
    }
    return nullptr;
}

// ===== PBRMaterialInstance Implementation =====

PBRMaterialInstance::~PBRMaterialInstance() {
    cleanup();
}

void PBRMaterialInstance::create(VulkanContext* ctx, Material* mat, MaterialManager* matMgr) {
    context = ctx;
    material = mat;
    materialManager = matMgr;

    // Initialize material data with defaults
    data = PBRMaterialData{};

    // Allocate material ID slot
    materialID = materialManager->allocateMaterialSlot();
    if (materialID == 0) {
        violet::Log::error("Material", "Failed to allocate material slot for PBRMaterialInstance");
        return;
    }

    // Get materials buffer from MaterialManager
    auto* materialsBuffer = materialManager->getMaterialsBuffer();
    if (!materialsBuffer) {
        violet::Log::error("Material", "Materials buffer not initialized");
        materialManager->freeMaterialSlot(materialID);
        materialID = 0;
        return;
    }

    // Initialize all fields in materials SSBO using reflection API (write to all frames)
    writeToAllFrames(materialsBuffer, [&]() {
        auto& matBuf = *materialsBuffer;
        matBuf["materials"][materialID]["baseColorFactor"] = data.baseColorFactor;
        matBuf["materials"][materialID]["metallicFactor"] = data.metallicFactor;
        matBuf["materials"][materialID]["roughnessFactor"] = data.roughnessFactor;
        matBuf["materials"][materialID]["normalScale"] = data.normalScale;
        matBuf["materials"][materialID]["occlusionStrength"] = data.occlusionStrength;
        matBuf["materials"][materialID]["emissiveFactor"] = data.emissiveFactor;
        matBuf["materials"][materialID]["alphaCutoff"] = data.alphaCutoff;

        // Initialize texture indices to 0 (no texture)
        matBuf["materials"][materialID]["baseColorTexIndex"] = 0u;
        matBuf["materials"][materialID]["metallicRoughnessTexIndex"] = 0u;
        matBuf["materials"][materialID]["normalTexIndex"] = 0u;
        matBuf["materials"][materialID]["occlusionTexIndex"] = 0u;
        matBuf["materials"][materialID]["emissiveTexIndex"] = 0u;
    });

    violet::Log::debug("Material", "Created PBRMaterialInstance with materialID {}", materialID);
}

void PBRMaterialInstance::cleanup() {
    if (!materialManager || materialID == 0) {
        return;
    }

    // Free all bindless texture indices tracked locally
    if (baseColorTexIndex != 0) {
        materialManager->freeBindlessTexture(baseColorTexIndex);
        baseColorTexIndex = 0;
    }
    if (metallicRoughnessTexIndex != 0) {
        materialManager->freeBindlessTexture(metallicRoughnessTexIndex);
        metallicRoughnessTexIndex = 0;
    }
    if (normalTexIndex != 0) {
        materialManager->freeBindlessTexture(normalTexIndex);
        normalTexIndex = 0;
    }
    if (occlusionTexIndex != 0) {
        materialManager->freeBindlessTexture(occlusionTexIndex);
        occlusionTexIndex = 0;
    }
    if (emissiveTexIndex != 0) {
        materialManager->freeBindlessTexture(emissiveTexIndex);
        emissiveTexIndex = 0;
    }

    // Free material ID slot
    materialManager->freeMaterialSlot(materialID);
    materialID = 0;

    violet::Log::debug("Material", "Cleaned up PBRMaterialInstance");
}

void PBRMaterialInstance::setBaseColorTexture(Texture* texture) {
    if (baseColorTexture == texture || !materialManager || materialID == 0) {
        return;
    }

    auto* materialsBuffer = materialManager->getMaterialsBuffer();
    if (!materialsBuffer) {
        return;
    }

    // Free old bindless texture index
    if (baseColorTexture && baseColorTexIndex != 0) {
        materialManager->freeBindlessTexture(baseColorTexIndex);
        baseColorTexIndex = 0;
    }

    // Update texture pointer
    baseColorTexture = texture;

    // Allocate new bindless index and update SSBO in all frames
    uint32_t texIndex = 0;
    if (texture) {
        texIndex = materialManager->allocateBindlessTexture(texture);
        baseColorTexIndex = texIndex;  // Track for cleanup
    }

    writeToAllFrames(materialsBuffer, [&]() {
        auto& matBuf = *materialsBuffer;
        matBuf["materials"][materialID]["baseColorTexIndex"] = texIndex;
    });
}

void PBRMaterialInstance::setMetallicRoughnessTexture(Texture* texture) {
    if (metallicRoughnessTexture == texture || !materialManager || materialID == 0) {
        return;
    }

    auto* materialsBuffer = materialManager->getMaterialsBuffer();
    if (!materialsBuffer) {
        return;
    }

    // Free old bindless texture index
    if (metallicRoughnessTexture && metallicRoughnessTexIndex != 0) {
        materialManager->freeBindlessTexture(metallicRoughnessTexIndex);
        metallicRoughnessTexIndex = 0;
    }

    // Update texture pointer
    metallicRoughnessTexture = texture;

    // Allocate new bindless index and update SSBO in all frames
    uint32_t texIndex = 0;
    if (texture) {
        texIndex = materialManager->allocateBindlessTexture(texture);
        metallicRoughnessTexIndex = texIndex;  // Track for cleanup
    }

    writeToAllFrames(materialsBuffer, [&]() {
        auto& matBuf = *materialsBuffer;
        matBuf["materials"][materialID]["metallicRoughnessTexIndex"] = texIndex;
    });
}

void PBRMaterialInstance::setNormalTexture(Texture* texture) {
    if (normalTexture == texture || !materialManager || materialID == 0) {
        return;
    }

    auto* materialsBuffer = materialManager->getMaterialsBuffer();
    if (!materialsBuffer) {
        return;
    }

    // Free old bindless texture index
    if (normalTexture && normalTexIndex != 0) {
        materialManager->freeBindlessTexture(normalTexIndex);
        normalTexIndex = 0;
    }

    // Update texture pointer
    normalTexture = texture;

    // Allocate new bindless index and update SSBO in all frames
    uint32_t texIndex = 0;
    if (texture) {
        texIndex = materialManager->allocateBindlessTexture(texture);
        normalTexIndex = texIndex;  // Track for cleanup
    }

    writeToAllFrames(materialsBuffer, [&]() {
        auto& matBuf = *materialsBuffer;
        matBuf["materials"][materialID]["normalTexIndex"] = texIndex;
    });
}

void PBRMaterialInstance::setOcclusionTexture(Texture* texture) {
    if (occlusionTexture == texture || !materialManager || materialID == 0) {
        return;
    }

    auto* materialsBuffer = materialManager->getMaterialsBuffer();
    if (!materialsBuffer) {
        return;
    }

    // Free old bindless texture index
    if (occlusionTexture && occlusionTexIndex != 0) {
        materialManager->freeBindlessTexture(occlusionTexIndex);
        occlusionTexIndex = 0;
    }

    // Update texture pointer
    occlusionTexture = texture;

    // Allocate new bindless index and update SSBO in all frames
    uint32_t texIndex = 0;
    if (texture) {
        texIndex = materialManager->allocateBindlessTexture(texture);
        occlusionTexIndex = texIndex;  // Track for cleanup
    }

    writeToAllFrames(materialsBuffer, [&]() {
        auto& matBuf = *materialsBuffer;
        matBuf["materials"][materialID]["occlusionTexIndex"] = texIndex;
    });
}

void PBRMaterialInstance::setEmissiveTexture(Texture* texture) {
    if (emissiveTexture == texture || !materialManager || materialID == 0) {
        return;
    }

    auto* materialsBuffer = materialManager->getMaterialsBuffer();
    if (!materialsBuffer) {
        return;
    }

    // Free old bindless texture index
    if (emissiveTexture && emissiveTexIndex != 0) {
        materialManager->freeBindlessTexture(emissiveTexIndex);
        emissiveTexIndex = 0;
    }

    // Update texture pointer
    emissiveTexture = texture;

    // Allocate new bindless index and update SSBO in all frames
    uint32_t texIndex = 0;
    if (texture) {
        texIndex = materialManager->allocateBindlessTexture(texture);
        emissiveTexIndex = texIndex;  // Track for cleanup
    }

    writeToAllFrames(materialsBuffer, [&]() {
        auto& matBuf = *materialsBuffer;
        matBuf["materials"][materialID]["emissiveTexIndex"] = texIndex;
    });
}

void PBRMaterialInstance::updateMaterialData() {
    if (!materialManager || materialID == 0) {
        return;
    }

    auto* materialsBuffer = materialManager->getMaterialsBuffer();
    if (!materialsBuffer) {
        return;
    }

    // Update all material parameters (not texture indices) in SSBO in all frames
    writeToAllFrames(materialsBuffer, [&]() {
        auto& matBuf = *materialsBuffer;
        matBuf["materials"][materialID]["baseColorFactor"] = data.baseColorFactor;
        matBuf["materials"][materialID]["metallicFactor"] = data.metallicFactor;
        matBuf["materials"][materialID]["roughnessFactor"] = data.roughnessFactor;
        matBuf["materials"][materialID]["normalScale"] = data.normalScale;
        matBuf["materials"][materialID]["occlusionStrength"] = data.occlusionStrength;
        matBuf["materials"][materialID]["emissiveFactor"] = data.emissiveFactor;
        matBuf["materials"][materialID]["alphaCutoff"] = data.alphaCutoff;
    });
}

// ===== UnlitMaterialInstance Implementation =====

UnlitMaterialInstance::~UnlitMaterialInstance() {
    cleanup();
}

void UnlitMaterialInstance::create(VulkanContext* ctx, Material* mat, MaterialManager* matMgr) {
    context = ctx;
    material = mat;
    materialManager = matMgr;

    // Initialize material data with defaults
    data = UnlitMaterialData{};

    // Allocate material ID slot
    materialID = materialManager->allocateMaterialSlot();
    if (materialID == 0) {
        violet::Log::error("Material", "Failed to allocate material slot for UnlitMaterialInstance");
        return;
    }

    auto* materialsBuffer = materialManager->getMaterialsBuffer();
    if (!materialsBuffer) {
        violet::Log::error("Material", "Materials buffer not initialized");
        materialManager->freeMaterialSlot(materialID);
        materialID = 0;
        return;
    }

    // Initialize fields using reflection API (write to all frames)
    writeToAllFrames(materialsBuffer, [&]() {
        auto& matBuf = *materialsBuffer;
        matBuf["materials"][materialID]["baseColor"] = data.baseColor;
        matBuf["materials"][materialID]["baseColorTexIndex"] = 0u;
    });

    violet::Log::debug("Material", "Created UnlitMaterialInstance with materialID {}", materialID);
}

void UnlitMaterialInstance::cleanup() {
    if (!materialManager || materialID == 0) {
        return;
    }

    // Free bindless texture index if allocated
    if (baseColorTexIndex != 0) {
        materialManager->freeBindlessTexture(baseColorTexIndex);
        baseColorTexIndex = 0;
    }

    materialManager->freeMaterialSlot(materialID);
    materialID = 0;

    violet::Log::debug("Material", "Cleaned up UnlitMaterialInstance");
}

void UnlitMaterialInstance::setBaseColorTexture(Texture* texture) {
    if (baseColorTexture == texture || !materialManager || materialID == 0) {
        return;
    }

    auto* materialsBuffer = materialManager->getMaterialsBuffer();
    if (!materialsBuffer) {
        return;
    }

    // Free old bindless texture index
    if (baseColorTexture && baseColorTexIndex != 0) {
        materialManager->freeBindlessTexture(baseColorTexIndex);
        baseColorTexIndex = 0;
    }

    // Update texture pointer
    baseColorTexture = texture;

    // Allocate new bindless index and update SSBO in all frames
    uint32_t texIndex = 0;
    if (texture) {
        texIndex = materialManager->allocateBindlessTexture(texture);
        baseColorTexIndex = texIndex;  // Track for cleanup
    }

    writeToAllFrames(materialsBuffer, [&]() {
        auto& matBuf = *materialsBuffer;
        matBuf["materials"][materialID]["baseColorTexIndex"] = texIndex;
    });
}

void UnlitMaterialInstance::updateMaterialData() {
    if (!materialManager || materialID == 0) {
        return;
    }

    auto* materialsBuffer = materialManager->getMaterialsBuffer();
    if (!materialsBuffer) {
        return;
    }

    writeToAllFrames(materialsBuffer, [&]() {
        auto& matBuf = *materialsBuffer;
        matBuf["materials"][materialID]["baseColor"] = data.baseColor;
    });
}

} // namespace violet