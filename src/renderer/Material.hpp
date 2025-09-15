#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <EASTL/vector.h>

namespace violet {

class VulkanContext;
class Texture;

struct PBRMaterialData {
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor{1.0f};
    float roughnessFactor{1.0f};
    float normalScale{1.0f};
    float occlusionStrength{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float alphaCutoff{0.5f};
};

class Material {
public:
    Material() = default;
    ~Material() = default;

    void setBaseColorTexture(Texture* texture) { baseColorTexture = texture; }
    void setMetallicRoughnessTexture(Texture* texture) { metallicRoughnessTexture = texture; }
    void setNormalTexture(Texture* texture) { normalTexture = texture; }
    void setOcclusionTexture(Texture* texture) { occlusionTexture = texture; }
    void setEmissiveTexture(Texture* texture) { emissiveTexture = texture; }

    Texture* getBaseColorTexture() const { return baseColorTexture; }
    Texture* getMetallicRoughnessTexture() const { return metallicRoughnessTexture; }
    Texture* getNormalTexture() const { return normalTexture; }
    Texture* getOcclusionTexture() const { return occlusionTexture; }
    Texture* getEmissiveTexture() const { return emissiveTexture; }

    PBRMaterialData& getData() { return data; }
    const PBRMaterialData& getData() const { return data; }

    bool isDoubleSided() const { return doubleSided; }
    void setDoubleSided(bool value) { doubleSided = value; }

    enum class AlphaMode {
        Opaque,
        Mask,
        Blend
    };

    AlphaMode getAlphaMode() const { return alphaMode; }
    void setAlphaMode(AlphaMode mode) { alphaMode = mode; }

private:
    PBRMaterialData data;
    
    Texture* baseColorTexture{nullptr};
    Texture* metallicRoughnessTexture{nullptr};
    Texture* normalTexture{nullptr};
    Texture* occlusionTexture{nullptr};
    Texture* emissiveTexture{nullptr};
    
    bool doubleSided{false};
    AlphaMode alphaMode{AlphaMode::Opaque};
};

}
