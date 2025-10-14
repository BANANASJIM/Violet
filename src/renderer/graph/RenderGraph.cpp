#include "RenderGraph.hpp"
#include "TransientPool.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "core/Log.hpp"

namespace violet {

RenderGraph::PassBuilder::PassBuilder(RenderGraph* g, const eastl::string& n)
    : graph(g) {
    node.name = n;
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::read(const eastl::string& name, ResourceUsage usage) {
    PassNode::ResourceAccess access;
    access.name = name;
    access.usage = usage;
    access.isWrite = false;
    node.accesses.push_back(access);
    return *this;
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::write(const eastl::string& name, const ImageDesc& desc, ResourceUsage usage) {
    PassNode::ResourceAccess access;
    access.name = name;
    access.usage = usage;
    access.imageDesc = desc;
    access.isWrite = true;
    node.accesses.push_back(access);
    return *this;
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::execute(eastl::function<void(vk::CommandBuffer, uint32_t)> callback) {
    node.executeCallback = callback;
    return *this;
}

void RenderGraph::PassBuilder::build() {
    graph->passes.push_back(node);
}

RenderGraph::PassBuilder RenderGraph::addPass(const eastl::string& name) {
    return PassBuilder(this, name);
}

void RenderGraph::init(VulkanContext* ctx) {
    context = ctx;
    transientPool = new TransientPool();
    transientPool->init(ctx);
    Log::info("RenderGraph", "Initialized");
}

void RenderGraph::cleanup() {
    if (transientPool) {
        transientPool->cleanup();
        delete transientPool;
        transientPool = nullptr;
    }
    clear();
    context = nullptr;
}

ResourceHandle RenderGraph::importImage(const eastl::string& name, const ImageResource* imageRes) {
    if (!imageRes) {
        Log::error("RenderGraph", "Cannot import null image: {}", name.c_str());
        return InvalidResource;
    }

    auto& res = resources[name];
    res.name = name;
    res.type = ResourceType::Image;
    res.isExternal = true;
    res.isPersistent = true;
    res.imageResource = imageRes;

    Log::debug("RenderGraph", "Imported external image '{}'", name.c_str());
    return 1;
}

ResourceHandle RenderGraph::importBuffer(const eastl::string& name, const BufferResource* bufferRes) {
    if (!bufferRes) {
        Log::error("RenderGraph", "Cannot import null buffer: {}", name.c_str());
        return InvalidResource;
    }

    auto& res = resources[name];
    res.name = name;
    res.type = ResourceType::Buffer;
    res.isExternal = true;
    res.isPersistent = true;
    res.bufferResource = bufferRes;

    Log::debug("RenderGraph", "Imported external buffer '{}'", name.c_str());
    return 1;
}

ResourceHandle RenderGraph::declareImage(const eastl::string& name, const ImageDesc& desc, bool persistent) {
    auto& res = resources[name];
    res.name = name;
    res.type = ResourceType::Image;
    res.isExternal = false;
    res.isPersistent = persistent;
    res.imageDesc = desc;

    Log::debug("RenderGraph", "Declared {} image '{}'", persistent ? "persistent" : "transient", name.c_str());
    return 1;
}

void RenderGraph::build() {
    if (passes.empty()) {
        Log::warn("RenderGraph", "No passes to build");
        return;
    }

    Log::info("RenderGraph", "Building dependency graph with {} passes", passes.size());

    buildDependencyGraph();
    pruneUnreachable();

    built = true;
    Log::info("RenderGraph", "Dependency graph built, {} passes reachable", compiledPasses.size());
}

void RenderGraph::buildDependencyGraph() {
    for (uint32_t i = 0; i < passes.size(); ++i) {
        passes[i].passIndex = i;
    }

    for (auto& pass : passes) {
        for (const auto& access : pass.accesses) {
            if (access.isWrite) {
                auto it = resources.find(access.name);
                if (it == resources.end()) {
                    auto& res = resources[access.name];
                    res.name = access.name;
                    res.type = ResourceType::Image;
                    res.isExternal = false;
                    res.isPersistent = false;
                    res.imageDesc = access.imageDesc;
                    Log::debug("RenderGraph", "Auto-created transient resource '{}'", access.name.c_str());
                }
            }
        }
    }

    for (auto& pass : passes) {
        bool hasValidAccess = false;
        for (const auto& access : pass.accesses) {
            if (resources.find(access.name) != resources.end()) {
                hasValidAccess = true;
                break;
            }
        }
        if (hasValidAccess) {
            pass.reachable = true;
        }
    }
}

void RenderGraph::pruneUnreachable() {
    compiledPasses.clear();

    for (auto& pass : passes) {
        if (pass.reachable) {
            compiledPasses.push_back(pass);
        } else {
            Log::debug("RenderGraph", "Pruned unreachable pass '{}'", pass.name.c_str());
        }
    }

    for (uint32_t i = 0; i < compiledPasses.size(); ++i) {
        compiledPasses[i].passIndex = i;
    }
}

void RenderGraph::compile() {
    if (!built) {
        Log::error("RenderGraph", "Must call build() before compile()");
        return;
    }

    Log::info("RenderGraph", "Compiling render graph");

    preBarriers.resize(compiledPasses.size());
    postBarriers.resize(compiledPasses.size());

    computeLifetimes();
    allocatePhysicalResources();
    generateBarriers();

    uint32_t totalBarriers = 0;
    for (const auto& barriers : preBarriers) totalBarriers += barriers.size();
    for (const auto& barriers : postBarriers) totalBarriers += barriers.size();

    compiled = true;
    Log::info("RenderGraph", "Compiled: {} passes, {} resources, {} barriers",
              compiledPasses.size(), resources.size(), totalBarriers);
}

void RenderGraph::computeLifetimes() {
    for (auto& [name, res] : resources) {
        res.firstUse = UINT32_MAX;
        res.lastUse = 0;
    }

    for (const auto& pass : compiledPasses) {
        for (const auto& access : pass.accesses) {
            auto it = resources.find(access.name);
            if (it != resources.end()) {
                auto& res = it->second;
                res.firstUse = eastl::min(res.firstUse, pass.passIndex);
                res.lastUse = eastl::max(res.lastUse, pass.passIndex);
            }
        }
    }

    for (const auto& [name, res] : resources) {
        if (!res.isExternal && !res.isPersistent) {
            Log::debug("RenderGraph", "Resource '{}': lifetime [{}, {}]",
                      name.c_str(), res.firstUse, res.lastUse);
        }
    }
}

void RenderGraph::allocatePhysicalResources() {
    if (!transientPool) {
        Log::error("RenderGraph", "TransientPool not initialized");
        return;
    }

    for (auto& [name, res] : resources) {
        if (res.isExternal) {
            continue;
        }

        if (res.isPersistent) {
            Log::warn("RenderGraph", "Persistent resource '{}' allocation not yet implemented", name.c_str());
            continue;
        }

        if (res.type == ResourceType::Image) {
            auto transientImg = transientPool->createImage(res.imageDesc, res.firstUse, res.lastUse);
            res.physicalHandle = reinterpret_cast<void*>(static_cast<VkImage>(transientImg.image));
            Log::debug("RenderGraph", "Allocated transient image '{}'", name.c_str());
        }
    }
}

void RenderGraph::generateBarriers() {
    for (auto& barriers : preBarriers) barriers.clear();
    for (auto& barriers : postBarriers) barriers.clear();

    for (const auto& pass : compiledPasses) {
        for (const auto& access : pass.accesses) {
            auto it = resources.find(access.name);
            if (it == resources.end() || it->second.type != ResourceType::Image) continue;

            auto& res = it->second;
            vk::ImageLayout newLayout = getLayoutForUsage(access.usage);
            vk::PipelineStageFlags newStage = getStageForUsage(access.usage);
            vk::AccessFlags newAccess = getAccessForUsage(access.usage);

            if (res.state.layout != newLayout) {
                Barrier barrier;
                barrier.resourceName = access.name;
                barrier.isImage = true;
                barrier.srcStage = res.state.stage;
                barrier.dstStage = newStage;

                auto& imgBarrier = barrier.imageBarrier;
                imgBarrier.oldLayout = res.state.layout;
                imgBarrier.newLayout = newLayout;
                imgBarrier.srcAccessMask = res.state.access;
                imgBarrier.dstAccessMask = newAccess;
                imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                if (res.isExternal && res.imageResource) {
                    imgBarrier.image = res.imageResource->image;
                } else {
                    imgBarrier.image = static_cast<vk::Image>(reinterpret_cast<VkImage>(res.physicalHandle));
                }

                imgBarrier.subresourceRange.aspectMask =
                    (access.usage == ResourceUsage::DepthAttachment) ?
                    vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
                imgBarrier.subresourceRange.baseMipLevel = 0;
                imgBarrier.subresourceRange.levelCount = res.imageDesc.mipLevels;
                imgBarrier.subresourceRange.baseArrayLayer = 0;
                imgBarrier.subresourceRange.layerCount = res.imageDesc.arrayLayers;

                preBarriers[pass.passIndex].push_back(barrier);

                res.state.layout = newLayout;
                res.state.stage = newStage;
                res.state.access = newAccess;
            }
        }
    }
}

void RenderGraph::execute(vk::CommandBuffer cmd, uint32_t frameIndex) {
    if (!compiled) {
        Log::error("RenderGraph", "Graph must be compiled before execution");
        return;
    }

    for (const auto& pass : compiledPasses) {
        insertPreBarriers(cmd, pass.passIndex);

        if (pass.executeCallback) {
            pass.executeCallback(cmd, frameIndex);
        }

        insertPostBarriers(cmd, pass.passIndex);
    }

    if (transientPool) {
        transientPool->reset();
    }
}

void RenderGraph::insertPreBarriers(vk::CommandBuffer cmd, uint32_t passIndex) {
    if (passIndex >= preBarriers.size()) return;

    const auto& barriers = preBarriers[passIndex];
    if (barriers.empty()) return;

    for (const auto& barrier : barriers) {
        if (barrier.isImage) {
            cmd.pipelineBarrier(
                barrier.srcStage, barrier.dstStage,
                {},
                {},
                {},
                barrier.imageBarrier
            );
        } else {
            cmd.pipelineBarrier(
                barrier.srcStage, barrier.dstStage,
                {},
                {},
                barrier.bufferBarrier,
                {}
            );
        }
    }
}

void RenderGraph::insertPostBarriers(vk::CommandBuffer cmd, uint32_t passIndex) {
    if (passIndex >= postBarriers.size()) return;

    const auto& barriers = postBarriers[passIndex];
    if (barriers.empty()) return;

    for (const auto& barrier : barriers) {
        if (barrier.isImage) {
            cmd.pipelineBarrier(
                barrier.srcStage, barrier.dstStage,
                {},
                {},
                {},
                barrier.imageBarrier
            );
        } else {
            cmd.pipelineBarrier(
                barrier.srcStage, barrier.dstStage,
                {},
                {},
                barrier.bufferBarrier,
                {}
            );
        }
    }
}

void RenderGraph::clear() {
    resources.clear();
    passes.clear();
    compiledPasses.clear();
    preBarriers.clear();
    postBarriers.clear();
    built = false;
    compiled = false;
}

const LogicalResource* RenderGraph::getResource(const eastl::string& name) const {
    auto it = resources.find(name);
    return (it != resources.end()) ? &it->second : nullptr;
}

void RenderGraph::debugPrint() const {
    Log::info("RenderGraph", "=== Render Graph Debug ===");
    Log::info("RenderGraph", "Resources: {}", resources.size());
    for (const auto& [name, res] : resources) {
        eastl::string typeStr = res.isExternal ? "external" : (res.isPersistent ? "persistent" : "transient");
        Log::info("RenderGraph", "  '{}': {} (lifetime [{}, {}])",
                  name.c_str(), typeStr.c_str(), res.firstUse, res.lastUse);
    }

    Log::info("RenderGraph", "Compiled Passes: {}", compiledPasses.size());
    for (const auto& pass : compiledPasses) {
        Log::info("RenderGraph", "  [{}] {}", pass.passIndex, pass.name.c_str());
        for (const auto& access : pass.accesses) {
            Log::info("RenderGraph", "    {} '{}'",
                      access.isWrite ? "write" : "read", access.name.c_str());
        }
    }

    uint32_t totalBarriers = 0;
    for (const auto& barriers : preBarriers) totalBarriers += barriers.size();
    for (const auto& barriers : postBarriers) totalBarriers += barriers.size();
    Log::info("RenderGraph", "Barriers: {} total", totalBarriers);
    Log::info("RenderGraph", "========================");
}

vk::ImageLayout RenderGraph::getLayoutForUsage(ResourceUsage usage) const {
    switch (usage) {
        case ResourceUsage::ColorAttachment: return vk::ImageLayout::eColorAttachmentOptimal;
        case ResourceUsage::DepthAttachment: return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        case ResourceUsage::ShaderRead: return vk::ImageLayout::eShaderReadOnlyOptimal;
        case ResourceUsage::ShaderWrite: return vk::ImageLayout::eGeneral;
        case ResourceUsage::TransferSrc: return vk::ImageLayout::eTransferSrcOptimal;
        case ResourceUsage::TransferDst: return vk::ImageLayout::eTransferDstOptimal;
        case ResourceUsage::Present: return vk::ImageLayout::ePresentSrcKHR;
        default: return vk::ImageLayout::eUndefined;
    }
}

vk::PipelineStageFlags RenderGraph::getStageForUsage(ResourceUsage usage) const {
    switch (usage) {
        case ResourceUsage::ColorAttachment: return vk::PipelineStageFlagBits::eColorAttachmentOutput;
        case ResourceUsage::DepthAttachment: return vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
        case ResourceUsage::ShaderRead: return vk::PipelineStageFlagBits::eFragmentShader;
        case ResourceUsage::ShaderWrite: return vk::PipelineStageFlagBits::eComputeShader;
        case ResourceUsage::TransferSrc:
        case ResourceUsage::TransferDst: return vk::PipelineStageFlagBits::eTransfer;
        case ResourceUsage::Present: return vk::PipelineStageFlagBits::eBottomOfPipe;
        default: return vk::PipelineStageFlagBits::eTopOfPipe;
    }
}

vk::AccessFlags RenderGraph::getAccessForUsage(ResourceUsage usage) const {
    switch (usage) {
        case ResourceUsage::ColorAttachment: return vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead;
        case ResourceUsage::DepthAttachment: return vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead;
        case ResourceUsage::ShaderRead: return vk::AccessFlagBits::eShaderRead;
        case ResourceUsage::ShaderWrite: return vk::AccessFlagBits::eShaderWrite;
        case ResourceUsage::TransferSrc: return vk::AccessFlagBits::eTransferRead;
        case ResourceUsage::TransferDst: return vk::AccessFlagBits::eTransferWrite;
        case ResourceUsage::Present: return vk::AccessFlagBits::eMemoryRead;
        default: return {};
    }
}

ResourceHandle RenderGraph::getOrCreateResource(const eastl::string& name) {
    auto it = resources.find(name);
    if (it != resources.end()) {
        return 1;
    }

    auto& res = resources[name];
    res.name = name;
    res.type = ResourceType::Unknown;
    res.isExternal = false;
    res.isPersistent = false;
    return 1;
}

} // namespace violet