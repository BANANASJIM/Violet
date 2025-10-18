#include "RenderGraph.hpp"
#include "TransientPool.hpp"
#include "RenderPass.hpp"
#include "ComputePass.hpp"
#include <EASTL/queue.h>
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
    if (auto* renderPass = dynamic_cast<RenderPass*>(node.pass.get())) {
        renderPass->setExecuteCallback(callback);
    } else if (auto* computePass = dynamic_cast<ComputePass*>(node.pass.get())) {
        computePass->setExecuteCallback(callback);
    }
    return *this;
}

void RenderGraph::addPass(const eastl::string& name, eastl::function<void(PassBuilder&, RenderPass&)> setupCallback) {
    auto renderPass = eastl::make_unique<RenderPass>();
    renderPass->init(context, name);

    auto node = eastl::make_unique<PassNode>();
    node->pass = eastl::move(renderPass);

PassBuilder builder(*node);

    setupCallback(builder, *static_cast<RenderPass*>(node->pass.get()));

    passes.push_back(eastl::move(node));

    Log::trace("RenderGraph", "Added graphics pass '{}'", name.c_str());
}

void RenderGraph::addComputePass(const eastl::string& name, eastl::function<void(PassBuilder&, ComputePass&)> setupCallback) {
    // Create ComputePass object
    auto computePass = eastl::make_unique<ComputePass>();
    computePass->init(context, name);

    // Create PassNode and store Pass
    auto node = eastl::make_unique<PassNode>();
    node->pass = eastl::move(computePass);

    // Create PassBuilder for configuration
    PassBuilder builder(*node);

    // Call user setup callback
    setupCallback(builder, *static_cast<ComputePass*>(node->pass.get()));

    // Store PassNode
    passes.push_back(eastl::move(node));

    Log::trace("RenderGraph", "Added compute pass '{}'", name.c_str());
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

ResourceHandle RenderGraph::importImage(
    const eastl::string& name,
    const ImageResource* imageRes,
    vk::ImageLayout initialLayout,
    vk::ImageLayout finalLayout,
    vk::PipelineStageFlags2 initialStage,
    vk::PipelineStageFlags2 finalStage,
    vk::AccessFlags2 initialAccess,
    vk::AccessFlags2 finalAccess
) {
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

    // Populate imageDesc from external image for proper renderArea calculation
    res.imageDesc.format = imageRes->format;
    res.imageDesc.extent = vk::Extent3D{imageRes->width, imageRes->height, 1};
    res.imageDesc.mipLevels = 1;
    res.imageDesc.arrayLayers = 1;

    // Set external constraints
    res.externalConstraints.initialLayout = initialLayout;
    res.externalConstraints.finalLayout = finalLayout;
    res.externalConstraints.initialStage = initialStage;
    res.externalConstraints.finalStage = finalStage;
    res.externalConstraints.initialAccess = initialAccess;
    res.externalConstraints.finalAccess = finalAccess;

    // Initialize state to initial layout
    res.state.layout = initialLayout;
    res.state.stage = initialStage;
    res.state.access = initialAccess;

    Log::trace("RenderGraph", "Imported external image '{}' (handle={}, extent={}x{}, layout: {} → {})",
              name.c_str(), res.handle.id, imageRes->width, imageRes->height,
              vk::to_string(initialLayout).c_str(), vk::to_string(finalLayout).c_str());
    return res.handle;
}

ResourceHandle RenderGraph::importBuffer(
    const eastl::string& name,
    const BufferResource* bufferRes,
    vk::PipelineStageFlags2 initialStage,
    vk::PipelineStageFlags2 finalStage,
    vk::AccessFlags2 initialAccess,
    vk::AccessFlags2 finalAccess
) {
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

    // Populate bufferDesc from external buffer
    res.bufferDesc.size = bufferRes->size;

    // Set external constraints
    res.externalConstraints.initialStage = initialStage;
    res.externalConstraints.finalStage = finalStage;
    res.externalConstraints.initialAccess = initialAccess;
    res.externalConstraints.finalAccess = finalAccess;

    // Initialize state to initial access
    res.state.stage = initialStage;
    res.state.access = initialAccess;

    Log::trace("RenderGraph", "Imported external buffer '{}' (handle={}, size={})",
              name.c_str(), res.handle.id, bufferRes->size);
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

    Log::trace("RenderGraph", "Created {} image '{}' (handle={})", persistent ? "persistent" : "transient", name.c_str(), res.handle.id);
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

    Log::trace("RenderGraph", "Created {} buffer '{}' (handle={})", persistent ? "persistent" : "transient", name.c_str(), res.handle.id);
    return res.handle;
}

void RenderGraph::build() {
    if (passes.empty()) {
        Log::warn("RenderGraph", "No passes to build");
        return;
    }

    Log::trace("RenderGraph", "Building dependency graph with {} passes", passes.size());

    buildDependencyGraph();
    pruneUnreachable();
    topologicalSortWithOptimization();  // Optimize pass execution order
    computeLifetimes();  // Recompute after reordering

    built = true;
    Log::trace("RenderGraph", "Dependency graph built, {} passes reachable", compiledPasses.size());
}

void RenderGraph::buildDependencyGraph() {
    for (uint32_t i = 0; i < passes.size(); ++i) {
        passes[i]->passIndex = i;
        passes[i]->reachable = false;
        passes[i]->dependencies.clear();
    }

    //Build resource writer map (resource name → index of pass that writes it)
    eastl::hash_map<eastl::string, uint32_t> resourceWriters;

    for (const auto& pass : passes) {
        for (const auto& access : pass->accesses) {
            if (access.isWrite) {
                resourceWriters[access.resourceName] = pass->passIndex;
                Log::trace("RenderGraph", "Pass '{}' writes '{}'",
                          pass->pass->getName().c_str(), access.resourceName.c_str());
            }
        }
    }

    // Find Present passes (graph endpoints)
    // Present usage marks passes that write to external resources for presentation
    eastl::vector<uint32_t> presentPasses;

    for (const auto& pass : passes) {
        for (const auto& access : pass->accesses) {
            // Mark passes with Present usage (explicit presentation to swapchain)
            if (access.usage == ResourceUsage::Present && access.isWrite) {
                presentPasses.push_back(pass->passIndex);
                Log::trace("RenderGraph", "Present pass: '{}' (index {}) writes '{}'",
                          pass->pass->getName().c_str(), pass->passIndex, access.resourceName.c_str());
                break;
            }
        }
    }

    if (presentPasses.empty()) {
        Log::warn("RenderGraph", "No Present passes found - all passes will be culled!");
        return;
    }

    // Backward BFS traversal from Present passes
    eastl::queue<uint32_t> queue;

    // Enqueue all Present passes
    for (uint32_t idx : presentPasses) {
        passes[idx]->reachable = true;
        queue.push(idx);
    }

    while (!queue.empty()) {
        uint32_t currentIdx = queue.front();
        queue.pop();

        auto& currentPass = passes[currentIdx];

        // For each resource this pass reads
        for (const auto& access : currentPass->accesses) {
            if (!access.isWrite) {  // Only process reads
                // Find the pass that writes this resource
                auto writerIt = resourceWriters.find(access.resourceName);
                if (writerIt != resourceWriters.end()) {
                    uint32_t producerIdx = writerIt->second;
                    auto& producerPass = passes[producerIdx];

                    // Add dependency edge: currentPass depends on producerPass (avoid duplicates)
                    auto depIt = eastl::find(currentPass->dependencies.begin(), currentPass->dependencies.end(), producerIdx);
                    if (depIt == currentPass->dependencies.end()) {
                        currentPass->dependencies.push_back(producerIdx);
                        Log::trace("RenderGraph", "Pass '{}' depends on '{}' (via resource '{}')",
                                  currentPass->pass->getName().c_str(),
                                  producerPass->pass->getName().c_str(),
                                  access.resourceName.c_str());
                    }

                    // Mark producer as reachable if not already
                    if (!producerPass->reachable) {
                        producerPass->reachable = true;
                        queue.push(producerIdx);
                        Log::trace("RenderGraph", "Marked '{}' as REACHABLE",
                                  producerPass->pass->getName().c_str());
                    }
                } else {
                    Log::warn("RenderGraph", "Pass '{}': No producer found for resource '{}'",
                             currentPass->pass->getName().c_str(),
                             access.resourceName.c_str());
                }
            }
        }
    }

    // Summary
    uint32_t reachableCount = 0;
    for (const auto& pass : passes) {
        if (pass->reachable) reachableCount++;
    }
    Log::trace("RenderGraph", "Result: {} reachable, {} culled",
              reachableCount, passes.size() - reachableCount);
}

void RenderGraph::pruneUnreachable() {
    compiledPasses.clear();

    for (auto& pass : passes) {
        if (pass->reachable) {
            compiledPasses.push_back(pass.get());
        } else {
            const char* passName = pass->pass ? pass->pass->getName().c_str() : "unknown";
            Log::trace("RenderGraph", "Pruned unreachable pass '{}'", passName);
        }
    }

    // DO NOT reassign passIndex here - dependencies reference original indices
    // topologicalSortWithOptimization() will remap using originalToCompiled map
}

void RenderGraph::compile() {
    if (!built) {
        Log::error("RenderGraph", "Must call build() before compile()");
        return;
    }

    Log::trace("RenderGraph", "Compiling render graph");

    preBarriers.resize(compiledPasses.size());
    postBarriers.resize(compiledPasses.size());

    computeLifetimes();
    // Physical resource allocation moved to execute() for per-frame recycling
    generateBarriers();

    uint32_t totalBarriers = 0;
    for (const auto& barriers : preBarriers) totalBarriers += barriers.size();
    for (const auto& barriers : postBarriers) totalBarriers += barriers.size();

    compiled = true;
    Log::trace("RenderGraph", "Compiled: {} passes, {} resources, {} barriers",
              compiledPasses.size(), resources.size(), totalBarriers);
}

void RenderGraph::computeLifetimes() {
    for (auto& [name, res] : resources) {
        res.firstUse = UINT32_MAX;
        res.lastUse = 0;
    }

    for (const auto& pass : compiledPasses) {
        for (const auto& access : pass->accesses) {
            auto it = resources.find(access.resourceName);
            if (it != resources.end()) {
                auto& res = it->second;
                res.firstUse = eastl::min(res.firstUse, pass->passIndex);
                res.lastUse = eastl::max(res.lastUse, pass->passIndex);
            }
        }
    }

    for (const auto& [name, res] : resources) {
        if (!res.isExternal && !res.isPersistent) {
            Log::trace("RenderGraph", "Resource '{}': lifetime [{}, {}]",
                      name.c_str(), res.firstUse, res.lastUse);
        }
    }
}

void RenderGraph::allocatePhysicalResources(uint32_t frameIndex) {
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
            auto transientImg = transientPool->createImage(res.imageDesc, res.firstUse, res.lastUse, frameIndex);
            res.physicalHandle = reinterpret_cast<void*>(static_cast<VkImage>(transientImg.image));
            res.transientView = transientImg.view;  // Save ImageView for buildRenderingInfos
            Log::trace("RenderGraph", "Allocated transient image '{}' with view for frame {}", name.c_str(), frameIndex);
        } else if (res.type == ResourceType::Buffer) {
            auto transientBuf = transientPool->createBuffer(res.bufferDesc, res.firstUse, res.lastUse, frameIndex);
            res.physicalHandle = reinterpret_cast<void*>(static_cast<VkBuffer>(transientBuf.buffer));
            Log::trace("RenderGraph", "Allocated transient buffer '{}' for frame {}", name.c_str(), frameIndex);
        }
    }
}

void RenderGraph::generateBarriers() {
    for (auto& barriers : preBarriers) barriers.clear();
    for (auto& barriers : postBarriers) barriers.clear();

    // Build resource usage table for forward-looking analysis
    buildResourceUsageTable();

    // Initialize resource states
    for (auto& [name, res] : resources) {
        if (res.isExternal) {
            // External resources start with their initial layout/stage from constraints
            res.state.layout = res.externalConstraints.initialLayout;
            res.state.stage = res.externalConstraints.initialStage;
            res.state.access = {};
        } else {
            // Transient resources start undefined
            res.state.layout = vk::ImageLayout::eUndefined;
            res.state.stage = vk::PipelineStageFlagBits2::eTopOfPipe;
            res.state.access = {};
        }
    }

    // Generate barriers using bidirectional merging:
    // PRE-BARRIER: invalidate cache from previous state to current usage
    // POST-BARRIER: flush cache from current usage to next user (forward-looking)
    for (const auto& pass : compiledPasses) {
        for (const auto& access : pass->accesses) {
            auto it = resources.find(access.resourceName);
            if (it == resources.end()) continue;

            auto& res = it->second;
            vk::PipelineStageFlags2 currentStage = getStageForUsage(access.usage);
            vk::AccessFlags2 currentAccess = getAccessForUsage(access.usage);

            if (res.type == ResourceType::Image) {
                vk::ImageLayout currentLayout = getLayoutForUsage(access.usage);

                // PRE-BARRIER: Transition from previous state to current usage (INVALIDATE)
                if (res.state.layout != currentLayout) {
                    Barrier barrier;
                    barrier.resourceName = access.resourceName;
                    barrier.isImage = true;

                    auto& imgBarrier = barrier.imageBarrier;
                    imgBarrier.srcStageMask = res.state.stage;
                    imgBarrier.dstStageMask = currentStage;
                    imgBarrier.oldLayout = res.state.layout;
                    imgBarrier.newLayout = currentLayout;
                    imgBarrier.srcAccessMask = res.state.access;
                    imgBarrier.dstAccessMask = currentAccess;
                    imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                    // Image handle will be filled in insertBarriers() when resources are allocated
                    if (res.isExternal && res.imageResource) {
                        imgBarrier.image = res.imageResource->image;
                    } else {
                        imgBarrier.image = VK_NULL_HANDLE;
                    }

                    // Determine aspect mask based on format
                    bool isDepthFormat = (res.imageDesc.format == vk::Format::eD32Sfloat ||
                                         res.imageDesc.format == vk::Format::eD24UnormS8Uint ||
                                         res.imageDesc.format == vk::Format::eD16Unorm ||
                                         res.imageDesc.format == vk::Format::eD32SfloatS8Uint);
                    imgBarrier.subresourceRange.aspectMask = isDepthFormat ?
                        vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
                    imgBarrier.subresourceRange.baseMipLevel = 0;
                    imgBarrier.subresourceRange.levelCount = res.imageDesc.mipLevels;
                    imgBarrier.subresourceRange.baseArrayLayer = 0;
                    imgBarrier.subresourceRange.layerCount = res.imageDesc.arrayLayers;

                    preBarriers[pass->passIndex].push_back(barrier);

                    Log::trace("RenderGraph", "Pass [{}]: PRE-BARRIER '{}' ({} → {})",
                              pass->passIndex, access.resourceName.c_str(),
                              vk::to_string(res.state.layout).c_str(),
                              vk::to_string(currentLayout).c_str());
                }

                // POST-BARRIER: Flush writes to next user (FLUSH) - only for WRITE accesses
                if (access.isWrite) {
                    const ResourceUsageInfo* nextUser = findNextUser(access.resourceName, pass->passIndex);

                    // Special case: External resources on last use need final layout transition
                    if (nextUser == nullptr && res.isExternal && pass->passIndex == res.lastUse) {
                        // Check if we need to transition to final layout
                        if (currentLayout != res.externalConstraints.finalLayout) {
                            Barrier finalBarrier;
                            finalBarrier.resourceName = access.resourceName;
                            finalBarrier.isImage = true;

                            auto& imgBarrier = finalBarrier.imageBarrier;
                            imgBarrier.srcStageMask = currentStage;
                            imgBarrier.dstStageMask = res.externalConstraints.finalStage;
                            imgBarrier.oldLayout = currentLayout;
                            imgBarrier.newLayout = res.externalConstraints.finalLayout;
                            imgBarrier.srcAccessMask = currentAccess;
                            imgBarrier.dstAccessMask = res.externalConstraints.finalAccess;  // Use externally-specified access for next user
                            imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                            imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                            if (res.imageResource) {
                                imgBarrier.image = res.imageResource->image;
                            } else {
                                imgBarrier.image = VK_NULL_HANDLE;
                            }

                            bool isDepthFormat = (res.imageDesc.format == vk::Format::eD32Sfloat ||
                                                 res.imageDesc.format == vk::Format::eD24UnormS8Uint ||
                                                 res.imageDesc.format == vk::Format::eD16Unorm ||
                                                 res.imageDesc.format == vk::Format::eD32SfloatS8Uint);
                            imgBarrier.subresourceRange.aspectMask = isDepthFormat ?
                                vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
                            imgBarrier.subresourceRange.baseMipLevel = 0;
                            imgBarrier.subresourceRange.levelCount = res.imageDesc.mipLevels;
                            imgBarrier.subresourceRange.baseArrayLayer = 0;
                            imgBarrier.subresourceRange.layerCount = res.imageDesc.arrayLayers;

                            postBarriers[pass->passIndex].push_back(finalBarrier);

                            Log::trace("RenderGraph", "Pass [{}]: FINAL TRANSITION for external '{}' ({} → {})",
                                      pass->passIndex, access.resourceName.c_str(),
                                      vk::to_string(currentLayout).c_str(),
                                      vk::to_string(res.externalConstraints.finalLayout).c_str());

                            // Update resource state to final layout
                            res.state.layout = res.externalConstraints.finalLayout;
                            res.state.stage = res.externalConstraints.finalStage;
                            res.state.access = {};
                        }
                    }
                    // Normal case: Transition to next user
                    else if (nextUser != nullptr) {
                        Barrier barrier;
                        barrier.resourceName = access.resourceName;
                        barrier.isImage = true;

                        auto& imgBarrier = barrier.imageBarrier;
                        imgBarrier.srcStageMask = currentStage;
                        imgBarrier.dstStageMask = nextUser->stage;
                        imgBarrier.oldLayout = currentLayout;
                        imgBarrier.newLayout = nextUser->layout;
                        imgBarrier.srcAccessMask = currentAccess;
                        imgBarrier.dstAccessMask = nextUser->access;
                        imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                        if (res.isExternal && res.imageResource) {
                            imgBarrier.image = res.imageResource->image;
                        } else {
                            imgBarrier.image = VK_NULL_HANDLE;
                        }

                        bool isDepthFormat = (res.imageDesc.format == vk::Format::eD32Sfloat ||
                                             res.imageDesc.format == vk::Format::eD24UnormS8Uint ||
                                             res.imageDesc.format == vk::Format::eD16Unorm ||
                                             res.imageDesc.format == vk::Format::eD32SfloatS8Uint);
                        imgBarrier.subresourceRange.aspectMask = isDepthFormat ?
                            vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
                        imgBarrier.subresourceRange.baseMipLevel = 0;
                        imgBarrier.subresourceRange.levelCount = res.imageDesc.mipLevels;
                        imgBarrier.subresourceRange.baseArrayLayer = 0;
                        imgBarrier.subresourceRange.layerCount = res.imageDesc.arrayLayers;

                        postBarriers[pass->passIndex].push_back(barrier);

                        Log::trace("RenderGraph", "Pass [{}]: POST-BARRIER '{}' → next user at pass {} ({} → {})",
                                  pass->passIndex, access.resourceName.c_str(), nextUser->passIndex,
                                  vk::to_string(currentLayout).c_str(),
                                  vk::to_string(nextUser->layout).c_str());

                        // Update resource state to next user's layout (POST-BARRIER transitions to next layout)
                        res.state.layout = nextUser->layout;
                        res.state.stage = nextUser->stage;
                        res.state.access = nextUser->access;
                        continue;  // Skip the state update below since we already updated
                    }
                }

                // Update resource state to current (only if no POST-BARRIER was generated)
                res.state.layout = currentLayout;
                res.state.stage = currentStage;
                res.state.access = currentAccess;

            } else if (res.type == ResourceType::Buffer) {
                // PRE-BARRIER: Transition from previous state to current usage
                if (res.state.access != currentAccess || res.state.stage != currentStage) {
                    Barrier barrier;
                    barrier.resourceName = access.resourceName;
                    barrier.isImage = false;

                    auto& bufBarrier = barrier.bufferBarrier;
                    bufBarrier.srcStageMask = res.state.stage;
                    bufBarrier.dstStageMask = currentStage;
                    bufBarrier.srcAccessMask = res.state.access;
                    bufBarrier.dstAccessMask = currentAccess;
                    bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                    if (res.isExternal && res.bufferResource) {
                        bufBarrier.buffer = res.bufferResource->buffer;
                    } else {
                        bufBarrier.buffer = VK_NULL_HANDLE;
                    }
                    bufBarrier.offset = 0;
                    bufBarrier.size = res.bufferDesc.size;

                    preBarriers[pass->passIndex].push_back(barrier);
                }

                // POST-BARRIER: Flush writes to next user - only for WRITE accesses
                if (access.isWrite) {
                    const ResourceUsageInfo* nextUser = findNextUser(access.resourceName, pass->passIndex);

                    // Special case: External buffers on last use need final stage/access transition
                    if (nextUser == nullptr && res.isExternal && pass->passIndex == res.lastUse) {
                        // Check if we need to transition to final stage/access
                        if (currentStage != res.externalConstraints.finalStage ||
                            currentAccess != res.externalConstraints.finalAccess) {
                            Barrier finalBarrier;
                            finalBarrier.resourceName = access.resourceName;
                            finalBarrier.isImage = false;

                            auto& bufBarrier = finalBarrier.bufferBarrier;
                            bufBarrier.srcStageMask = currentStage;
                            bufBarrier.dstStageMask = res.externalConstraints.finalStage;
                            bufBarrier.srcAccessMask = currentAccess;
                            bufBarrier.dstAccessMask = res.externalConstraints.finalAccess;
                            bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                            bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                            if (res.bufferResource) {
                                bufBarrier.buffer = res.bufferResource->buffer;
                            } else {
                                bufBarrier.buffer = VK_NULL_HANDLE;
                            }
                            bufBarrier.offset = 0;
                            bufBarrier.size = res.bufferDesc.size;

                            postBarriers[pass->passIndex].push_back(finalBarrier);

                            Log::trace("RenderGraph", "Pass [{}]: FINAL BUFFER TRANSITION for external '{}' (stage: {} → {}, access: {} → {})",
                                      pass->passIndex, access.resourceName.c_str(),
                                      vk::to_string(currentStage).c_str(),
                                      vk::to_string(res.externalConstraints.finalStage).c_str(),
                                      vk::to_string(currentAccess).c_str(),
                                      vk::to_string(res.externalConstraints.finalAccess).c_str());

                            // Update resource state to final stage/access
                            res.state.stage = res.externalConstraints.finalStage;
                            res.state.access = res.externalConstraints.finalAccess;
                        }
                    }
                    // Normal case: Transition to next user
                    else if (nextUser != nullptr) {
                        Barrier barrier;
                        barrier.resourceName = access.resourceName;
                        barrier.isImage = false;

                        auto& bufBarrier = barrier.bufferBarrier;
                        bufBarrier.srcStageMask = currentStage;
                        bufBarrier.dstStageMask = nextUser->stage;
                        bufBarrier.srcAccessMask = currentAccess;
                        bufBarrier.dstAccessMask = nextUser->access;
                        bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                        if (res.isExternal && res.bufferResource) {
                            bufBarrier.buffer = res.bufferResource->buffer;
                        } else {
                            bufBarrier.buffer = VK_NULL_HANDLE;
                        }
                        bufBarrier.offset = 0;
                        bufBarrier.size = res.bufferDesc.size;

                        postBarriers[pass->passIndex].push_back(barrier);

                        Log::trace("RenderGraph", "Pass [{}]: POST-BARRIER buffer '{}' → next user at pass {}",
                                  pass->passIndex, access.resourceName.c_str(), nextUser->passIndex);

                        // Update resource state to next user's stage/access
                        res.state.stage = nextUser->stage;
                        res.state.access = nextUser->access;
                        continue;  // Skip the state update below since we already updated
                    }
                }

                // Update resource state
                res.state.stage = currentStage;
                res.state.access = currentAccess;
            }
        }
    }

    uint32_t totalPreBarriers = 0;
    uint32_t totalPostBarriers = 0;
    for (const auto& barriers : preBarriers) totalPreBarriers += barriers.size();
    for (const auto& barriers : postBarriers) totalPostBarriers += barriers.size();

    Log::trace("RenderGraph", "Generated {} PRE-BARRIERs, {} POST-BARRIERs",
              totalPreBarriers, totalPostBarriers);
}

void RenderGraph::execute(vk::CommandBuffer cmd, uint32_t frameIndex) {
    if (!compiled) {
        Log::error("RenderGraph", "Graph must be compiled before execution");
        return;
    }

    // Recycle transient resources from previous frame with same index
    // Safe: App waits on fence before calling, ensuring GPU finished with old resources
    if (transientPool) {
        transientPool->beginFrame(frameIndex);
    }

    // Allocate physical resources for this frame
    allocatePhysicalResources(frameIndex);
    buildRenderingInfos();  // Build attachment infos after resources allocated

    for (const auto& passNode : compiledPasses) {
        if (!passNode->pass) continue;

        insertPreBarriers(cmd, passNode->passIndex);

        PassType type = passNode->pass->getType();

        // Graphics pass: auto beginRendering/endRendering
        if (type == PassType::Graphics) {
            // Skip beginRendering for passes with no attachments (e.g., Present-only passes)
            bool hasAttachments = !passNode->colorAttachmentInfos.empty() || passNode->hasDepth || passNode->hasStencil;

            if (hasAttachments) {
                vk::RenderingInfo renderingInfo;
                renderingInfo.renderArea = vk::Rect2D{{0, 0}, passNode->renderArea};
                renderingInfo.layerCount = 1;
                renderingInfo.colorAttachmentCount = passNode->colorAttachmentInfos.size();
                renderingInfo.pColorAttachments = passNode->colorAttachmentInfos.data();

                if (passNode->hasDepth) {
                    renderingInfo.pDepthAttachment = &passNode->depthAttachmentInfo;
                }
                if (passNode->hasStencil) {
                    renderingInfo.pStencilAttachment = &passNode->stencilAttachmentInfo;
                }

                cmd.beginRendering(renderingInfo);

                // Set dynamic viewport and scissor
                vk::Viewport viewport{};
                viewport.x = 0.0f;
                viewport.y = 0.0f;
                viewport.width = static_cast<float>(passNode->renderArea.width);
                viewport.height = static_cast<float>(passNode->renderArea.height);
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                cmd.setViewport(0, viewport);

                vk::Rect2D scissor{};
                scissor.offset = vk::Offset2D{0, 0};
                scissor.extent = passNode->renderArea;
                cmd.setScissor(0, scissor);

                passNode->pass->execute(cmd, frameIndex);
                cmd.endRendering();
            } else {
                // No attachments - just execute pass callback (for barrier-only passes)
                passNode->pass->execute(cmd, frameIndex);
            }
        }
        // Compute/Transfer pass: direct execution
        else {
            passNode->pass->execute(cmd, frameIndex);
        }

        insertPostBarriers(cmd, passNode->passIndex);
    }

    // DO NOT reset transientPool here! With triple buffering (3 frames in flight),
    // the GPU might still be using transient resources from previous frames.
    // TransientPool manages memory aliasing based on lifetime analysis,
    // so resources will be reused automatically when safe.
    // Only reset on cleanup() or before recompile().
}

void RenderGraph::insertPreBarriers(vk::CommandBuffer cmd, uint32_t passIndex) {
    if (passIndex >= preBarriers.size()) return;

    const auto& barriers = preBarriers[passIndex];
    if (barriers.empty()) return;

    eastl::vector<vk::ImageMemoryBarrier2> imageBarriers;
    eastl::vector<vk::BufferMemoryBarrier2> bufferBarriers;

    for (auto barrier : barriers) {  // Copy barrier to modify it
        if (barrier.isImage) {
            // Fill in VkImage handle for transient resources (allocated in execute())
            if (barrier.imageBarrier.image == VK_NULL_HANDLE) {
                auto it = resources.find(barrier.resourceName);
                if (it != resources.end() && it->second.physicalHandle) {
                    barrier.imageBarrier.image = static_cast<vk::Image>(reinterpret_cast<VkImage>(it->second.physicalHandle));
                }
            }
            imageBarriers.push_back(barrier.imageBarrier);
        } else {
            // Fill in VkBuffer handle for transient resources
            if (barrier.bufferBarrier.buffer == VK_NULL_HANDLE) {
                auto it = resources.find(barrier.resourceName);
                if (it != resources.end() && it->second.physicalHandle) {
                    barrier.bufferBarrier.buffer = static_cast<vk::Buffer>(reinterpret_cast<VkBuffer>(it->second.physicalHandle));
                }
            }
            bufferBarriers.push_back(barrier.bufferBarrier);
        }
    }

    vk::DependencyInfo dependencyInfo;
    dependencyInfo.imageMemoryBarrierCount = imageBarriers.size();
    dependencyInfo.pImageMemoryBarriers = imageBarriers.data();
    dependencyInfo.bufferMemoryBarrierCount = bufferBarriers.size();
    dependencyInfo.pBufferMemoryBarriers = bufferBarriers.data();

    cmd.pipelineBarrier2(dependencyInfo);
}

void RenderGraph::insertPostBarriers(vk::CommandBuffer cmd, uint32_t passIndex) {
    if (passIndex >= postBarriers.size()) return;

    const auto& barriers = postBarriers[passIndex];
    if (barriers.empty()) return;

    eastl::vector<vk::ImageMemoryBarrier2> imageBarriers;
    eastl::vector<vk::BufferMemoryBarrier2> bufferBarriers;

    for (auto barrier : barriers) {  // Copy barrier to modify it
        if (barrier.isImage) {
            // Fill in VkImage handle for transient resources (allocated in execute())
            if (barrier.imageBarrier.image == VK_NULL_HANDLE) {
                auto it = resources.find(barrier.resourceName);
                if (it != resources.end() && it->second.physicalHandle) {
                    barrier.imageBarrier.image = static_cast<vk::Image>(reinterpret_cast<VkImage>(it->second.physicalHandle));
                }
            }
            imageBarriers.push_back(barrier.imageBarrier);
        } else {
            // Fill in VkBuffer handle for transient resources
            if (barrier.bufferBarrier.buffer == VK_NULL_HANDLE) {
                auto it = resources.find(barrier.resourceName);
                if (it != resources.end() && it->second.physicalHandle) {
                    barrier.bufferBarrier.buffer = static_cast<vk::Buffer>(reinterpret_cast<VkBuffer>(it->second.physicalHandle));
                }
            }
            bufferBarriers.push_back(barrier.bufferBarrier);
        }
    }

    vk::DependencyInfo dependencyInfo;
    dependencyInfo.imageMemoryBarrierCount = imageBarriers.size();
    dependencyInfo.pImageMemoryBarriers = imageBarriers.data();
    dependencyInfo.bufferMemoryBarrierCount = bufferBarriers.size();
    dependencyInfo.pBufferMemoryBarriers = bufferBarriers.data();

    cmd.pipelineBarrier2(dependencyInfo);
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
        const char* passName = pass->pass ? pass->pass->getName().c_str() : "unknown";
        Log::info("RenderGraph", "  [{}] {}", pass->passIndex, passName);
        for (const auto& access : pass->accesses) {
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
        // Present usage renders to ColorAttachment, POST-BARRIER handles transition to PresentSrcKHR
        case ResourceUsage::Present: return vk::ImageLayout::eColorAttachmentOptimal;
        default: return vk::ImageLayout::eUndefined;
    }
}

vk::PipelineStageFlags2 RenderGraph::getStageForUsage(ResourceUsage usage) const {
    switch (usage) {
        case ResourceUsage::ColorAttachment: return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        case ResourceUsage::DepthAttachment: return vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
        case ResourceUsage::ShaderRead: return vk::PipelineStageFlagBits2::eFragmentShader;
        case ResourceUsage::ShaderWrite: return vk::PipelineStageFlagBits2::eComputeShader;
        case ResourceUsage::TransferSrc:
        case ResourceUsage::TransferDst: return vk::PipelineStageFlagBits2::eTransfer;
        // Present usage renders at ColorAttachmentOutput stage (writes color attachment, final layout is PresentSrcKHR)
        case ResourceUsage::Present: return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        default: return vk::PipelineStageFlagBits2::eTopOfPipe;
    }
}

vk::AccessFlags2 RenderGraph::getAccessForUsage(ResourceUsage usage) const {
    switch (usage) {
        case ResourceUsage::ColorAttachment: return vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead;
        case ResourceUsage::DepthAttachment: return vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentRead;
        case ResourceUsage::ShaderRead: return vk::AccessFlagBits2::eShaderRead;
        case ResourceUsage::ShaderWrite: return vk::AccessFlagBits2::eShaderWrite;
        case ResourceUsage::TransferSrc: return vk::AccessFlagBits2::eTransferRead;
        case ResourceUsage::TransferDst: return vk::AccessFlagBits2::eTransferWrite;
        // Present usage writes to color attachment (fragment shader output)
        case ResourceUsage::Present: return vk::AccessFlagBits2::eColorAttachmentWrite;
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
        pass->colorAttachmentInfos.clear();
        pass->hasDepth = false;
        pass->hasStencil = false;
        pass->renderArea = vk::Extent2D{0, 0};

        for (const auto& access : pass->accesses) {
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
                const char* passName = pass->pass ? pass->pass->getName().c_str() : "unknown";
                Log::warn("RenderGraph", "Pass '{}': resource '{}' has no valid ImageView",
                         passName, access.resourceName.c_str());
                continue;
            }

            // Update render area based on resource extent
            if (res.imageDesc.extent.width > pass->renderArea.width) {
                pass->renderArea.width = res.imageDesc.extent.width;
            }
            if (res.imageDesc.extent.height > pass->renderArea.height) {
                pass->renderArea.height = res.imageDesc.extent.height;
            }

            // Build attachment info based on usage
            if ((access.usage == ResourceUsage::ColorAttachment || access.usage == ResourceUsage::Present) && access.isWrite) {
                vk::RenderingAttachmentInfo attachmentInfo;
                attachmentInfo.imageView = imageView;
                // Use ColorAttachmentOptimal even for Present usage during rendering
                // POST-BARRIER will handle transition to PresentSrcKHR after rendering
                attachmentInfo.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
                attachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;  // TODO: Allow configuration
                attachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
                attachmentInfo.clearValue = res.imageDesc.clearValue;

                pass->colorAttachmentInfos.push_back(attachmentInfo);

            } else if (access.usage == ResourceUsage::DepthAttachment) {
                // Add depth attachment for both read and write
                pass->depthAttachmentInfo.imageView = imageView;
                pass->depthAttachmentInfo.imageLayout = getLayoutForUsage(access.usage);

                if (access.isWrite) {
                    // Write access: clear and store
                    pass->depthAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
                    pass->depthAttachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
                    pass->depthAttachmentInfo.clearValue = res.imageDesc.clearValue;
                } else {
                    // Read-only access: load existing content, don't store
                    pass->depthAttachmentInfo.loadOp = vk::AttachmentLoadOp::eLoad;
                    pass->depthAttachmentInfo.storeOp = vk::AttachmentStoreOp::eDontCare;
                }

                pass->hasDepth = true;
            }
        }

        const char* passName = pass->pass ? pass->pass->getName().c_str() : "unknown";
        Log::trace("RenderGraph", "Pass '{}': {} color attachments, {} depth, render area [{}, {}]",
                  passName, pass->colorAttachmentInfos.size(),
                  pass->hasDepth ? "has" : "no", pass->renderArea.width, pass->renderArea.height);
    }
}

// ============================================================================
// Topological Sort with Optimization
// ============================================================================

int RenderGraph::countSharedResources(PassNode* a, PassNode* b) {
    if (!a || !b) return 0;

    int count = 0;
    for (const auto& accessA : a->accesses) {
        for (const auto& accessB : b->accesses) {
            if (accessA.resourceName == accessB.resourceName) {
                count++;
                break;
            }
        }
    }
    return count;
}

int RenderGraph::calculateLayoutTransitions(PassNode* next, PassNode* prev) {
    if (!next || !prev) return 0;

    int transitions = 0;

    // Check each resource in next pass
    for (const auto& accessNext : next->accesses) {
        // Find if prev pass accessed the same resource
        for (const auto& accessPrev : prev->accesses) {
            if (accessNext.resourceName == accessPrev.resourceName) {
                vk::ImageLayout prevLayout = getLayoutForUsage(accessPrev.usage);
                vk::ImageLayout nextLayout = getLayoutForUsage(accessNext.usage);

                if (prevLayout != nextLayout) {
                    transitions++;
                }
                break;
            }
        }
    }

    return transitions;
}

PassNode* RenderGraph::selectOptimalPass(
    const eastl::vector<PassNode*>& readyQueue,
    PassNode* lastExecuted,
    const eastl::hash_map<eastl::string, uint32_t>& externalFirstUse,
    const eastl::hash_map<eastl::string, uint32_t>& externalLastUse
) {
    if (readyQueue.empty()) return nullptr;
    if (readyQueue.size() == 1) return readyQueue[0];

    int bestScore = -1;
    PassNode* bestPass = nullptr;

    for (auto* pass : readyQueue) {
        int score = 0;

        // Hard constraint: External resource firstUse (highest priority)
        for (const auto& access : pass->accesses) {
            auto firstIt = externalFirstUse.find(access.resourceName);
            if (firstIt != externalFirstUse.end() && firstIt->second == pass->passIndex) {
                score += 1000;  // Very high priority
            }

            auto lastIt = externalLastUse.find(access.resourceName);
            if (lastIt != externalLastUse.end() && lastIt->second == pass->passIndex) {
                score += 500;  // High priority
            }
        }

        // If no previous pass, just use constraints
        if (!lastExecuted) {
            if (score > bestScore) {
                bestScore = score;
                bestPass = pass;
            }
            continue;
        }

        // Soft heuristics
        // 1. Resource locality: shared resources with previous pass
        score += countSharedResources(pass, lastExecuted) * 100;

        // 2. Layout transition cost: penalize transitions
        score -= calculateLayoutTransitions(pass, lastExecuted) * 10;

        if (score > bestScore) {
            bestScore = score;
            bestPass = pass;
        }
    }

    return bestPass ? bestPass : readyQueue[0];
}

void RenderGraph::topologicalSortWithOptimization() {
    if (compiledPasses.empty()) {
        Log::warn("RenderGraph", "No passes to sort");
        return;
    }

    Log::trace("RenderGraph", "Topological sort with optimization");

    // Build index map for compiled passes (original index → compiled index)
    eastl::hash_map<uint32_t, uint32_t> originalToCompiled;
    for (size_t i = 0; i < compiledPasses.size(); ++i) {
        originalToCompiled[compiledPasses[i]->passIndex] = static_cast<uint32_t>(i);
    }

    // Calculate in-degree for each compiled pass
    eastl::vector<uint32_t> inDegree(compiledPasses.size(), 0);

    for (auto* pass : compiledPasses) {
        for (uint32_t depIdx : pass->dependencies) {
            // Find the compiled index of the dependency
            auto it = originalToCompiled.find(depIdx);
            if (it != originalToCompiled.end()) {
                uint32_t compiledDepIdx = it->second;
                // Increment in-degree for current pass
                uint32_t currentCompiled = originalToCompiled[pass->passIndex];
                inDegree[currentCompiled]++;
            }
        }
    }

    // Identify external resource constraints (currently none, but keep for future)
    eastl::hash_map<eastl::string, uint32_t> externalFirstUse;
    eastl::hash_map<eastl::string, uint32_t> externalLastUse;

    for (const auto& [name, res] : resources) {
        if (res.isExternal && res.firstUse != UINT32_MAX) {
            externalFirstUse[name] = res.firstUse;
            externalLastUse[name] = res.lastUse;
        }
    }

    // Find initial ready passes (in-degree = 0)
    eastl::vector<PassNode*> readyQueue;
    for (size_t i = 0; i < compiledPasses.size(); ++i) {
        if (inDegree[i] == 0) {
            readyQueue.push_back(compiledPasses[i]);
        }
    }

    // Topological sort with optimization
    eastl::vector<PassNode*> sorted;
    PassNode* lastExecuted = nullptr;

    while (!readyQueue.empty()) {
        // Select optimal pass from ready queue
        PassNode* bestPass = selectOptimalPass(readyQueue, lastExecuted,
                                               externalFirstUse, externalLastUse);

        sorted.push_back(bestPass);
        lastExecuted = bestPass;

        // Remove from ready queue
        readyQueue.erase(eastl::find(readyQueue.begin(), readyQueue.end(), bestPass));

        // Update in-degrees and add newly ready passes
        uint32_t selectedOrigIdx = bestPass->passIndex;
        for (size_t i = 0; i < compiledPasses.size(); ++i) {
            auto* pass = compiledPasses[i];

            // Check if this pass depends on the selected pass
            if (eastl::find(pass->dependencies.begin(), pass->dependencies.end(), selectedOrigIdx) != pass->dependencies.end()) {
                inDegree[i]--;
                if (inDegree[i] == 0) {
                    readyQueue.push_back(pass);
                }
            }
        }
    }

    // Check if all passes were sorted (cycle detection)
    if (sorted.size() != compiledPasses.size()) {
        Log::error("RenderGraph", "Cyclic dependency detected! Only {} of {} passes sorted",
                  sorted.size(), compiledPasses.size());

        // Debug: Print all passes and their dependencies
        Log::error("RenderGraph", "=== Dependency Graph Debug ===");
        for (const auto& pass : compiledPasses) {
            const char* passName = pass->pass ? pass->pass->getName().c_str() : "unknown";
            Log::error("RenderGraph", "Pass '{}' (index {}):", passName, pass->passIndex);
            for (const auto& access : pass->accesses) {
                Log::error("RenderGraph", "  {} '{}'", access.isWrite ? "writes" : "reads", access.resourceName.c_str());
            }
            Log::error("RenderGraph", "  Dependencies:");
            for (uint32_t depIdx : pass->dependencies) {
                const char* depName = "unknown";
                for (const auto& p : compiledPasses) {
                    if (p->passIndex == depIdx && p->pass) {
                        depName = p->pass->getName().c_str();
                        break;
                    }
                }
                Log::error("RenderGraph", "    -> Pass {} ('{}')", depIdx, depName);
            }
        }

        return;
    }

    // Update compiledPasses with sorted order
    compiledPasses = sorted;

    // Reassign pass indices to match new order
    for (uint32_t i = 0; i < compiledPasses.size(); ++i) {
        compiledPasses[i]->passIndex = i;
    }

    Log::trace("RenderGraph", "Topological sort complete: {} passes", compiledPasses.size());
}

// ============================================================================
// Resource Usage Table & Forward-Looking Barrier Generation
// ============================================================================

void RenderGraph::buildResourceUsageTable() {
    resourceUsageTable.clear();

    for (auto* pass : compiledPasses) {
        for (const auto& access : pass->accesses) {
            ResourceUsageInfo info;
            info.passIndex = pass->passIndex;
            info.usage = access.usage;
            info.isWrite = access.isWrite;
            info.stage = getStageForUsage(access.usage);
            info.access = getAccessForUsage(access.usage);
            info.layout = getLayoutForUsage(access.usage);

            resourceUsageTable[access.resourceName].push_back(info);
        }
    }

    Log::trace("RenderGraph", "Built resource usage table for {} resources", resourceUsageTable.size());
}

const RenderGraph::ResourceUsageInfo* RenderGraph::findNextUser(
    const eastl::string& resourceName,
    uint32_t currentPassIndex
) const {
    auto it = resourceUsageTable.find(resourceName);
    if (it == resourceUsageTable.end()) {
        return nullptr;
    }

    const auto& usages = it->second;

    // Find the first usage after currentPassIndex
    for (const auto& usage : usages) {
        if (usage.passIndex > currentPassIndex) {
            return &usage;
        }
    }

    return nullptr;
}

} // namespace violet