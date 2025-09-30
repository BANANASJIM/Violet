#include "PipelineBase.hpp"
#include "VulkanContext.hpp"
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

} // namespace violet