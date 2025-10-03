#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <functional>

namespace violet {

class VulkanContext;

// Pass type enumeration
enum class PassType {
    Graphics,    // Graphics rendering pass
    Compute,     // Compute pass
    Transfer     // Transfer pass (future)
};

// Base configuration for all passes
struct PassConfigBase {
    eastl::string name;
    PassType type;

    // Pipeline barrier configuration for multi-pass synchronization
    vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
    vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eTopOfPipe;
    vk::AccessFlags srcAccess = {};
    vk::AccessFlags dstAccess = {};

    // Execution callback - invoked during pass execution
    eastl::function<void(vk::CommandBuffer, uint32_t)> execute;
};

// Unified pass interface for all pass types
class Pass {
public:
    virtual ~Pass() = default;

    // Lifecycle management
    virtual void cleanup() = 0;

    // Pass execution interface
    virtual void begin(vk::CommandBuffer cmd, uint32_t frameIndex) = 0;
    virtual void execute(vk::CommandBuffer cmd, uint32_t frameIndex) = 0;
    virtual void end(vk::CommandBuffer cmd) = 0;

    // Pass metadata
    virtual PassType getType() const = 0;
    virtual const eastl::string& getName() const = 0;

    // Barrier configuration access
    virtual vk::PipelineStageFlags getSrcStage() const = 0;
    virtual vk::PipelineStageFlags getDstStage() const = 0;
    virtual vk::AccessFlags getSrcAccess() const = 0;
    virtual vk::AccessFlags getDstAccess() const = 0;
};

} // namespace violet
