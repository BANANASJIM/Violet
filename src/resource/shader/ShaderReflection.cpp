#include "ShaderReflection.hpp"
#include <slang.h>
#include "core/Log.hpp"

namespace violet {

namespace {

FieldType slangTypeToFieldType(slang::TypeReflection* type) {
    if (!type) return FieldType::Unknown;

    auto kind = type->getKind();

    if (kind == slang::TypeReflection::Kind::Scalar) {
        auto scalarType = type->getScalarType();
        if (scalarType == slang::TypeReflection::ScalarType::Float32) return FieldType::Float;
        if (scalarType == slang::TypeReflection::ScalarType::Int32) return FieldType::Int;
        if (scalarType == slang::TypeReflection::ScalarType::UInt32) return FieldType::UInt;
    }
    else if (kind == slang::TypeReflection::Kind::Vector) {
        auto elemType = type->getElementType();
        auto count = type->getElementCount();

        if (elemType->getScalarType() == slang::TypeReflection::ScalarType::Float32) {
            if (count == 2) return FieldType::Vec2;
            if (count == 3) return FieldType::Vec3;
            if (count == 4) return FieldType::Vec4;
        }
        else if (elemType->getScalarType() == slang::TypeReflection::ScalarType::Int32) {
            if (count == 2) return FieldType::IVec2;
            if (count == 3) return FieldType::IVec3;
            if (count == 4) return FieldType::IVec4;
        }
        else if (elemType->getScalarType() == slang::TypeReflection::ScalarType::UInt32) {
            if (count == 2) return FieldType::UVec2;
            if (count == 3) return FieldType::UVec3;
            if (count == 4) return FieldType::UVec4;
        }
    }
    else if (kind == slang::TypeReflection::Kind::Matrix) {
        auto rowCount = type->getRowCount();
        auto colCount = type->getColumnCount();
        if (rowCount == 4 && colCount == 4) return FieldType::Mat4;
    }

    return FieldType::Unknown;
}

void extractFields(slang::TypeLayoutReflection* typeLayout, eastl::vector<ReflectedField>& fields, uint32_t baseOffset = 0) {
    if (!typeLayout) return;

    uint32_t fieldCount = typeLayout->getFieldCount();
    for (uint32_t i = 0; i < fieldCount; i++) {
        auto field = typeLayout->getFieldByIndex(i);
        if (!field) continue;

        ReflectedField reflectedField;
        reflectedField.name = field->getName();
        reflectedField.offset = baseOffset + uint32_t(field->getOffset());
        reflectedField.size = uint32_t(field->getTypeLayout()->getSize());
        reflectedField.type = slangTypeToFieldType(field->getType());

        fields.push_back(reflectedField);
    }
}

} // anonymous namespace

// Get shader stage flags from program layout
vk::ShaderStageFlags getShaderStageFlags(slang::ProgramLayout* programLayout) {
    vk::ShaderStageFlags flags = {};
    uint32_t entryPointCount = programLayout->getEntryPointCount();

    for (uint32_t i = 0; i < entryPointCount; ++i) {
        auto entryPoint = programLayout->getEntryPointByIndex(i);
        if (!entryPoint) continue;

        switch (entryPoint->getStage()) {
            case SLANG_STAGE_VERTEX:
                flags |= vk::ShaderStageFlagBits::eVertex;
                break;
            case SLANG_STAGE_FRAGMENT:
                flags |= vk::ShaderStageFlagBits::eFragment;
                break;
            case SLANG_STAGE_COMPUTE:
                flags |= vk::ShaderStageFlagBits::eCompute;
                break;
            case SLANG_STAGE_GEOMETRY:
                flags |= vk::ShaderStageFlagBits::eGeometry;
                break;
            case SLANG_STAGE_HULL:
                flags |= vk::ShaderStageFlagBits::eTessellationControl;
                break;
            case SLANG_STAGE_DOMAIN:
                flags |= vk::ShaderStageFlagBits::eTessellationEvaluation;
                break;
        }
    }

    return flags;
}

// Extract reflection from Slang ProgramLayout
bool extractReflection(void* slangProgramLayout, ShaderReflection& reflection) {
    if (!slangProgramLayout) {
        Log::error("ShaderReflection", "Invalid program layout");
        return false;
    }

    auto* programLayout = static_cast<slang::ProgramLayout*>(slangProgramLayout);
    reflection.clear();

    // Get global parameters
    auto globalParams = programLayout->getGlobalParamsVarLayout();
    if (!globalParams) {
        Log::warn("ShaderReflection", "No global parameters found");
        return true;
    }

    auto typeLayout = globalParams->getTypeLayout();
    if (!typeLayout) {
        Log::warn("ShaderReflection", "No type layout found");
        return true;
    }

    vk::ShaderStageFlags stageFlags = getShaderStageFlags(programLayout);

    // Extract each field (descriptor binding)
    uint32_t fieldCount = typeLayout->getFieldCount();
    for (uint32_t i = 0; i < fieldCount; i++) {
        auto varLayout = typeLayout->getFieldByIndex(i);
        if (!varLayout) continue;

        auto paramType = varLayout->getType();
        if (!paramType) continue;

        auto kind = paramType->getKind();

        // Create unified ReflectedResource
        ReflectedResource resource;
        resource.name = varLayout->getName();
        resource.binding = varLayout->getBindingIndex();
        resource.set = varLayout->getBindingSpace();
        resource.stages = stageFlags;
        resource.arraySize = 1;
        resource.isBindless = false;
        resource.bufferLayout = nullptr;

        // Determine descriptor type and extract details
        switch (kind) {
            case slang::TypeReflection::Kind::ConstantBuffer: {
                // UBO (UniformBuffer)
                resource.type = vk::DescriptorType::eUniformBuffer;

                // Extract detailed buffer layout
                ReflectedBuffer buffer;
                buffer.name = varLayout->getName();
                buffer.binding = varLayout->getBindingIndex();
                buffer.set = varLayout->getBindingSpace();

                auto elementTypeLayout = varLayout->getTypeLayout()->getElementTypeLayout();
                if (elementTypeLayout) {
                    buffer.totalSize = uint32_t(elementTypeLayout->getSize());
                    extractFields(elementTypeLayout, buffer.fields);
                }

                reflection.addBuffer(buffer);
                resource.bufferLayout = reflection.findBuffer(buffer.name);
                break;
            }

            case slang::TypeReflection::Kind::Resource: {
                auto shape = paramType->getResourceShape();
                auto access = paramType->getResourceAccess();

                // Texture types
                if (shape == SLANG_TEXTURE_1D || shape == SLANG_TEXTURE_2D ||
                    shape == SLANG_TEXTURE_3D || shape == SLANG_TEXTURE_CUBE) {

                    if (access == SLANG_RESOURCE_ACCESS_READ_WRITE) {
                        // RWTexture -> StorageImage
                        resource.type = vk::DescriptorType::eStorageImage;
                    } else {
                        // Texture -> CombinedImageSampler
                        resource.type = vk::DescriptorType::eCombinedImageSampler;
                    }
                }
                // Structured Buffer
                else if (shape == SLANG_STRUCTURED_BUFFER) {
                    resource.type = vk::DescriptorType::eStorageBuffer;

                    // Extract buffer layout for SSBO
                    ReflectedBuffer buffer;
                    buffer.name = varLayout->getName();
                    buffer.binding = varLayout->getBindingIndex();
                    buffer.set = varLayout->getBindingSpace();

                    auto elementTypeLayout = varLayout->getTypeLayout()->getElementTypeLayout();
                    if (elementTypeLayout) {
                        buffer.totalSize = uint32_t(elementTypeLayout->getSize());
                        extractFields(elementTypeLayout, buffer.fields);
                    }

                    reflection.addBuffer(buffer);
                    resource.bufferLayout = reflection.findBuffer(buffer.name);
                }
                break;
            }

            case slang::TypeReflection::Kind::SamplerState: {
                resource.type = vk::DescriptorType::eSampler;
                break;
            }

            case slang::TypeReflection::Kind::Array: {
                auto elementType = paramType->getElementType();
                uint32_t arraySize = paramType->getElementCount();

                if (elementType && elementType->getKind() == slang::TypeReflection::Kind::Resource) {
                    auto shape = elementType->getResourceShape();
                    auto access = elementType->getResourceAccess();

                    // Determine array element type
                    if (shape == SLANG_TEXTURE_1D || shape == SLANG_TEXTURE_2D ||
                        shape == SLANG_TEXTURE_3D || shape == SLANG_TEXTURE_CUBE) {

                        if (access == SLANG_RESOURCE_ACCESS_READ_WRITE) {
                            resource.type = vk::DescriptorType::eStorageImage;
                        } else {
                            resource.type = vk::DescriptorType::eCombinedImageSampler;
                        }
                    }

                    // Check if bindless (unbounded or very large array)
                    if (arraySize == 0 || arraySize > 100) {
                        resource.isBindless = true;
                        resource.arraySize = (arraySize == 0) ? 1024 : arraySize;
                        Log::info("ShaderReflection", "Detected bindless array: {} (size: {})",
                                 resource.name.c_str(), resource.arraySize);
                    } else {
                        resource.arraySize = arraySize;
                    }
                }
                break;
            }

            default:
                // Skip unknown types
                continue;
        }

        // Add to reflection
        reflection.addResource(resource);
    }

    return true;
}

} // namespace violet