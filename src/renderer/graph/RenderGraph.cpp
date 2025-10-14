#include "RenderGraph.hpp"
#include "TransientPool.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "core/Log.hpp"

namespace violet {

RenderGraph::PassBuilder::PassBuilder(PassNode& n)
    : node(n) {
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::read(const eastl::string& resourceName, ResourceUsage usage) {
    PassNode::ResourceAccess access;
    access.resourceName = resourceName;
    access.usage = usage;
    access.isWrite = false;
    node.accesses.push_back(access);
    return *this;
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::write(const eastl::string& resourceName, ResourceUsage usage) {
    PassNode::ResourceAccess access;
    access.resourceName = resourceName;
    access.usage = usage;
    access.isWrite = true;
    node.accesses.push_back(access);
    return *this;
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::execute(eastl::function<void(vk::CommandBuffer, uint32_t)> callback) {
    node.executeCallback = callback;
    return *this;
}

RenderGraph::PassBuilder RenderGraph::addPass(const eastl::string& name) {
    passes.emplace_back();  // Create PassNode in passes vector
    passes.back().name = name;
    return PassBuilder(passes.back());  // Return builder that modifies it in-place
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
    res.handle = ResourceHandle::allocate();
    res.name = name;
    res.type = ResourceType::Image;
    res.isExternal = true;
    res.isPersistent = true;
    res.imageResource = imageRes;

    Log::debug("RenderGraph", "Imported external image '{}' (handle={})", name.c_str(), res.handle.id);
    return res.handle;
}

ResourceHandle RenderGraph::importBuffer(const eastl::string& name, const BufferResource* bufferRes) {
    if (!bufferRes) {
        Log::error("RenderGraph", "Cannot import null buffer: {}", name.c_str());
        return InvalidResource;
    }

    auto& res = resources[name];
    res.handle = ResourceHandle::allocate();
    res.name = name;
    res.type = ResourceType::Buffer;
    res.isExternal = true;
    res.isPersistent = true;
    res.bufferResource = bufferRes;

    Log::debug("RenderGraph", "Imported external buffer '{}' (handle={})", name.c_str(), res.handle.id);
    return res.handle;
}

ResourceHandle RenderGraph::createImage(const eastl::string& name, const ImageDesc& desc, bool persistent) {
    auto& res = resources[name];
    res.handle = ResourceHandle::allocate();
    res.name = name;
    res.type = ResourceType::Image;
    res.isExternal = false;
    res.isPersistent = persistent;
    res.imageDesc = desc;

    Log::debug("RenderGraph", "Created {} image '{}' (handle={})", persistent ? "persistent" : "transient", name.c_str(), res.handle.id);
    return res.handle;
}

ResourceHandle RenderGraph::createBuffer(const eastl::string& name, const BufferDesc& desc, bool persistent) {
    auto& res = resources[name];
    res.handle = ResourceHandle::allocate();
    res.name = name;
    res.type = ResourceType::Buffer;
    res.isExternal = false;
    res.isPersistent = persistent;
    res.bufferDesc = desc;

    Log::debug("RenderGraph", "Created {} buffer '{}' (handle={})", persistent ? "persistent" : "transient", name.c_str(), res.handle.id);
    return res.handle;
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

    // Mark passes as reachable if they access valid resources
    for (auto& pass : passes) {
        bool hasValidAccess = false;
        for (const auto& access : pass.accesses) {
            if (resources.find(access.resourceName) != resources.end()) {
                hasValidAccess = true;
                break;
            }
        }
        if (hasValidAccess) {
            pass.reachable = true;
        } else {
            Log::warn("RenderGraph", "Pass '{}' has no valid resource accesses", pass.name.c_str());
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
    buildRenderingInfos();  // Build vk::RenderingAttachmentInfo after resources allocated
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
            auto it = resources.find(access.resourceName);
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
            res.transientView = transientImg.view;  // Save ImageView for buildRenderingInfos
            Log::debug("RenderGraph", "Allocated transient image '{}' with view", name.c_str());
        } else if (res.type == ResourceType::Buffer) {
            auto transientBuf = transientPool->createBuffer(res.bufferDesc, res.firstUse, res.lastUse);
            res.physicalHandle = reinterpret_cast<void*>(static_cast<VkBuffer>(transientBuf.buffer));
            Log::debug("RenderGraph", "Allocated transient buffer '{}'", name.c_str());
        }
    }
}

void RenderGraph::generateBarriers() {
    for (auto& barriers : preBarriers) barriers.clear();
    for (auto& barriers : postBarriers) barriers.clear();

    for (const auto& pass : compiledPasses) {
        for (const auto& access : pass.accesses) {
            auto it = resources.find(access.resourceName);
            if (it == resources.end()) continue;

            auto& res = it->second;
            vk::PipelineStageFlags newStage = getStageForUsage(access.usage);
            vk::AccessFlags newAccess = getAccessForUsage(access.usage);

            if (res.type == ResourceType::Image) {
                vk::ImageLayout newLayout = getLayoutForUsage(access.usage);

                if (res.state.layout != newLayout) {
                    Barrier barrier;
                    barrier.resourceName = access.resourceName;
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
            } else if (res.type == ResourceType::Buffer) {
                // Buffer barriers: check for access pattern changes (e.g., write→read)
                if (res.state.access != newAccess || res.state.stage != newStage) {
                    Barrier barrier;
                    barrier.resourceName = access.resourceName;
                    barrier.isImage = false;
                    barrier.srcStage = res.state.stage;
                    barrier.dstStage = newStage;

                    auto& bufBarrier = barrier.bufferBarrier;
                    bufBarrier.srcAccessMask = res.state.access;
                    bufBarrier.dstAccessMask = newAccess;
                    bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                    if (res.isExternal && res.bufferResource) {
                        bufBarrier.buffer = res.bufferResource->buffer;
                    } else {
                        bufBarrier.buffer = static_cast<vk::Buffer>(reinterpret_cast<VkBuffer>(res.physicalHandle));
                    }
                    bufBarrier.offset = 0;
                    bufBarrier.size = res.bufferDesc.size;

                    preBarriers[pass.passIndex].push_back(barrier);

                    res.state.stage = newStage;
                    res.state.access = newAccess;
                }
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
                      access.isWrite ? "write" : "read", access.resourceName.c_str());
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
        return it->second.handle;
    }

    auto& res = resources[name];
    res.handle = ResourceHandle::allocate();
    res.name = name;
    res.type = ResourceType::Unknown;
    res.isExternal = false;
    res.isPersistent = false;

    Log::warn("RenderGraph", "Auto-created resource '{}' with unknown type (handle={})", name.c_str(), res.handle.id);
    return res.handle;
}

void RenderGraph::buildRenderingInfos() {
    // Build vk::RenderingAttachmentInfo for each pass based on resource accesses
    for (auto& pass : compiledPasses) {
        pass.colorAttachmentInfos.clear();
        pass.hasDepth = false;
        pass.hasStencil = false;
        pass.renderArea = vk::Extent2D{0, 0};

        for (const auto& access : pass.accesses) {
            auto it = resources.find(access.resourceName);
            if (it == resources.end() || it->second.type != ResourceType::Image) {
                continue;
            }

            const auto& res = it->second;

            // Get ImageView from either transientView or external imageResource
            vk::ImageView imageView = VK_NULL_HANDLE;
            if (res.isExternal && res.imageResource) {
                imageView = res.imageResource->view;
            } else if (res.transientView) {
                imageView = res.transientView;
            } else {
                Log::warn("RenderGraph", "Pass '{}': resource '{}' has no valid ImageView",
                         pass.name.c_str(), access.resourceName.c_str());
                continue;
            }

            // Update render area based on resource extent
            if (res.imageDesc.extent.width > pass.renderArea.width) {
                pass.renderArea.width = res.imageDesc.extent.width;
            }
            if (res.imageDesc.extent.height > pass.renderArea.height) {
                pass.renderArea.height = res.imageDesc.extent.height;
            }

            // Build attachment info based on usage
            if (access.usage == ResourceUsage::ColorAttachment && access.isWrite) {
                vk::RenderingAttachmentInfo attachmentInfo;
                attachmentInfo.imageView = imageView;
                attachmentInfo.imageLayout = getLayoutForUsage(access.usage);
                attachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;  // TODO: Allow configuration
                attachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
                attachmentInfo.clearValue = res.imageDesc.clearValue;

                pass.colorAttachmentInfos.push_back(attachmentInfo);

            } else if (access.usage == ResourceUsage::DepthAttachment && access.isWrite) {
                pass.depthAttachmentInfo.imageView = imageView;
                pass.depthAttachmentInfo.imageLayout = getLayoutForUsage(access.usage);
                pass.depthAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
                pass.depthAttachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
                pass.depthAttachmentInfo.clearValue = res.imageDesc.clearValue;
                pass.hasDepth = true;
            }
        }

        Log::debug("RenderGraph", "Pass '{}': {} color attachments, {} depth, render area [{}, {}]",
                  pass.name.c_str(), pass.colorAttachmentInfos.size(),
                  pass.hasDepth ? "has" : "no", pass.renderArea.width, pass.renderArea.height);
    }
}

} // namespace violet