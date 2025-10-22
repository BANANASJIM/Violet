#include "ReflectionHelper.hpp"
#include "core/Log.hpp"

namespace violet {

ReflectionHelper::ReflectionHelper(slang::ProgramLayout* layout)
    : layout(layout) {
}

eastl::vector<ReflectionHelper::SetLayoutInfo> ReflectionHelper::extractDescriptorSetLayouts() const {
    if (!layout) return {};

    eastl::vector<SetLayoutInfo> setLayouts;
    auto globalParams = layout->getGlobalParamsVarLayout();
    if (!globalParams) return {};

    auto typeLayout = globalParams->getTypeLayout();
    if (!typeLayout) return {};

    // Process each parameter to extract binding information
    uint32_t paramCount = typeLayout->getFieldCount();
    for (uint32_t i = 0; i < paramCount; ++i) {
        auto varLayout = typeLayout->getFieldByIndex(i);
        if (!varLayout) continue;

        auto param = varLayout->getTypeLayout();
        if (!param) continue;

        // Get descriptor set (binding space) and binding index
        uint32_t setIndex = varLayout->getBindingSpace();
        uint32_t binding = varLayout->getBindingIndex();

        // Ensure setLayouts has enough capacity
        if (setIndex >= setLayouts.size()) {
            setLayouts.resize(setIndex + 1);
            for (uint32_t j = 0; j <= setIndex; ++j) {
                setLayouts[j].setIndex = j;
            }
        }

        SetLayoutInfo& setInfo = setLayouts[setIndex];

        BindingInfo bindingInfo;
        bindingInfo.set = setIndex;
        bindingInfo.binding = binding;
        bindingInfo.name = varLayout->getName();
        bindingInfo.stageFlags = getShaderStageFlags();

        auto paramType = varLayout->getType();
        if (!paramType) continue;

        auto kind = paramType->getKind();

        // Determine descriptor type based on resource kind
        switch (kind) {
            case slang::TypeReflection::Kind::ConstantBuffer:
                bindingInfo.type = vk::DescriptorType::eUniformBuffer;
                bindingInfo.descriptorCount = 1;
                break;

            case slang::TypeReflection::Kind::Resource: {
                auto shape = paramType->getResourceShape();
                auto access = paramType->getResourceAccess();

                if (shape == SLANG_TEXTURE_1D || shape == SLANG_TEXTURE_2D ||
                    shape == SLANG_TEXTURE_3D || shape == SLANG_TEXTURE_CUBE) {
                    bindingInfo.type = (access == SLANG_RESOURCE_ACCESS_READ_WRITE)
                        ? vk::DescriptorType::eStorageImage
                        : vk::DescriptorType::eCombinedImageSampler;
                } else if (shape == SLANG_STRUCTURED_BUFFER) {
                    bindingInfo.type = (access == SLANG_RESOURCE_ACCESS_READ_WRITE)
                        ? vk::DescriptorType::eStorageBuffer
                        : vk::DescriptorType::eStorageBuffer;  // Use SSBO for both
                }

                bindingInfo.descriptorCount = paramType->getElementCount();
                if (bindingInfo.descriptorCount == 0) {
                    bindingInfo.descriptorCount = 1;  // Default to 1 for non-array
                }
                break;
            }

            case slang::TypeReflection::Kind::SamplerState:
                bindingInfo.type = vk::DescriptorType::eSampler;
                bindingInfo.descriptorCount = 1;
                break;

            case slang::TypeReflection::Kind::Array: {
                // Handle arrays (e.g., Texture2D textures[])
                auto elementType = paramType->getElementType();
                if (elementType) {
                    auto elementKind = elementType->getKind();
                    if (elementKind == slang::TypeReflection::Kind::Resource) {
                        bindingInfo.type = vk::DescriptorType::eCombinedImageSampler;
                    }
                }

                uint32_t arraySize = paramType->getElementCount();
                bindingInfo.descriptorCount = arraySize;

                // Check for unsized arrays (bindless)
                if (arraySize == 0 || arraySize > 10000) {
                    bindingInfo.isBindless = true;
                    bindingInfo.descriptorCount = 1024;  // Conservative estimate
                    setInfo.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
                }
                break;
            }

            default:
                continue;  // Skip unsupported types
        }

        setInfo.bindings.push_back(bindingInfo);
    }

    return setLayouts;
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

void ReflectionHelper::processParameter(
    slang::VariableLayoutReflection* param,
    uint32_t setIndex,
    eastl::vector<BindingInfo>& bindings
) const {
    if (!param) return;

    BindingInfo binding;
    binding.set = setIndex;
    binding.name = param->getName();

    auto typeLayout = param->getTypeLayout();
    if (typeLayout) {
        binding.descriptorCount = static_cast<uint32_t>(typeLayout->getElementCount());
    }

    bindings.push_back(binding);
}

} // namespace violet