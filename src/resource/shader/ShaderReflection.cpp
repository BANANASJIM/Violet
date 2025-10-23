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

    // Extract each field (descriptor binding)
    uint32_t fieldCount = typeLayout->getFieldCount();
    for (uint32_t i = 0; i < fieldCount; i++) {
        auto varLayout = typeLayout->getFieldByIndex(i);
        if (!varLayout) continue;

        auto paramType = varLayout->getType();
        if (!paramType) continue;

        // Only process ConstantBuffer and StructuredBuffer
        auto kind = paramType->getKind();
        if (kind != slang::TypeReflection::Kind::ConstantBuffer &&
            kind != slang::TypeReflection::Kind::Resource) {
            continue;
        }

        ReflectedBuffer buffer;
        buffer.name = varLayout->getName();
        buffer.binding = varLayout->getBindingIndex();
        buffer.set = varLayout->getBindingSpace();

        // Get element type layout (the struct inside ConstantBuffer<T>)
        auto elementTypeLayout = varLayout->getTypeLayout()->getElementTypeLayout();
        if (elementTypeLayout) {
            buffer.totalSize = uint32_t(elementTypeLayout->getSize());
            extractFields(elementTypeLayout, buffer.fields);
        }

        reflection.addBuffer(buffer);
    }

    return true;
}

} // namespace violet