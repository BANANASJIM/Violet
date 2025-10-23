#include "ReflectionHelper.hpp"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "renderer/vulkan/DescriptorManager.hpp"  // For DescriptorLayoutDesc
#include "core/Log.hpp"

namespace violet {

ReflectionHelper::ReflectionHelper(slang::ProgramLayout* layout, SlangSession* session)
    : layout(layout), session(session) {
}

eastl::vector<DescriptorLayoutDesc> ReflectionHelper::extractDescriptorLayouts(
    const eastl::string& shaderName
) const {
    if (!layout) return {};

    eastl::vector<DescriptorLayoutDesc> descriptorLayouts;
    auto globalParams = layout->getGlobalParamsVarLayout();
    if (!globalParams) return {};

    auto typeLayout = globalParams->getTypeLayout();
    if (!typeLayout) return {};

    vk::ShaderStageFlags shaderStages = getShaderStageFlags();

    // Process each parameter to extract binding information
    uint32_t paramCount = typeLayout->getFieldCount();
    for (uint32_t i = 0; i < paramCount; ++i) {
        auto varLayout = typeLayout->getFieldByIndex(i);
        if (!varLayout) continue;

        // Get descriptor set (binding space) and binding index
        uint32_t setIndex = varLayout->getBindingSpace();
        uint32_t bindingIndex = varLayout->getBindingIndex();

        // Check for [[bindless]] attribute
        bool isBindlessAttribute = false;
        if (session && varLayout->getVariable()) {
            if (varLayout->getVariable()->findUserAttributeByName(session, "bindless")) {
                isBindlessAttribute = true;
            }
        }

        // Ensure descriptorLayouts has enough capacity
        if (setIndex >= descriptorLayouts.size()) {
            descriptorLayouts.resize(setIndex + 1);
        }

        DescriptorLayoutDesc& layoutDesc = descriptorLayouts[setIndex];

        // Set name (format: "shaderName_setN")
        if (layoutDesc.name.empty()) {
            char buffer[16];
            sprintf(buffer, "%u", setIndex);
            layoutDesc.name = shaderName + "_set" + buffer;
        }

        // Create binding descriptor
        BindingDesc bindingDesc;
        bindingDesc.binding = bindingIndex;
        bindingDesc.stages = shaderStages;

        auto paramType = varLayout->getType();
        if (!paramType) continue;

        // Determine descriptor type and count
        switch (paramType->getKind()) {
            case slang::TypeReflection::Kind::ConstantBuffer:
                bindingDesc.type = vk::DescriptorType::eUniformBuffer;
                bindingDesc.count = 1;
                break;

            case slang::TypeReflection::Kind::Resource: {
                auto shape = paramType->getResourceShape();
                auto access = paramType->getResourceAccess();

                if (shape == SLANG_TEXTURE_1D || shape == SLANG_TEXTURE_2D ||
                    shape == SLANG_TEXTURE_3D || shape == SLANG_TEXTURE_CUBE) {
                    bindingDesc.type = (access == SLANG_RESOURCE_ACCESS_READ_WRITE)
                        ? vk::DescriptorType::eStorageImage
                        : vk::DescriptorType::eCombinedImageSampler;
                } else if (shape == SLANG_STRUCTURED_BUFFER) {
                    bindingDesc.type = vk::DescriptorType::eStorageBuffer;
                }

                bindingDesc.count = paramType->getElementCount();
                if (bindingDesc.count == 0) {
                    bindingDesc.count = 1;
                }
                break;
            }

            case slang::TypeReflection::Kind::SamplerState:
                bindingDesc.type = vk::DescriptorType::eSampler;
                bindingDesc.count = 1;
                break;

            case slang::TypeReflection::Kind::Array: {
                auto elementType = paramType->getElementType();
                if (elementType && elementType->getKind() == slang::TypeReflection::Kind::Resource) {
                    bindingDesc.type = vk::DescriptorType::eCombinedImageSampler;
                }

                uint32_t arraySize = paramType->getElementCount();

                // Priority 1: Explicit [[bindless]] attribute
                if (isBindlessAttribute) {
                    bindingDesc.count = (arraySize == 0) ? 1024 : arraySize;
                    layoutDesc.isBindless = true;
                    layoutDesc.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;

                    // Set per-binding flags for bindless
                    bindingDesc.flags = vk::DescriptorBindingFlagBits::eUpdateAfterBind |
                                       vk::DescriptorBindingFlagBits::ePartiallyBound;
                }
                // Priority 2: Fallback heuristic (unsized or very large arrays)
                else if (arraySize == 0 || arraySize > 10000) {
                    bindingDesc.count = 1024;
                    layoutDesc.isBindless = true;
                    layoutDesc.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;

                    bindingDesc.flags = vk::DescriptorBindingFlagBits::eUpdateAfterBind |
                                       vk::DescriptorBindingFlagBits::ePartiallyBound;

                    Log::warn("ReflectionHelper", "Array detected as bindless via heuristic - consider using [[bindless]] attribute");
                }
                // Normal sized array
                else {
                    bindingDesc.count = arraySize;
                }
                break;
            }

            default:
                continue;
        }

        layoutDesc.bindings.push_back(bindingDesc);
    }

    // Infer update frequency for each set
    for (auto& layoutDesc : descriptorLayouts) {
        if (!layoutDesc.bindings.empty()) {
            layoutDesc.frequency = inferUpdateFrequency(layoutDesc.bindings);
        }
    }

    return descriptorLayouts;
}

eastl::vector<ReflectionHelper::PushConstantInfo> ReflectionHelper::extractPushConstants() const {
    if (!layout) return {};

    eastl::vector<PushConstantInfo> pushConstants;

    auto globalParams = layout->getGlobalParamsVarLayout();
    if (!globalParams) return {};

    auto typeLayout = globalParams->getTypeLayout();
    if (!typeLayout) return {};

    // Check for push constant usage
    size_t pushConstantSize = typeLayout->getSize(SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER);
    if (pushConstantSize > 0) {
        PushConstantInfo info;
        info.offset = 0;
        info.size = static_cast<uint32_t>(pushConstantSize);
        info.stageFlags = getShaderStageFlags();
        pushConstants.push_back(info);
    }

    return pushConstants;
}

vk::ShaderStageFlags ReflectionHelper::getShaderStageFlags() const {
    if (!layout) return {};

    vk::ShaderStageFlags flags = {};
    uint32_t entryPointCount = layout->getEntryPointCount();

    for (uint32_t i = 0; i < entryPointCount; ++i) {
        auto entryPoint = layout->getEntryPointByIndex(i);
        if (entryPoint) {
            flags |= slangStageToVulkan(entryPoint->getStage());
        }
    }

    return flags;
}

vk::DescriptorType ReflectionHelper::slangTypeToVulkan(slang::BindingType type) {
    switch (type) {
        case slang::BindingType::Sampler:
            return vk::DescriptorType::eSampler;
        case slang::BindingType::Texture:
        case slang::BindingType::CombinedTextureSampler:
            return vk::DescriptorType::eCombinedImageSampler;
        case slang::BindingType::RawBuffer:
        case slang::BindingType::MutableRawBuffer:
            return vk::DescriptorType::eStorageBuffer;
        case slang::BindingType::TypedBuffer:
        case slang::BindingType::MutableTypedBuffer:
            return vk::DescriptorType::eStorageBuffer;
        case slang::BindingType::ConstantBuffer:
            return vk::DescriptorType::eUniformBuffer;
        case slang::BindingType::InlineUniformData:
            return vk::DescriptorType::eInlineUniformBlock;
        default:
            return vk::DescriptorType::eUniformBuffer;
    }
}

vk::ShaderStageFlags ReflectionHelper::slangStageToVulkan(SlangStage stage) {
    switch (stage) {
        case SLANG_STAGE_VERTEX:
            return vk::ShaderStageFlagBits::eVertex;
        case SLANG_STAGE_FRAGMENT:
            return vk::ShaderStageFlagBits::eFragment;
        case SLANG_STAGE_COMPUTE:
            return vk::ShaderStageFlagBits::eCompute;
        case SLANG_STAGE_GEOMETRY:
            return vk::ShaderStageFlagBits::eGeometry;
        case SLANG_STAGE_HULL:
            return vk::ShaderStageFlagBits::eTessellationControl;
        case SLANG_STAGE_DOMAIN:
            return vk::ShaderStageFlagBits::eTessellationEvaluation;
        default:
            return {};
    }
}

UpdateFrequency ReflectionHelper::inferUpdateFrequency(const eastl::vector<BindingDesc>& bindings) {
    // Heuristics to determine update frequency based on binding types and counts

    bool hasLargeArray = false;
    bool hasUniformBuffer = false;
    bool hasStorageImage = false;

    for (const auto& binding : bindings) {
        // Large arrays (bindless) are typically Static
        if (binding.count > 100) {
            hasLargeArray = true;
        }

        // Uniform buffers often contain per-frame data (camera, lights)
        if (binding.type == vk::DescriptorType::eUniformBuffer) {
            hasUniformBuffer = true;
        }

        // Storage images are often render targets (per-pass)
        if (binding.type == vk::DescriptorType::eStorageImage) {
            hasStorageImage = true;
        }
    }

    // Decision logic (prioritize from most specific to least specific)
    if (hasLargeArray) {
        return UpdateFrequency::Static;  // Bindless arrays rarely change
    }

    if (hasStorageImage) {
        return UpdateFrequency::PerPass;  // Storage images often used for render targets
    }

    if (hasUniformBuffer) {
        return UpdateFrequency::PerFrame;  // UBOs often contain camera/view/proj
    }

    // Default: Material-level updates (textures, material properties)
    return UpdateFrequency::PerMaterial;
}

} // namespace violet