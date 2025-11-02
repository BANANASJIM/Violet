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
        if (!elemType) return FieldType::Unknown;

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

        auto fieldTypeLayout = field->getTypeLayout();
        if (!fieldTypeLayout) {
            Log::warn("ShaderReflection", "Field '{}' has no type layout, skipping", field->getName());
            continue;
        }

        auto fieldType = field->getType();
        if (!fieldType) {
            Log::warn("ShaderReflection", "Field '{}' has no type, skipping", field->getName());
            continue;
        }

        ReflectedField reflectedField;
        reflectedField.name = field->getName();
        reflectedField.offset = baseOffset + uint32_t(field->getOffset());
        reflectedField.size = uint32_t(fieldTypeLayout->getSize());

        auto kind = fieldType->getKind();
        if (kind == slang::TypeReflection::Kind::Array) {
            auto elementType = fieldType->getElementType();
            uint32_t arrayCount = fieldType->getElementCount();

            if (!elementType) {
                Log::warn("ShaderReflection", "Array field '{}' has no element type", field->getName());
                continue;
            }

            reflectedField.type = slangTypeToFieldType(elementType);
            if (reflectedField.type == FieldType::Unknown) {
                continue;
            }

            reflectedField.arraySize = arrayCount;

            auto elementTypeLayout = fieldTypeLayout->getElementTypeLayout();
            reflectedField.arrayStride = elementTypeLayout ?
                uint32_t(elementTypeLayout->getSize()) : (reflectedField.size / arrayCount);

            Log::info("ShaderReflection", "Array field '{}': type={}, count={}, stride={}",
                     field->getName(), static_cast<int>(reflectedField.type),
                     arrayCount, reflectedField.arrayStride);
        } else {
            // Not an array - regular scalar/vector/matrix field
            reflectedField.type = slangTypeToFieldType(fieldType);

            if (reflectedField.type == FieldType::Unknown) {
                Log::debug("ShaderReflection", "Field '{}' has unsupported type, skipping", field->getName());
                continue;
            }

            reflectedField.arraySize = 1;
            reflectedField.arrayStride = 0;
        }

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
            default:
                // Ignore unsupported stages (e.g., ray tracing)
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

    // Extract push constants from entry points (for single-entry-point shaders like shadow)
    uint32_t entryPointCount = programLayout->getEntryPointCount();
    for (uint32_t epIdx = 0; epIdx < entryPointCount; ++epIdx) {
        auto entryPoint = programLayout->getEntryPointByIndex(epIdx);
        if (!entryPoint) continue;

        auto entryTypeLayout = entryPoint->getTypeLayout();
        if (!entryTypeLayout) continue;

        uint32_t entryFieldCount = entryTypeLayout->getFieldCount();
        for (uint32_t i = 0; i < entryFieldCount; ++i) {
            auto varLayout = entryTypeLayout->getFieldByIndex(i);
            if (!varLayout) continue;

            auto category = varLayout->getCategory();
            if (category == slang::ParameterCategory::PushConstantBuffer) {
                auto varTypeLayout = varLayout->getTypeLayout();
                if (varTypeLayout) {
                    auto elementTypeLayout = varTypeLayout->getElementTypeLayout();
                    if (elementTypeLayout) {
                        vk::ShaderStageFlags entryStageFlags = {};
                        switch (entryPoint->getStage()) {
                            case SLANG_STAGE_VERTEX: entryStageFlags = vk::ShaderStageFlagBits::eVertex; break;
                            case SLANG_STAGE_FRAGMENT: entryStageFlags = vk::ShaderStageFlagBits::eFragment; break;
                            case SLANG_STAGE_COMPUTE: entryStageFlags = vk::ShaderStageFlagBits::eCompute; break;
                            default: break;
                        }

                        vk::PushConstantRange range;
                        range.stageFlags = entryStageFlags;
                        range.offset = uint32_t(varLayout->getOffset());
                        range.size = uint32_t(elementTypeLayout->getSize());

                        reflection.addPushConstantRange(range);
                        Log::info("ShaderReflection", "Extracted entry-point push constant '{}': offset={}, size={}, stages={}",
                                 varLayout->getName(), range.offset, range.size,
                                 static_cast<uint32_t>(range.stageFlags));
                    }
                }
            }
        }
    }

    // Extract each field (descriptor binding or push constant)
    uint32_t fieldCount = typeLayout->getFieldCount();
    for (uint32_t i = 0; i < fieldCount; i++) {
        auto varLayout = typeLayout->getFieldByIndex(i);
        if (!varLayout) continue;

        // Check if this is a push constant (must check category first)
        auto category = varLayout->getCategory();
        if (category == slang::ParameterCategory::PushConstantBuffer) {
            // Extract push constant range
            auto varTypeLayout = varLayout->getTypeLayout();
            if (varTypeLayout) {
                // For ConstantBuffer types (including push constants), get element type size
                auto elementTypeLayout = varTypeLayout->getElementTypeLayout();
                if (elementTypeLayout) {
                    vk::PushConstantRange range;
                    range.stageFlags = stageFlags;  // Use global stage flags
                    range.offset = uint32_t(varLayout->getOffset());
                    range.size = uint32_t(elementTypeLayout->getSize());

                    reflection.addPushConstantRange(range);
                    Log::info("ShaderReflection", "Extracted push constant '{}': offset={}, size={}, stages={}",
                             varLayout->getName(), range.offset, range.size,
                             static_cast<uint32_t>(range.stageFlags));
                }
            }
            continue;  // Skip adding push constants to regular resources
        }

        auto paramType = varLayout->getType();
        if (!paramType) continue;

        auto kind = paramType->getKind();

        // Create unified ReflectedResource
        ReflectedResource resource;
        const char* rawName = varLayout->getName();
        if (!rawName || rawName[0] == '\0') {
            // Skip resources without names (internal compiler artifacts)
            continue;
        }
        resource.name = rawName;
        resource.binding = varLayout->getBindingIndex();
        resource.set = varLayout->getBindingSpace();
        resource.stages = stageFlags;
        resource.arraySize = 1;
        resource.isBindless = false;
        resource.bufferLayoutIndex = ~0u;

        // Determine descriptor type and extract details
        switch (kind) {
            case slang::TypeReflection::Kind::ConstantBuffer: {
                // UBO (UniformBuffer)
                resource.type = vk::DescriptorType::eUniformBuffer;

                // Extract detailed buffer layout
                ReflectedBuffer buffer;

                auto varTypeLayout = varLayout->getTypeLayout();
                if (varTypeLayout) {
                    auto elementTypeLayout = varTypeLayout->getElementTypeLayout();
                    if (elementTypeLayout) {
                        buffer.totalSize = uint32_t(elementTypeLayout->getSize());
                        extractFields(elementTypeLayout, buffer.fields);
                    }
                }

                Log::debug("ShaderReflection", "BUFFER EXTRACTION - ConstantBuffer '{}' (set={}, binding={}, size={}B, fields={})",
                          varLayout->getName(), varLayout->getBindingSpace(), varLayout->getBindingIndex(),
                          buffer.totalSize, buffer.fields.size());

                // Store buffer and link to resource
                resource.bufferLayoutIndex = reflection.storeBuffer(buffer);
                Log::debug("ShaderReflection", "  -> Stored at bufferLayoutIndex={}", resource.bufferLayoutIndex);
                break;
            }

            case slang::TypeReflection::Kind::Resource: {
                auto shape = paramType->getResourceShape();
                auto access = paramType->getResourceAccess();

                // Extract base texture shape (ignore flags like ARRAY, MULTISAMPLE, etc.)
                // SLANG_TEXTURE_2D_ARRAY = SLANG_TEXTURE_2D | SLANG_TEXTURE_ARRAY_FLAG (0x02 | 0x40)
                uint32_t baseShape = shape & 0x0F;  // Mask to get base shape only

                // Debug: Log resource type
                Log::debug("ShaderReflection", "Resource '{}': shape={:#x}, baseShape={:#x}, access={:#x}",
                          varLayout->getName(), static_cast<uint32_t>(shape), baseShape, static_cast<uint32_t>(access));

                // Texture types (handles both regular and array textures)
                if (baseShape == SLANG_TEXTURE_1D || baseShape == SLANG_TEXTURE_2D ||
                    baseShape == SLANG_TEXTURE_3D || baseShape == SLANG_TEXTURE_CUBE) {

                    if (access == SLANG_RESOURCE_ACCESS_READ_WRITE) {
                        // RWTexture / RWTexture2DArray -> StorageImage
                        resource.type = vk::DescriptorType::eStorageImage;
                        Log::debug("ShaderReflection", "  -> Mapped to StorageImage (type={})",
                                  static_cast<uint32_t>(resource.type));
                    } else {
                        // Texture / Texture2DArray -> SampledImage (for separate sampler/texture)
                        resource.type = vk::DescriptorType::eSampledImage;
                        Log::debug("ShaderReflection", "  -> Mapped to SampledImage (type={})",
                                  static_cast<uint32_t>(resource.type));
                    }
                }
                // Structured Buffer
                else if (shape == SLANG_STRUCTURED_BUFFER) {
                    resource.type = vk::DescriptorType::eStorageBuffer;

                    // Extract buffer layout for SSBO
                    ReflectedBuffer buffer;

                    auto varTypeLayout = varLayout->getTypeLayout();
                    if (varTypeLayout) {
                        auto elementTypeLayout = varTypeLayout->getElementTypeLayout();
                        if (elementTypeLayout) {
                            buffer.totalSize = uint32_t(elementTypeLayout->getSize());
                            extractFields(elementTypeLayout, buffer.fields);
                        }
                    }

                    Log::debug("ShaderReflection", "BUFFER EXTRACTION - StructuredBuffer '{}' (set={}, binding={}, size={}B, fields={})",
                              varLayout->getName(), varLayout->getBindingSpace(), varLayout->getBindingIndex(),
                              buffer.totalSize, buffer.fields.size());

                    // Store buffer and link to resource
                    resource.bufferLayoutIndex = reflection.storeBuffer(buffer);
                    Log::debug("ShaderReflection", "  -> Stored at bufferLayoutIndex={}", resource.bufferLayoutIndex);

                    // Detect bindless StructuredBuffer
                    // Set 2 + name "materials": Materials buffer (bindless large-capacity SSBO)
                    // TODO: Future optimization - use config file to control bindless sets and capacity
                    if (resource.set == 2 && resource.name == "materials") {
                        resource.isBindless = true;
                        resource.arraySize = 1024;  // Default capacity for bindless SSBO
                        Log::info("ShaderReflection", "Detected bindless StructuredBuffer '{}' in Set {} (capacity: {})",
                                 resource.name.c_str(), resource.set, resource.arraySize);
                    }
                }
                break;
            }

            case slang::TypeReflection::Kind::SamplerState: {
                resource.type = vk::DescriptorType::eSampler;
                Log::debug("ShaderReflection", "SamplerState '{}' -> Mapped to Sampler (type={})",
                          varLayout->getName(), static_cast<uint32_t>(resource.type));
                break;
            }

            case slang::TypeReflection::Kind::Array: {
                auto elementType = paramType->getElementType();
                uint32_t arraySize = paramType->getElementCount();

                if (elementType && elementType->getKind() == slang::TypeReflection::Kind::Resource) {
                    auto shape = elementType->getResourceShape();
                    auto access = elementType->getResourceAccess();

                    // Extract base texture shape (ignore flags)
                    uint32_t baseShape = shape & 0x0F;

                    // Determine array element type (handles both regular and array textures)
                    if (baseShape == SLANG_TEXTURE_1D || baseShape == SLANG_TEXTURE_2D ||
                        baseShape == SLANG_TEXTURE_3D || baseShape == SLANG_TEXTURE_CUBE) {

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