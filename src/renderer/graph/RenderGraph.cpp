#include "renderer/graph/RenderGraph.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "core/Log.hpp"
#include <EASTL/algorithm.h>

namespace violet {

// === Resource Management ===

ResourceHandle RenderGraph::importTexture(const eastl::string& name, const ImageResource* imageRes) {
    if (!imageRes) {
        Log::error("RenderGraph", "Cannot import null texture resource: {}", name.c_str());
        return InvalidResource;
    }

    ResourceHandle handle = nextHandle++;

    LogicalResource& res = resources[handle];
    res.name = name;
    res.type = ResourceType::Texture2D;
    res.imageResource = imageRes;

    // Track name mapping
    resourceNames[name] = handle;

    Log::debug("RenderGraph", "Imported texture '{}' as resource #{}", name.c_str(), handle);
    return handle;
}

ResourceHandle RenderGraph::importBuffer(const eastl::string& name, const BufferResource* bufferRes) {
    if (!bufferRes) {
        Log::error("RenderGraph", "Cannot import null buffer resource: {}", name.c_str());
        return InvalidResource;
    }

    ResourceHandle handle = nextHandle++;

    LogicalResource& res = resources[handle];
    res.name = name;
    res.type = ResourceType::Buffer;
    res.bufferResource = bufferRes;

    // Track name mapping
    resourceNames[name] = handle;

    Log::debug("RenderGraph", "Imported buffer '{}' as resource #{}", name.c_str(), handle);
    return handle;
}

ResourceHandle RenderGraph::createTransient(const eastl::string& name, ResourceType type) {
    // TODO: Implement transient resource allocation in future
    // For now, just create a placeholder
    Log::warn("RenderGraph", "Transient resources not yet implemented: {}", name.c_str());
    return InvalidResource;
}

// === Compilation ===

void RenderGraph::compile() {
    if (passes.empty()) {
        Log::warn("RenderGraph", "No passes to compile");
        return;
    }

    Log::info("RenderGraph", "Compiling graph with {} passes", passes.size());

    // Generate barriers based on resource dependencies
    generateBarriers();

    compiled = true;
    Log::info("RenderGraph", "Graph compiled successfully with {} barriers", barriers.size());
}

void RenderGraph::generateBarriers() {
    barriers.clear();

    // For each pass, check resource transitions
    for (size_t i = 0; i < passes.size(); ++i) {
        const auto& pass = passes[i];

        // For each resource this pass reads
        for (const auto& read : pass.reads) {
            auto resIt = resources.find(read.handle);
            if (resIt == resources.end()) continue;

            auto& resource = resIt->second;

            // Calculate required state for reading
            vk::ImageLayout newLayout = getLayoutForUsage(read.usage);
            vk::PipelineStageFlags newStage = getStageForUsage(read.usage);
            vk::AccessFlags newAccess = getAccessForUsage(read.usage);

            // Check if we need a transition
            if (resource.currentState.layout != newLayout && resource.type == ResourceType::Texture2D) {
                ResourceBarrier barrier;
                barrier.passIndex = i;
                barrier.resource = read.handle;
                barrier.isImage = true;

                // Create image barrier
                auto& imgBarrier = barrier.imageBarrier;
                imgBarrier.oldLayout = resource.currentState.layout;
                imgBarrier.newLayout = newLayout;
                imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imgBarrier.image = getImageHandle(resource);
                imgBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
                imgBarrier.subresourceRange.baseMipLevel = 0;
                imgBarrier.subresourceRange.levelCount = 1;
                imgBarrier.subresourceRange.baseArrayLayer = 0;
                imgBarrier.subresourceRange.layerCount = 1;
                imgBarrier.srcAccessMask = resource.currentState.access;
                imgBarrier.dstAccessMask = newAccess;

                barriers.push_back(barrier);

                // Update tracked state
                resource.currentState.layout = newLayout;
                resource.currentState.stage = newStage;
                resource.currentState.access = newAccess;

                Log::debug("RenderGraph", "Pass '{}': Transition resource '{}' from {:x} to {:x}",
                          pass.name.c_str(), resource.name.c_str(),
                          static_cast<uint32_t>(imgBarrier.oldLayout),
                          static_cast<uint32_t>(imgBarrier.newLayout));
            }
        }

        // For each resource this pass writes
        for (const auto& write : pass.writes) {
            auto resIt = resources.find(write.handle);
            if (resIt == resources.end()) continue;

            auto& resource = resIt->second;

            // Calculate required state for writing
            vk::ImageLayout newLayout = getLayoutForUsage(write.usage);
            vk::PipelineStageFlags newStage = getStageForUsage(write.usage);
            vk::AccessFlags newAccess = getAccessForUsage(write.usage);

            // Check if we need a transition
            if (resource.currentState.layout != newLayout && resource.type == ResourceType::Texture2D) {
                ResourceBarrier barrier;
                barrier.passIndex = i;
                barrier.resource = write.handle;
                barrier.isImage = true;

                // Create image barrier
                auto& imgBarrier = barrier.imageBarrier;
                imgBarrier.oldLayout = resource.currentState.layout;
                imgBarrier.newLayout = newLayout;
                imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imgBarrier.image = getImageHandle(resource);
                imgBarrier.subresourceRange.aspectMask =
                    (write.usage == ResourceUsage::DepthAttachment) ?
                    vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
                imgBarrier.subresourceRange.baseMipLevel = 0;
                imgBarrier.subresourceRange.levelCount = 1;
                imgBarrier.subresourceRange.baseArrayLayer = 0;
                imgBarrier.subresourceRange.layerCount = 1;
                imgBarrier.srcAccessMask = resource.currentState.access;
                imgBarrier.dstAccessMask = newAccess;

                barriers.push_back(barrier);

                // Update tracked state
                resource.currentState.layout = newLayout;
                resource.currentState.stage = newStage;
                resource.currentState.access = newAccess;

                Log::debug("RenderGraph", "Pass '{}': Transition resource '{}' for write from {:x} to {:x}",
                          pass.name.c_str(), resource.name.c_str(),
                          static_cast<uint32_t>(imgBarrier.oldLayout),
                          static_cast<uint32_t>(imgBarrier.newLayout));
            }
        }
    }
}

// === Execution ===

void RenderGraph::execute(vk::CommandBuffer cmd, uint32_t frameIndex) {
    if (!compiled) {
        Log::error("RenderGraph", "Graph must be compiled before execution");
        return;
    }

    for (size_t i = 0; i < passes.size(); ++i) {
        const auto& pass = passes[i];

        Log::debug("RenderGraph", "Executing pass '{}'", pass.name.c_str());

        // Insert barriers before this pass
        insertBarriers(cmd, i);

        // Execute the pass
        if (pass.execute) {
            pass.execute(cmd, frameIndex);
        } else if (pass.wrappedPass) {
            // If there's a wrapped Pass object, use it
            // Note: This would require Pass to have a generic execute method
            Log::warn("RenderGraph", "Wrapped pass execution not yet implemented");
        }
    }
}

void RenderGraph::insertBarriers(vk::CommandBuffer cmd, size_t passIndex) {
    for (const auto& barrier : barriers) {
        if (barrier.passIndex != passIndex) continue;

        if (barrier.isImage) {
            // Determine pipeline stages based on layouts
            vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
            vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eBottomOfPipe;

            // Source stage
            if (barrier.imageBarrier.oldLayout == vk::ImageLayout::eColorAttachmentOptimal) {
                srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            } else if (barrier.imageBarrier.oldLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
                srcStage = vk::PipelineStageFlagBits::eLateFragmentTests;
            } else if (barrier.imageBarrier.oldLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
                srcStage = vk::PipelineStageFlagBits::eFragmentShader;
            } else if (barrier.imageBarrier.oldLayout == vk::ImageLayout::eTransferSrcOptimal) {
                srcStage = vk::PipelineStageFlagBits::eTransfer;
            } else if (barrier.imageBarrier.oldLayout == vk::ImageLayout::eTransferDstOptimal) {
                srcStage = vk::PipelineStageFlagBits::eTransfer;
            }

            // Destination stage
            if (barrier.imageBarrier.newLayout == vk::ImageLayout::eColorAttachmentOptimal) {
                dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            } else if (barrier.imageBarrier.newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
                dstStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
            } else if (barrier.imageBarrier.newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
                dstStage = vk::PipelineStageFlagBits::eFragmentShader;
            } else if (barrier.imageBarrier.newLayout == vk::ImageLayout::eTransferSrcOptimal) {
                dstStage = vk::PipelineStageFlagBits::eTransfer;
            } else if (barrier.imageBarrier.newLayout == vk::ImageLayout::eTransferDstOptimal) {
                dstStage = vk::PipelineStageFlagBits::eTransfer;
            } else if (barrier.imageBarrier.newLayout == vk::ImageLayout::ePresentSrcKHR) {
                dstStage = vk::PipelineStageFlagBits::eBottomOfPipe;
            }

            cmd.pipelineBarrier(
                srcStage, dstStage,
                {}, // No dependency flags
                {}, // No memory barriers
                {}, // No buffer barriers
                barrier.imageBarrier
            );
        } else {
            // Buffer barrier
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eVertexShader,
                vk::PipelineStageFlagBits::eVertexShader,
                {},
                {},
                barrier.bufferBarrier,
                {}
            );
        }
    }
}

// === Utility ===

void RenderGraph::clear() {
    resources.clear();
    resourceNames.clear();
    passes.clear();
    barriers.clear();
    nextHandle = 1;
    compiled = false;
}

const LogicalResource* RenderGraph::getResource(ResourceHandle handle) const {
    auto it = resources.find(handle);
    return (it != resources.end()) ? &it->second : nullptr;
}

void RenderGraph::debugPrint() const {
    Log::info("RenderGraph", "=== Render Graph Debug ===");
    Log::info("RenderGraph", "Resources: {}", resources.size());
    for (const auto& [handle, res] : resources) {
        Log::info("RenderGraph", "  #{}: {} (type={})", handle, res.name.c_str(), static_cast<int>(res.type));
    }

    Log::info("RenderGraph", "Passes: {}", passes.size());
    for (size_t i = 0; i < passes.size(); ++i) {
        const auto& pass = passes[i];
        Log::info("RenderGraph", "  [{}] {}", i, pass.name.c_str());

        if (!pass.reads.empty()) {
            Log::info("RenderGraph", "    Reads:");
            for (const auto& read : pass.reads) {
                if (auto res = getResource(read.handle)) {
                    Log::info("RenderGraph", "      - {} (usage={})", res->name.c_str(), static_cast<int>(read.usage));
                }
            }
        }

        if (!pass.writes.empty()) {
            Log::info("RenderGraph", "    Writes:");
            for (const auto& write : pass.writes) {
                if (auto res = getResource(write.handle)) {
                    Log::info("RenderGraph", "      - {} (usage={})", res->name.c_str(), static_cast<int>(write.usage));
                }
            }
        }
    }

    Log::info("RenderGraph", "Barriers: {}", barriers.size());
    Log::info("RenderGraph", "========================");
}

// === Helper Methods ===

vk::ImageLayout RenderGraph::getLayoutForUsage(ResourceUsage usage) const {
    switch (usage) {
        case ResourceUsage::ColorAttachment:
            return vk::ImageLayout::eColorAttachmentOptimal;
        case ResourceUsage::DepthAttachment:
            return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        case ResourceUsage::ShaderRead:
            return vk::ImageLayout::eShaderReadOnlyOptimal;
        case ResourceUsage::ShaderWrite:
            return vk::ImageLayout::eGeneral;
        case ResourceUsage::TransferSrc:
            return vk::ImageLayout::eTransferSrcOptimal;
        case ResourceUsage::TransferDst:
            return vk::ImageLayout::eTransferDstOptimal;
        case ResourceUsage::Present:
            return vk::ImageLayout::ePresentSrcKHR;
        default:
            return vk::ImageLayout::eUndefined;
    }
}

vk::PipelineStageFlags RenderGraph::getStageForUsage(ResourceUsage usage) const {
    switch (usage) {
        case ResourceUsage::ColorAttachment:
            return vk::PipelineStageFlagBits::eColorAttachmentOutput;
        case ResourceUsage::DepthAttachment:
            return vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
        case ResourceUsage::ShaderRead:
            return vk::PipelineStageFlagBits::eFragmentShader;
        case ResourceUsage::ShaderWrite:
            return vk::PipelineStageFlagBits::eComputeShader;
        case ResourceUsage::TransferSrc:
        case ResourceUsage::TransferDst:
            return vk::PipelineStageFlagBits::eTransfer;
        case ResourceUsage::Present:
            return vk::PipelineStageFlagBits::eBottomOfPipe;
        default:
            return vk::PipelineStageFlagBits::eTopOfPipe;
    }
}

vk::AccessFlags RenderGraph::getAccessForUsage(ResourceUsage usage) const {
    switch (usage) {
        case ResourceUsage::ColorAttachment:
            return vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead;
        case ResourceUsage::DepthAttachment:
            return vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead;
        case ResourceUsage::ShaderRead:
            return vk::AccessFlagBits::eShaderRead;
        case ResourceUsage::ShaderWrite:
            return vk::AccessFlagBits::eShaderWrite;
        case ResourceUsage::TransferSrc:
            return vk::AccessFlagBits::eTransferRead;
        case ResourceUsage::TransferDst:
            return vk::AccessFlagBits::eTransferWrite;
        case ResourceUsage::Present:
            return vk::AccessFlagBits::eMemoryRead;
        default:
            return {};
    }
}

vk::Image RenderGraph::getImageHandle(const LogicalResource& res) const {
    if (res.type == ResourceType::Texture2D && res.imageResource) {
        return res.imageResource->image;
    }
    return VK_NULL_HANDLE;
}

vk::Buffer RenderGraph::getBufferHandle(const LogicalResource& res) const {
    if (res.type == ResourceType::Buffer && res.bufferResource) {
        return res.bufferResource->buffer;
    }
    return VK_NULL_HANDLE;
}

} // namespace violet