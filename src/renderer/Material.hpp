#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include "DescriptorSet.hpp"

namespace violet {

class VulkanContext;
class Pipeline;
class Texture;
class UniformBuffer;
class DescriptorSet;

class Material {
public:
    Material() = default;
    ~Material();

    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;

    Material(Material&&) = default;
    Material& operator=(Material&&) = default;

    void create(VulkanContext* context);
    void create(VulkanContext* context, DescriptorSetType materialType);
    void cleanup();

    Pipeline* getPipeline() const { return pipeline; }
    vk::PipelineLayout getPipelineLayout() const;

    // Material管理自己的descriptor set layout
    vk::DescriptorSetLayout getDescriptorSetLayout() const;
    void createDescriptorSetLayout();
    void createDescriptorSetLayout(DescriptorSetType materialType);

    enum class AlphaMode {
        Opaque,
        Mask,
        Blend
    };

    AlphaMode getAlphaMode() const { return alphaMode; }
    void setAlphaMode(AlphaMode mode) { alphaMode = mode; }

    bool isDoubleSided() const { return doubleSided; }
    void setDoubleSided(bool value) { doubleSided = value; }

    Pipeline* pipeline = nullptr;

private:
    VulkanContext* context = nullptr;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;

    // Material管理自己的descriptor set layout
    vk::DescriptorSetLayout materialDescriptorSetLayout;
};

struct PBRMaterialData {
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor{0.0f}; // Default to non-metallic for better visibility
    float roughnessFactor{0.5f}; // Mid-range roughness
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

    virtual void create(VulkanContext* context, Material* material) = 0;
    virtual void cleanup() = 0;
    virtual void setBaseColorTexture(Texture* texture) = 0;
    virtual void updateDescriptorSet(uint32_t frameIndex) = 0;
    virtual void createDescriptorSet(uint32_t maxFramesInFlight) = 0;

    DescriptorSet* getDescriptorSet() const { return descriptorSet; }
    Material* getMaterial() const { return material; }
    void setDirty(bool isDirty) { dirty = isDirty; }

protected:
    VulkanContext* context = nullptr;
    Material* material = nullptr;
    DescriptorSet* descriptorSet = nullptr;
    UniformBuffer* uniformBuffer = nullptr;
    bool dirty = true;
};

class PBRMaterialInstance : public MaterialInstance {
public:
    PBRMaterialInstance() = default;
    ~PBRMaterialInstance();

    void create(VulkanContext* context, Material* material) override;
    void cleanup() override;
    void setBaseColorTexture(Texture* texture) override { baseColorTexture = texture; dirty = true; }
    void updateDescriptorSet(uint32_t frameIndex) override;
    void createDescriptorSet(uint32_t maxFramesInFlight) override;

    void setMetallicRoughnessTexture(Texture* texture) { metallicRoughnessTexture = texture; dirty = true; }
    void setNormalTexture(Texture* texture) { normalTexture = texture; dirty = true; }
    void setOcclusionTexture(Texture* texture) { occlusionTexture = texture; dirty = true; }
    void setEmissiveTexture(Texture* texture) { emissiveTexture = texture; dirty = true; }

    Texture* getBaseColorTexture() const { return baseColorTexture; }
    Texture* getMetallicRoughnessTexture() const { return metallicRoughnessTexture; }
    Texture* getNormalTexture() const { return normalTexture; }
    Texture* getOcclusionTexture() const { return occlusionTexture; }
    Texture* getEmissiveTexture() const { return emissiveTexture; }

    PBRMaterialData& getData() { dirty = true; return data; }
    const PBRMaterialData& getData() const { return data; }

private:
    PBRMaterialData data;
    Texture* baseColorTexture = nullptr;
    Texture* metallicRoughnessTexture = nullptr;
    Texture* normalTexture = nullptr;
    Texture* occlusionTexture = nullptr;
    Texture* emissiveTexture = nullptr;
};

class UnlitMaterialInstance : public MaterialInstance {
public:
    UnlitMaterialInstance() = default;
    ~UnlitMaterialInstance();

    void create(VulkanContext* context, Material* material) override;
    void cleanup() override;
    void setBaseColorTexture(Texture* texture) override { baseColorTexture = texture; dirty = true; }
    void updateDescriptorSet(uint32_t frameIndex) override;
    void createDescriptorSet(uint32_t maxFramesInFlight) override;

    Texture* getBaseColorTexture() const { return baseColorTexture; }

    UnlitMaterialData& getData() { dirty = true; return data; }
    const UnlitMaterialData& getData() const { return data; }

private:
    UnlitMaterialData data;
    Texture* baseColorTexture = nullptr;
};


} // namespace violet