#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>

namespace violet {

enum class FieldType : uint8_t {
    Float, Vec2, Vec3, Vec4,
    Int, IVec2, IVec3, IVec4,
    UInt, UVec2, UVec3, UVec4,
    Mat4,
    Unknown
};

// Field in UBO/SSBO
struct ReflectedField {
    eastl::string name;
    uint32_t offset;
    uint32_t size;
    FieldType type;
};

// UBO/SSBO layout
struct ReflectedBuffer {
    eastl::string name;
    uint32_t binding;
    uint32_t set;
    uint32_t totalSize;
    eastl::vector<ReflectedField> fields;
};

// Shader reflection container
class ShaderReflection {
public:
    void addBuffer(const ReflectedBuffer& buffer) {
        buffers.push_back(buffer);
    }

    const ReflectedBuffer* findBuffer(const eastl::string& name) const {
        for (const auto& buf : buffers) {
            if (buf.name == name) return &buf;
        }
        return nullptr;
    }

    const ReflectedField* findField(const eastl::string& bufferName, const eastl::string& fieldName) const {
        const ReflectedBuffer* buf = findBuffer(bufferName);
        if (!buf) return nullptr;

        for (const auto& field : buf->fields) {
            if (field.name == fieldName) return &field;
        }
        return nullptr;
    }

    const eastl::vector<ReflectedBuffer>& getBuffers() const { return buffers; }
    void clear() { buffers.clear(); }

private:
    eastl::vector<ReflectedBuffer> buffers;
};

// Extract reflection from Slang (opaque pointer to avoid slang.h dependency)
bool extractReflection(void* slangProgramLayout, ShaderReflection& reflection);

} // namespace violet