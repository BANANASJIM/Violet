#include "PipelineBase.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "resource/shader/ShaderReflection.hpp"
#include "core/FileSystem.hpp"
#include "core/Exception.hpp"
#include "core/Log.hpp"
#include <cstring>

namespace violet {

void PipelineBase::cleanup() {
    pipelineLayout = nullptr;
}

eastl::vector<char> PipelineBase::readFile(const eastl::string& filename) {
    auto data = FileSystem::readBinary(filename);
    if (data.empty()) {
        violet::Log::error("Renderer", "Failed to open file: {}", filename.c_str());
        throw RuntimeError("Failed to open shader file");
    }

    eastl::vector<char> buffer(data.size());
    memcpy(buffer.data(), data.data(), data.size());

    return buffer;
}

vk::raii::ShaderModule PipelineBase::createShaderModule(const eastl::vector<char>& code) {
    vk::ShaderModuleCreateInfo createInfo;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    return vk::raii::ShaderModule(context->getDeviceRAII(), createInfo);
}

eastl::optional<BindingKey> PipelineBase::getBindingKey(const eastl::string& name) const {
    if (!mergedReflection) {
        return eastl::nullopt;
    }

    const auto* resource = mergedReflection->findResource(name);
    if (!resource) {
        return eastl::nullopt;
    }

    return BindingKey{resource->set, resource->binding};
}

LayoutHandle PipelineBase::getLayoutHandle(uint32_t set) const {
    if (set >= layoutHandles.size()) {
        return 0;
    }
    return layoutHandles[set];
}

} // namespace violet