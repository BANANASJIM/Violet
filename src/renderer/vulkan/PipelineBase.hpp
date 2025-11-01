#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/optional.h>

namespace violet {

class VulkanContext;
class ShaderReflection;

struct BindingKey;
using LayoutHandle = uint32_t;

class PipelineBase {
public:
    PipelineBase() = default;
    virtual ~PipelineBase() = default;

    PipelineBase(const PipelineBase&) = delete;
    PipelineBase& operator=(const PipelineBase&) = delete;

    PipelineBase(PipelineBase&&) = default;
    PipelineBase& operator=(PipelineBase&&) = default;

    virtual void bind(vk::CommandBuffer commandBuffer) = 0;
    virtual vk::PipelineLayout getPipelineLayout() const = 0;
    virtual vk::PipelineBindPoint getBindPoint() const = 0;
    virtual void cleanup();

    const ShaderReflection* getReflection() const { return mergedReflection.get(); }
    eastl::optional<BindingKey> getBindingKey(const eastl::string& name) const;
    LayoutHandle getLayoutHandle(uint32_t set) const;
    const eastl::vector<LayoutHandle>& getLayoutHandles() const { return layoutHandles; }

protected:
    eastl::vector<char> readFile(const eastl::string& filename);
    vk::raii::ShaderModule createShaderModule(const eastl::vector<char>& code);

protected:
    VulkanContext* context = nullptr;
    vk::raii::PipelineLayout pipelineLayout{nullptr};

    eastl::unique_ptr<ShaderReflection> mergedReflection;
    eastl::vector<LayoutHandle> layoutHandles;
};

} // namespace violet