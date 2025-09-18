#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <EASTL/vector.h>
#include <EASTL/string.h>

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
    void cleanup();

    Pipeline* getPipeline() const { return pipeline; }
    vk::PipelineLayout getPipelineLayout() const;

    // Material管理自己的descriptor set layout
    vk::DescriptorSetLayout getDescriptorSetLayout() const;
    void createDescriptorSetLayout();

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
    float metallicFactor{1.0f};
    float roughnessFactor{1.0f};
    float normalScale{1.0f};
    float occlusionStrength{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float alphaCutoff{0.5f};
};

class MaterialInstance {
public:
    MaterialInstance() = default;
    ~MaterialInstance();

    MaterialInstance(const MaterialInstance&) = delete;
    MaterialInstance& operator=(const MaterialInstance&) = delete;

    MaterialInstance(MaterialInstance&&) = default;
    MaterialInstance& operator=(MaterialInstance&&) = default;

    void create(VulkanContext* context, Material* material);
    void cleanup();

    void setBaseColorTexture(Texture* texture) { baseColorTexture = texture; dirty = true; }
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

    void updateDescriptorSet(uint32_t frameIndex);
    DescriptorSet* getDescriptorSet() const { return descriptorSet; }
    Material* getMaterial() const { return material; }

    // MaterialInstance创建自己的descriptor set实例
    void createDescriptorSet(uint32_t maxFramesInFlight);

private:
    VulkanContext* context = nullptr;
    Material* material = nullptr;

    PBRMaterialData data;

    Texture* baseColorTexture = nullptr;
    Texture* metallicRoughnessTexture = nullptr;
    Texture* normalTexture = nullptr;
    Texture* occlusionTexture = nullptr;
    Texture* emissiveTexture = nullptr;

    UniformBuffer* uniformBuffer = nullptr;
    DescriptorSet* descriptorSet = nullptr;

    bool dirty = true;
};

} // namespace violet