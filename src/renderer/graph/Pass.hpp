#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/functional.h>

namespace violet {

class VulkanContext;
class RenderGraph;
using ResourceHandle = uint32_t;

// Pass type enumeration
enum class PassType {
    Graphics,    // Graphics rendering pass
    Compute,     // Compute pass
    Transfer     // Transfer pass for GPU resource transfers
};

// Unified pass interface - simplified for RenderGraph integration
class Pass {
public:
    virtual ~Pass() = default;

    // Core execution (single entry point)
    virtual void execute(vk::CommandBuffer cmd, uint32_t frameIndex) = 0;

    // Pass metadata
    virtual PassType getType() const = 0;
    virtual const eastl::string& getName() const = 0;

    // Resource dependencies (for RenderGraph)
    virtual const eastl::vector<ResourceHandle>& getReadResources() const = 0;
    virtual const eastl::vector<ResourceHandle>& getWriteResources() const = 0;

    // Cleanup
    virtual void cleanup() = 0;
};

} // namespace violet
