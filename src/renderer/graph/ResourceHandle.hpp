#pragma once

#include <cstdint>
#include <EASTL/functional.h>

namespace violet {

struct ResourceHandle {
    uint32_t id = 0;

    // Allocate a new unique handle
    static ResourceHandle allocate() {
        static uint32_t nextId = 1;
        return ResourceHandle{nextId++};
    }

    constexpr bool valid() const noexcept { return id != 0; }
    constexpr operator bool() const noexcept { return valid(); }

    bool operator==(const ResourceHandle& other) const noexcept { return id == other.id; }
    bool operator!=(const ResourceHandle& other) const noexcept { return id != other.id; }
};

constexpr ResourceHandle InvalidResource{0};

} // namespace violet

// EASTL hash function specialization for ResourceHandle
namespace eastl {
    template<>
    struct hash<violet::ResourceHandle> {
        size_t operator()(const violet::ResourceHandle& h) const noexcept {
            return static_cast<size_t>(h.id);
        }
    };
}
