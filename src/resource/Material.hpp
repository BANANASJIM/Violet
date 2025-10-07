#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include "renderer/DescriptorSet.hpp"

namespace violet {

class VulkanContext;
class GraphicsPipeline;
class Texture;
class UniformBuffer;
class DescriptorSet;
class DescriptorManager;

class Material {
public:
    Material() = default;
    ~Material();

    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;

    Material(Material&&) = default;
    Material& operator=(Material&&) = default;

    void create(VulkanContext* context);
    void cleanup();

    GraphicsPipeline* getPipeline() const { return pipeline.get(); }
    vk::PipelineLayout getPipelineLayout() const;

    // NOTE: Descriptor set layouts are now managed centrally by DescriptorManager
    // Material no longer creates or owns descriptor set layouts

    enum class AlphaMode {
        Opaque,
        Mask,
        Blend
    };

    AlphaMode getAlphaMode() const { return alphaMode; }
    void setAlphaMode(AlphaMode mode) { alphaMode = mode; }

    bool isDoubleSided() const { return doubleSided; }
    void setDoubleSided(bool value) { doubleSided = value; }

    eastl::unique_ptr<GraphicsPipeline> pipeline;

private:
    VulkanContext* context = nullptr;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
};

struct PBRMaterialData {
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor{1.0f}; // glTF 2.0 default
    float roughnessFactor{1.0f}; // glTF 2.0 default
    float normalScale{1.0f};
    float occlusionStrength{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float alphaCutoff{0.5f};
};

struct UnlitMaterialData {
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
};

class MaterialInstance {
public:
    MaterialInstance() = default;
    virtual ~MaterialInstance() = default;

    MaterialInstance(const MaterialInstance&) = delete;
    MaterialInstance& operator=(const MaterialInstance&) = delete;

    MaterialInstance(MaterialInstance&&) = default;
    MaterialInstance& operator=(MaterialInstance&&) = default;

    virtual void create(VulkanContext* context, Material* material, DescriptorManager* descMgr) = 0;
    virtual void cleanup() = 0;

    // Bindless API - material ID points to materials[materialID] in SSBO
    virtual uint32_t getMaterialID() const = 0;

    Material* getMaterial() const { return material; }

protected:
    VulkanContext* context = nullptr;
    Material* material = nullptr;
    DescriptorManager* descriptorManager = nullptr;
};

class PBRMaterialInstance : public MaterialInstance {
public:
    PBRMaterialInstance() = default;
    ~PBRMaterialInstance();

    void create(VulkanContext* context, Material* material, DescriptorManager* descMgr) override;
    void cleanup() override;

    // Texture setters - automatically update materialID's texture indices in SSBO
    void setBaseColorTexture(Texture* texture);
    void setMetallicRoughnessTexture(Texture* texture);
    void setNormalTexture(Texture* texture);
    void setOcclusionTexture(Texture* texture);
    void setEmissiveTexture(Texture* texture);

    // Texture getters
    Texture* getBaseColorTexture() const { return baseColorTexture; }
    Texture* getMetallicRoughnessTexture() const { return metallicRoughnessTexture; }
    Texture* getNormalTexture() const { return normalTexture; }
    Texture* getOcclusionTexture() const { return occlusionTexture; }
    Texture* getEmissiveTexture() const { return emissiveTexture; }

    // Material data access
    PBRMaterialData& getData() { return data; }
    const PBRMaterialData& getData() const { return data; }

    // Update material parameters (not textures) in SSBO
    void updateMaterialData();

    // Bindless API implementation
    uint32_t getMaterialID() const override { return materialID; }

private:
    PBRMaterialData data;

    // Texture pointers (for reference)
    Texture* baseColorTexture = nullptr;
    Texture* metallicRoughnessTexture = nullptr;
    Texture* normalTexture = nullptr;
    Texture* occlusionTexture = nullptr;
    Texture* emissiveTexture = nullptr;

    // Material ID - index into materials[] SSBO
    uint32_t materialID = 0;
};

class UnlitMaterialInstance : public MaterialInstance {
public:
    UnlitMaterialInstance() = default;
    ~UnlitMaterialInstance();

    void create(VulkanContext* context, Material* material, DescriptorManager* descMgr) override;
    void cleanup() override;

    // Texture setter - automatically update materialID's texture index in SSBO
    void setBaseColorTexture(Texture* texture);

    // Texture getter
    Texture* getBaseColorTexture() const { return baseColorTexture; }

    // Material data access
    UnlitMaterialData& getData() { return data; }
    const UnlitMaterialData& getData() const { return data; }

    // Update material parameters in SSBO
    void updateMaterialData();

    // Bindless API implementation
    uint32_t getMaterialID() const override { return materialID; }

private:
    UnlitMaterialData data;

    // Texture pointer (for reference)
    Texture* baseColorTexture = nullptr;

    // Material ID - index into materials[] SSBO
    uint32_t materialID = 0;
};


} // namespace violet