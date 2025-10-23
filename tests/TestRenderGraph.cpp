// TestRenderGraph Implementation
// Based on: https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive/

#include "TestRenderGraph.hpp"
#include <EASTL/queue.h>
#include <fmt/core.h>

namespace violet {

// ============================================================================
// PassBuilder Implementation
// ============================================================================

TestRenderGraph::PassBuilder::PassBuilder(PassNode& n)
    : node(n) {
}

TestRenderGraph::PassBuilder& TestRenderGraph::PassBuilder::read(
    const eastl::string& resourceName, ResourceUsage usage) {
    PassNode::ResourceAccess access;
    access.resourceName = resourceName;
    access.usage = usage;
    access.isWrite = false;
    node.accesses.push_back(access);
    return *this;
}

TestRenderGraph::PassBuilder& TestRenderGraph::PassBuilder::write(
    const eastl::string& resourceName, ResourceUsage usage) {
    PassNode::ResourceAccess access;
    access.resourceName = resourceName;
    access.usage = usage;
    access.isWrite = true;
    node.accesses.push_back(access);
    return *this;
}

TestRenderGraph::PassBuilder& TestRenderGraph::PassBuilder::execute(
    eastl::function<void()> callback) {
    // Callback stored but not used in test
    return *this;
}

// ============================================================================
// TestRenderGraph Lifecycle
// ============================================================================

void TestRenderGraph::init(MockVulkanContext* ctx) {
    context = ctx;
    fmt::print("TestRenderGraph initialized\n");
}

void TestRenderGraph::cleanup() {
    clear();
    context = nullptr;
}

// ============================================================================
// Resource Creation
// ============================================================================

ResourceHandle TestRenderGraph::importImage(
    const eastl::string& name,
    void* externalImage,
    ImageLayout initialLayout,
    ImageLayout finalLayout,
    PipelineStage initialStage,
    PipelineStage finalStage
) {
    auto& res = resources[name];
    res.handle = ResourceHandle::allocate();
    res.name = name;
    res.type = ResourceType::Image;
    res.isExternal = true;
    res.isPersistent = true;

    // Set external constraints
    res.externalConstraints.initialLayout = initialLayout;
    res.externalConstraints.finalLayout = finalLayout;
    res.externalConstraints.initialStage = initialStage;
    res.externalConstraints.finalStage = finalStage;

    // Initialize state to initial layout
    res.state.layout = initialLayout;
    res.state.stage = initialStage;

    return res.handle;
}

ResourceHandle TestRenderGraph::importBuffer(const eastl::string& name, void* externalBuffer) {
    auto& res = resources[name];
    res.handle = ResourceHandle::allocate();
    res.name = name;
    res.type = ResourceType::Buffer;
    res.isExternal = true;
    res.isPersistent = true;
    return res.handle;
}

ResourceHandle TestRenderGraph::createImage(const eastl::string& name, const ImageDesc& desc, bool persistent) {
    auto& res = resources[name];
    res.handle = ResourceHandle::allocate();
    res.name = name;
    res.type = ResourceType::Image;
    res.isExternal = false;
    res.isPersistent = persistent;
    res.imageDesc = desc;
    return res.handle;
}

ResourceHandle TestRenderGraph::createBuffer(const eastl::string& name, const BufferDesc& desc, bool persistent) {
    auto& res = resources[name];
    res.handle = ResourceHandle::allocate();
    res.name = name;
    res.type = ResourceType::Buffer;
    res.isExternal = false;
    res.isPersistent = persistent;
    res.bufferDesc = desc;
    return res.handle;
}

// ============================================================================
// Pass Creation
// ============================================================================

void TestRenderGraph::addPass(const eastl::string& name,
                               eastl::function<void(PassBuilder&, MockRenderPass&)> setupCallback) {
    auto node = eastl::make_unique<PassNode>();
    node->name = name;

    PassBuilder builder(*node);
    MockRenderPass mockPass;
    mockPass.init(context, name);

    setupCallback(builder, mockPass);

    passes.push_back(eastl::move(node));
}

void TestRenderGraph::addComputePass(const eastl::string& name,
                                      eastl::function<void(PassBuilder&, MockComputePass&)> setupCallback) {
    auto node = eastl::make_unique<PassNode>();
    node->name = name;

    PassBuilder builder(*node);
    MockComputePass mockPass;
    mockPass.init(context, name);

    setupCallback(builder, mockPass);

    passes.push_back(eastl::move(node));
}

// ============================================================================
// Graph Building - Core Algorithm
// ============================================================================

void TestRenderGraph::build() {
    if (passes.empty()) {
        fmt::print("WARNING: No passes to build\n");
        return;
    }

    fmt::print("\n=== Building Dependency Graph ===\n");
    fmt::print("Total passes: {}\n", passes.size());

    buildDependencyGraph();
    pruneUnreachable();
    topologicalSortWithOptimization();  // NEW: Optimize pass execution order
    computeLifetimes();

    built = true;
    fmt::print("Build complete: {} reachable passes\n", compiledPasses.size());
}

/**
 * Build dependency graph using backward traversal from Present passes
 * Algorithm from Themaister's blog:
 * 1. Build resource writer map (resource → pass index)
 * 2. Find all Present passes (graph endpoints)
 * 3. BFS backward traversal:
 *    - For each reachable pass, examine its reads
 *    - Find producer passes that write those resources
 *    - Mark producers as reachable, add dependency edges
 */
void TestRenderGraph::buildDependencyGraph() {
    // Step 0: Initialize pass indices and reset state
    for (uint32_t i = 0; i < passes.size(); ++i) {
        passes[i]->passIndex = i;
        passes[i]->reachable = false;
        passes[i]->dependencies.clear();
    }

    // Step 1: Build resource writer map
    // Maps: resource name → index of pass that writes it
    eastl::hash_map<eastl::string, uint32_t> resourceWriters;

    fmt::print("\nResource writers:\n");
    for (const auto& pass : passes) {
        for (const auto& access : pass->accesses) {
            if (access.isWrite) {
                resourceWriters[access.resourceName] = pass->passIndex;
                fmt::print("  Pass '{}' writes '{}'\n",
                          pass->name.c_str(), access.resourceName.c_str());
            }
        }
    }

    // Step 2: Find Present passes (graph endpoints)
    eastl::vector<uint32_t> presentPasses;

    fmt::print("\nPresent passes:\n");
    for (const auto& pass : passes) {
        for (const auto& access : pass->accesses) {
            if (access.usage == ResourceUsage::Present && access.isWrite) {
                presentPasses.push_back(pass->passIndex);
                fmt::print("  '{}' (index {})\n", pass->name.c_str(), pass->passIndex);
                break;
            }
        }
    }

    if (presentPasses.empty()) {
        fmt::print("  WARNING: No Present passes found - all passes will be culled!\n");
        return;
    }

    // Step 3: Backward BFS traversal from Present passes
    fmt::print("\nBackward traversal:\n");
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
        fmt::print("  Processing '{}' (index {})\n", currentPass->name.c_str(), currentIdx);

        // For each resource this pass reads
        for (const auto& access : currentPass->accesses) {
            if (!access.isWrite) {  // Only process reads
                fmt::print("    Read '{}'\n", access.resourceName.c_str());

                // Find the pass that writes this resource
                auto writerIt = resourceWriters.find(access.resourceName);
                if (writerIt != resourceWriters.end()) {
                    uint32_t producerIdx = writerIt->second;
                    auto& producerPass = passes[producerIdx];

                    // Add dependency edge: currentPass depends on producerPass
                    currentPass->dependencies.push_back(producerIdx);
                    fmt::print("      → Depends on '{}' (index {})\n",
                              producerPass->name.c_str(), producerIdx);

                    // Mark producer as reachable if not already
                    if (!producerPass->reachable) {
                        producerPass->reachable = true;
                        queue.push(producerIdx);
                        fmt::print("      → Marked '{}' as REACHABLE\n",
                                  producerPass->name.c_str());
                    }
                } else {
                    fmt::print("      WARNING: No producer for '{}'\n",
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
    fmt::print("\nResult: {} reachable, {} culled\n",
              reachableCount, passes.size() - reachableCount);
}

/**
 * Prune unreachable passes
 * Fills compiledPasses with only reachable passes
 */
void TestRenderGraph::pruneUnreachable() {
    compiledPasses.clear();

    for (auto& pass : passes) {
        if (pass->reachable) {
            compiledPasses.push_back(pass.get());
        } else {
            fmt::print("  CULLED: '{}'\n", pass->name.c_str());
        }
    }

    // Reassign pass indices for compiled passes
    for (uint32_t i = 0; i < compiledPasses.size(); ++i) {
        compiledPasses[i]->passIndex = i;
    }
}

/**
 * Compute resource lifetimes
 * Determines firstUse and lastUse for each resource based on reachable passes
 */
void TestRenderGraph::computeLifetimes() {
    // Reset all lifetimes
    for (auto& [name, res] : resources) {
        res.firstUse = UINT32_MAX;
        res.lastUse = 0;
    }

    // Compute from compiled passes only
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

    fmt::print("\nResource lifetimes:\n");
    for (const auto& [name, res] : resources) {
        if (res.firstUse != UINT32_MAX) {
            fmt::print("  '{}': [{}, {}]\n", name.c_str(), res.firstUse, res.lastUse);
        } else {
            fmt::print("  '{}': UNUSED\n", name.c_str());
        }
    }
}

// ============================================================================
// Barrier Generation Helpers
// ============================================================================

/**
 * Map ResourceUsage to appropriate PipelineStageFlags2 (synchronization2)
 */
PipelineStageFlags2 TestRenderGraph::getStageForUsage(ResourceUsage usage) const {
    switch (usage) {
        case ResourceUsage::ColorAttachment:
            return PipelineStageFlags2::ColorAttachmentOutput;
        case ResourceUsage::DepthAttachment:
            return PipelineStageFlags2::EarlyFragmentTests | PipelineStageFlags2::LateFragmentTests;
        case ResourceUsage::ShaderRead:
            return PipelineStageFlags2::FragmentShader | PipelineStageFlags2::ComputeShader;
        case ResourceUsage::ShaderWrite:
            return PipelineStageFlags2::ComputeShader;
        case ResourceUsage::TransferSrc:
        case ResourceUsage::TransferDst:
            return PipelineStageFlags2::AllTransfer;  // synchronization2: more explicit
        case ResourceUsage::Present:
            return PipelineStageFlags2::BottomOfPipe;
        default:
            return PipelineStageFlags2::TopOfPipe;
    }
}

/**
 * Map ResourceUsage to appropriate AccessFlags2 (synchronization2)
 */
AccessFlags2 TestRenderGraph::getAccessForUsage(ResourceUsage usage, bool isWrite) const {
    switch (usage) {
        case ResourceUsage::ColorAttachment:
            return isWrite ? AccessFlags2::ColorAttachmentReadWrite  // synchronization2: combined flags
                           : AccessFlags2::ColorAttachmentRead;
        case ResourceUsage::DepthAttachment:
            return isWrite ? AccessFlags2::DepthStencilAttachmentReadWrite
                           : AccessFlags2::DepthStencilAttachmentRead;
        case ResourceUsage::ShaderRead:
            return AccessFlags2::ShaderSampledRead;  // synchronization2: more specific
        case ResourceUsage::ShaderWrite:
            return AccessFlags2::ShaderStorageWrite;  // synchronization2: explicit storage write
        case ResourceUsage::TransferSrc:
            return AccessFlags2::TransferRead;
        case ResourceUsage::TransferDst:
            return AccessFlags2::TransferWrite;
        case ResourceUsage::Present:
            return AccessFlags2::None;
        default:
            return AccessFlags2::None;
    }
}

/**
 * Convert PipelineStageFlags2 to string (synchronization2)
 */
eastl::string TestRenderGraph::toString(PipelineStageFlags2 flags) const {
    if (flags == PipelineStageFlags2::None) return "NONE";

    eastl::string result;
    uint64_t value = static_cast<uint64_t>(flags);

    // Standard stages
    if (value & static_cast<uint64_t>(PipelineStageFlags2::TopOfPipe)) result += "TOP_OF_PIPE|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::DrawIndirect)) result += "DRAW_INDIRECT|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::VertexInput)) result += "VERTEX_INPUT|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::VertexShader)) result += "VERTEX_SHADER|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::FragmentShader)) result += "FRAGMENT_SHADER|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::EarlyFragmentTests)) result += "EARLY_FRAGMENT_TESTS|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::LateFragmentTests)) result += "LATE_FRAGMENT_TESTS|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::ColorAttachmentOutput)) result += "COLOR_ATTACHMENT_OUTPUT|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::ComputeShader)) result += "COMPUTE_SHADER|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::AllTransfer)) result += "ALL_TRANSFER|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::BottomOfPipe)) result += "BOTTOM_OF_PIPE|";

    // synchronization2 extended stages
    if (value & static_cast<uint64_t>(PipelineStageFlags2::Copy)) result += "COPY|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::Blit)) result += "BLIT|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::Resolve)) result += "RESOLVE|";
    if (value & static_cast<uint64_t>(PipelineStageFlags2::Clear)) result += "CLEAR|";

    // Remove trailing '|'
    if (!result.empty() && result.back() == '|') {
        result.pop_back();
    }

    return result.empty() ? "UNKNOWN" : result;
}

/**
 * Convert AccessFlags2 to string (synchronization2)
 */
eastl::string TestRenderGraph::toString(AccessFlags2 flags) const {
    if (flags == AccessFlags2::None) return "NONE";

    eastl::string result;
    uint64_t value = static_cast<uint64_t>(flags);

    // Standard access types
    if (value & static_cast<uint64_t>(AccessFlags2::IndirectCommandRead)) result += "INDIRECT_COMMAND_READ|";
    if (value & static_cast<uint64_t>(AccessFlags2::IndexRead)) result += "INDEX_READ|";
    if (value & static_cast<uint64_t>(AccessFlags2::VertexAttributeRead)) result += "VERTEX_ATTRIBUTE_READ|";
    if (value & static_cast<uint64_t>(AccessFlags2::UniformRead)) result += "UNIFORM_READ|";
    if (value & static_cast<uint64_t>(AccessFlags2::ShaderRead)) result += "SHADER_READ|";
    if (value & static_cast<uint64_t>(AccessFlags2::ShaderWrite)) result += "SHADER_WRITE|";
    if (value & static_cast<uint64_t>(AccessFlags2::ColorAttachmentRead)) result += "COLOR_ATTACHMENT_READ|";
    if (value & static_cast<uint64_t>(AccessFlags2::ColorAttachmentWrite)) result += "COLOR_ATTACHMENT_WRITE|";
    if (value & static_cast<uint64_t>(AccessFlags2::DepthStencilAttachmentRead)) result += "DEPTH_STENCIL_ATTACHMENT_READ|";
    if (value & static_cast<uint64_t>(AccessFlags2::DepthStencilAttachmentWrite)) result += "DEPTH_STENCIL_ATTACHMENT_WRITE|";
    if (value & static_cast<uint64_t>(AccessFlags2::TransferRead)) result += "TRANSFER_READ|";
    if (value & static_cast<uint64_t>(AccessFlags2::TransferWrite)) result += "TRANSFER_WRITE|";

    // synchronization2 extended access types
    if (value & static_cast<uint64_t>(AccessFlags2::ShaderSampledRead)) result += "SHADER_SAMPLED_READ|";
    if (value & static_cast<uint64_t>(AccessFlags2::ShaderStorageRead)) result += "SHADER_STORAGE_READ|";
    if (value & static_cast<uint64_t>(AccessFlags2::ShaderStorageWrite)) result += "SHADER_STORAGE_WRITE|";

    // Remove trailing '|'
    if (!result.empty() && result.back() == '|') {
        result.pop_back();
    }

    return result.empty() ? "UNKNOWN" : result;
}

/**
 * Convert ImageLayout to string
 */
eastl::string TestRenderGraph::toString(ImageLayout layout) const {
    switch (layout) {
        case ImageLayout::Undefined: return "UNDEFINED";
        case ImageLayout::ColorAttachment: return "COLOR_ATTACHMENT";
        case ImageLayout::DepthStencilAttachment: return "DEPTH_STENCIL_ATTACHMENT";
        case ImageLayout::ShaderReadOnly: return "SHADER_READ_ONLY";
        case ImageLayout::General: return "GENERAL";
        case ImageLayout::TransferSrc: return "TRANSFER_SRC";
        case ImageLayout::TransferDst: return "TRANSFER_DST";
        case ImageLayout::PresentSrc: return "PRESENT_SRC";
        default: return "UNKNOWN";
    }
}

/**
 * Print barrier details (synchronization2)
 */
void TestRenderGraph::printBarrier(const ImageMemoryBarrier2& b) const {
    fmt::print("    Resource: '{}'\n", b.resourceName.c_str());
    fmt::print("      srcStage: {} → dstStage: {}\n",
               toString(b.srcStageMask).c_str(), toString(b.dstStageMask).c_str());
    fmt::print("      srcAccess: {} → dstAccess: {}\n",
               toString(b.srcAccessMask).c_str(), toString(b.dstAccessMask).c_str());
    fmt::print("      oldLayout: {} → newLayout: {}\n",
               toString(b.oldLayout).c_str(), toString(b.newLayout).c_str());

    if (b.isEmpty()) {
        fmt::print("      [EMPTY BARRIER - placeholder]\n");
    } else {
        fmt::print("      [TRANSITION REQUIRED]\n");
    }
}

/**
 * Build resource usage table for all compiled passes
 * Maps: resource name → list of all pass accesses in execution order
 * Used for forward-looking barrier generation
 */
void TestRenderGraph::buildResourceUsageTable() {
    resourceUsageTable.clear();

    for (auto* pass : compiledPasses) {
        for (const auto& access : pass->accesses) {
            ResourceUsageInfo info;
            info.passIndex = pass->passIndex;
            info.usage = access.usage;
            info.isWrite = access.isWrite;
            info.stage = getStageForUsage(access.usage);
            info.access = getAccessForUsage(access.usage, access.isWrite);
            info.layout = getLayoutForUsage(access.usage);

            resourceUsageTable[access.resourceName].push_back(info);
        }
    }

    fmt::print("Built resource usage table for {} resources\n", resourceUsageTable.size());
}

/**
 * Find the next user of a resource after currentPassIndex
 * Returns nullptr if no next user found
 */
const TestRenderGraph::ResourceUsageInfo* TestRenderGraph::findNextUser(
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

/**
 * Core barrier generation algorithm (Dual Bucket System)
 * Based on: https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive/
 *
 * For each pass in topological order:
 *   PRE-BARRIERS (Invalidate Bucket): Ensure resources are in correct state for reading
 *   POST-BARRIERS (Flush Bucket): Signal that resources have been written
 *
 * OPTIMIZATION: Bidirectional merging
 *   - PRE-BARRIER looks backward to previous POST-BARRIER state
 *   - POST-BARRIER looks forward to find actual next user (not just adjacent pass)
 */
void TestRenderGraph::generateBarriers() {
    fmt::print("\nGenerating barriers for {} passes...\n", compiledPasses.size());

    // Step 0: Build resource usage table for forward-looking analysis
    buildResourceUsageTable();

    // Step 1: Initialize resource states (synchronization2)
    // External resources start with their initial layout/stage/access
    // Transient resources start as Undefined
    for (auto& [name, res] : resources) {
        if (res.isExternal) {
            // External resources (swapchain, history buffers) have defined initial state
            res.state.layout = res.externalConstraints.initialLayout;

            // Convert PipelineStage to PipelineStageFlags2 for state tracking
            switch (res.externalConstraints.initialStage) {
                case PipelineStage::TopOfPipe:
                    res.state.validStages = PipelineStageFlags2::TopOfPipe;
                    break;
                case PipelineStage::ColorAttachmentOutput:
                    res.state.validStages = PipelineStageFlags2::ColorAttachmentOutput;
                    break;
                case PipelineStage::FragmentShader:
                    res.state.validStages = PipelineStageFlags2::FragmentShader | PipelineStageFlags2::ComputeShader;
                    break;
                case PipelineStage::ComputeShader:
                    res.state.validStages = PipelineStageFlags2::ComputeShader;
                    break;
                case PipelineStage::Transfer:
                    res.state.validStages = PipelineStageFlags2::AllTransfer;
                    break;
                case PipelineStage::BottomOfPipe:
                    res.state.validStages = PipelineStageFlags2::BottomOfPipe;
                    break;
                default:
                    res.state.validStages = PipelineStageFlags2::TopOfPipe;
                    break;
            }

            // Infer initial access from layout (synchronization2)
            // IMPORTANT: External resources should have their access flags set based on their initial layout
            switch (res.externalConstraints.initialLayout) {
                case ImageLayout::ShaderReadOnly:
                    res.state.validAccess = AccessFlags2::ShaderSampledRead;
                    break;
                case ImageLayout::ColorAttachment:
                    res.state.validAccess = AccessFlags2::ColorAttachmentReadWrite;
                    break;
                case ImageLayout::DepthStencilAttachment:
                    res.state.validAccess = AccessFlags2::DepthStencilAttachmentReadWrite;
                    break;
                case ImageLayout::PresentSrc:
                    res.state.validAccess = AccessFlags2::None;  // Present doesn't need access
                    break;
                case ImageLayout::General:
                    res.state.validAccess = AccessFlags2::ShaderReadWrite;
                    break;
                default:
                    res.state.validAccess = AccessFlags2::None;
                    break;
            }
        } else {
            // Transient resources start undefined
            res.state.layout = ImageLayout::Undefined;
            res.state.validStages = PipelineStageFlags2::TopOfPipe;
            res.state.validAccess = AccessFlags2::None;
        }
    }

    // Step 2: Generate barriers for each pass in execution order
    for (auto* pass : compiledPasses) {
        fmt::print("\nPass '{}' (index {}):\n", pass->name.c_str(), pass->passIndex);

        // Clear previous barriers
        pass->preBarriers.clear();
        pass->postBarriers.clear();

        // ====================================================================
        // PRE-BARRIERS (Invalidate Bucket)
        // ====================================================================
        // For each READ access, ensure resource is in correct state
        fmt::print("  PRE-BARRIERS (Invalidate):\n");

        for (const auto& access : pass->accesses) {
            if (access.isWrite) continue;  // Only process reads for pre-barriers

            auto it = resources.find(access.resourceName);
            if (it == resources.end()) continue;

            auto& res = it->second;

            // Determine required state for this read access
            ImageLayout requiredLayout = getLayoutForUsage(access.usage);
            PipelineStageFlags2 requiredStage = getStageForUsage(access.usage);
            AccessFlags2 requiredAccess = getAccessForUsage(access.usage, false);

            // Check if transition is needed
            bool needsTransition = (res.state.layout != requiredLayout) ||
                                   (res.state.validStages != requiredStage) ||
                                   (res.state.validAccess != requiredAccess);

            if (needsTransition) {
                ImageMemoryBarrier2 barrier;
                barrier.resourceName = access.resourceName;
                barrier.srcStageMask = res.state.validStages;
                barrier.dstStageMask = requiredStage;
                barrier.srcAccessMask = res.state.validAccess;
                barrier.dstAccessMask = requiredAccess;
                barrier.oldLayout = res.state.layout;
                barrier.newLayout = requiredLayout;

                pass->preBarriers.push_back(barrier);
                fmt::print("    '{}': {} → {}\n",
                          access.resourceName.c_str(),
                          toString(res.state.layout).c_str(),
                          toString(requiredLayout).c_str());

                // Update resource state to reflect the transition
                res.state.layout = requiredLayout;
                res.state.validStages = requiredStage;
                res.state.validAccess = requiredAccess;
            } else {
                fmt::print("    '{}': [no transition needed]\n", access.resourceName.c_str());
            }
        }

        // ====================================================================
        // POST-BARRIERS (Flush Bucket)
        // ====================================================================
        // For each WRITE access, emit flush barrier and update state
        fmt::print("  POST-BARRIERS (Flush):\n");

        for (const auto& access : pass->accesses) {
            if (!access.isWrite) continue;  // Only process writes for post-barriers

            auto it = resources.find(access.resourceName);
            if (it == resources.end()) continue;

            auto& res = it->second;

            // Determine write state
            ImageLayout writeLayout = getLayoutForUsage(access.usage);
            PipelineStageFlags2 writeStage = getStageForUsage(access.usage);
            AccessFlags2 writeAccess = getAccessForUsage(access.usage, true);

            // Check if the resource needs a pre-write transition
            // This handles WRITE-ONLY resources (not read by this pass)
            // For transient resources, this creates the initial UNDEFINED → proper layout transition
            bool needsPreWriteTransition = (res.state.layout != writeLayout);

            if (needsPreWriteTransition) {
                ImageMemoryBarrier2 preWriteBarrier;
                preWriteBarrier.resourceName = access.resourceName;
                preWriteBarrier.srcStageMask = res.state.validStages;
                preWriteBarrier.dstStageMask = writeStage;
                preWriteBarrier.srcAccessMask = res.state.validAccess;
                preWriteBarrier.dstAccessMask = writeAccess;
                preWriteBarrier.oldLayout = res.state.layout;
                preWriteBarrier.newLayout = writeLayout;

                pass->preBarriers.push_back(preWriteBarrier);
                fmt::print("    [PRE-WRITE] '{}': {} → {}\n",
                          access.resourceName.c_str(),
                          toString(res.state.layout).c_str(),
                          toString(writeLayout).c_str());

                // Update state immediately for this pre-write barrier
                res.state.layout = writeLayout;
                res.state.validStages = writeStage;
                res.state.validAccess = writeAccess;
            }

            // Emit flush barrier with forward-looking analysis (signals write completion)
            ImageMemoryBarrier2 barrier;
            barrier.resourceName = access.resourceName;
            barrier.srcStageMask = writeStage;
            barrier.srcAccessMask = writeAccess;
            barrier.oldLayout = writeLayout;

            // Forward-looking: Find actual next user instead of conservative BOTTOM_OF_PIPE
            const ResourceUsageInfo* nextUser = findNextUser(access.resourceName, pass->passIndex);
            if (nextUser != nullptr) {
                // Found next user - use its stage/access/layout directly
                barrier.dstStageMask = nextUser->stage;
                barrier.dstAccessMask = nextUser->access;
                barrier.newLayout = nextUser->layout;

                fmt::print("    '{}': FLUSH → next user at pass {} ({} → {})\n",
                          access.resourceName.c_str(),
                          nextUser->passIndex,
                          toString(writeLayout).c_str(),
                          toString(nextUser->layout).c_str());
            } else {
                // No next user - use conservative values (last write)
                barrier.dstStageMask = PipelineStageFlags2::BottomOfPipe;
                barrier.dstAccessMask = AccessFlags2::None;
                barrier.newLayout = writeLayout;

                fmt::print("    '{}': FLUSH (no next user, conservative)\n",
                          access.resourceName.c_str());
            }

            pass->postBarriers.push_back(barrier);

            // Update resource state to reflect the POST-BARRIER's destination
            // This is crucial for forward-looking barriers: state becomes what the NEXT user expects
            res.state.layout = barrier.newLayout;
            res.state.validStages = barrier.dstStageMask;
            res.state.validAccess = barrier.dstAccessMask;
        }

        // ====================================================================
        // EXTERNAL RESOURCE FINAL STATE
        // ====================================================================
        // If this is the last use of an external resource, transition to final layout
        for (const auto& access : pass->accesses) {
            auto it = resources.find(access.resourceName);
            if (it == resources.end()) continue;

            auto& res = it->second;

            if (res.isExternal && res.lastUse == pass->passIndex) {
                // Check if we need to transition to final layout
                if (res.state.layout != res.externalConstraints.finalLayout) {
                    ImageMemoryBarrier2 finalBarrier;
                    finalBarrier.resourceName = access.resourceName;
                    finalBarrier.srcStageMask = res.state.validStages;

                    // Convert finalStage to PipelineStageFlags2
                    switch (res.externalConstraints.finalStage) {
                        case PipelineStage::BottomOfPipe:
                            finalBarrier.dstStageMask = PipelineStageFlags2::BottomOfPipe;
                            break;
                        case PipelineStage::ColorAttachmentOutput:
                            finalBarrier.dstStageMask = PipelineStageFlags2::ColorAttachmentOutput;
                            break;
                        default:
                            finalBarrier.dstStageMask = PipelineStageFlags2::BottomOfPipe;
                            break;
                    }

                    finalBarrier.srcAccessMask = res.state.validAccess;
                    finalBarrier.dstAccessMask = AccessFlags2::None;
                    finalBarrier.oldLayout = res.state.layout;
                    finalBarrier.newLayout = res.externalConstraints.finalLayout;

                    pass->postBarriers.push_back(finalBarrier);
                    fmt::print("  FINAL TRANSITION for '{}': {} → {}\n",
                              access.resourceName.c_str(),
                              toString(res.state.layout).c_str(),
                              toString(res.externalConstraints.finalLayout).c_str());

                    // Update state
                    res.state.layout = res.externalConstraints.finalLayout;
                }
            }
        }
    }

    fmt::print("\nBarrier generation complete\n");
}

/**
 * Compare two barriers for equivalence
 * Used to identify mergeable barriers with identical stage/access/layout transitions
 */
bool TestRenderGraph::barriersAreEquivalent(
    const ImageMemoryBarrier2& a,
    const ImageMemoryBarrier2& b,
    bool compareResourceName
) const {
    // Compare resource names if requested
    if (compareResourceName && a.resourceName != b.resourceName) {
        return false;
    }

    // Compare all barrier properties
    return a.srcStageMask == b.srcStageMask &&
           a.dstStageMask == b.dstStageMask &&
           a.srcAccessMask == b.srcAccessMask &&
           a.dstAccessMask == b.dstAccessMask &&
           a.oldLayout == b.oldLayout &&
           a.newLayout == b.newLayout;
}

/**
 * Merge redundant barriers
 *
 * Optimization rules:
 * 1. Remove duplicate POST-BARRIERS (flush) for same resource with identical properties
 * 2. Remove redundant PRE-BARRIERS when resource already in correct state from previous pass
 */
void TestRenderGraph::mergeBarriers() {
    fmt::print("\n=== Barrier Merging ===\n");

    // Count barriers before optimization
    barrierStats.totalGenerated = 0;
    for (auto* pass : compiledPasses) {
        barrierStats.totalGenerated += pass->preBarriers.size() + pass->postBarriers.size();
    }

    fmt::print("Generated barriers: {}\n", barrierStats.totalGenerated);

    // Rule 1: Merge duplicate POST-BARRIERS (flush barriers)
    // Consecutive passes that write to the same resource with same layout can share flush barrier
    for (size_t i = 0; i < compiledPasses.size(); ++i) {
        auto* pass = compiledPasses[i];
        eastl::vector<ImageMemoryBarrier2> mergedPostBarriers;

        for (const auto& barrier : pass->postBarriers) {
            // Check if this barrier is identical to a previous pass's POST-BARRIER
            bool foundDuplicate = false;

            if (i > 0) {
                auto* prevPass = compiledPasses[i - 1];
                for (const auto& prevBarrier : prevPass->postBarriers) {
                    if (barriersAreEquivalent(barrier, prevBarrier, true)) {
                        // Found duplicate flush barrier - skip it
                        foundDuplicate = true;
                        barrierStats.mergedFlushBarriers++;
                        fmt::print("  Merged flush barrier for '{}' in pass '{}' (duplicate of pass '{}')\n",
                                  barrier.resourceName.c_str(), pass->name.c_str(), prevPass->name.c_str());
                        break;
                    }
                }
            }

            if (!foundDuplicate) {
                mergedPostBarriers.push_back(barrier);
            }
        }

        pass->postBarriers = eastl::move(mergedPostBarriers);
    }

    // Rule 2: Remove redundant PRE-BARRIERS
    // If resource is already in the required state from a previous pass, skip the barrier
    eastl::hash_map<eastl::string, ImageMemoryBarrier2> lastKnownState;

    for (auto* pass : compiledPasses) {
        eastl::vector<ImageMemoryBarrier2> mergedPreBarriers;

        for (const auto& barrier : pass->preBarriers) {
            bool isRedundant = false;

            // Check if we already have this resource in the correct state
            auto it = lastKnownState.find(barrier.resourceName);
            if (it != lastKnownState.end()) {
                const auto& knownState = it->second;

                // If the previous barrier already transitioned to the required state, skip
                if (knownState.newLayout == barrier.newLayout &&
                    knownState.dstStageMask == barrier.dstStageMask &&
                    knownState.dstAccessMask == barrier.dstAccessMask) {
                    isRedundant = true;
                    barrierStats.removedRedundantPreBarriers++;
                    fmt::print("  Removed redundant PRE-BARRIER for '{}' in pass '{}' (already in correct state)\n",
                              barrier.resourceName.c_str(), pass->name.c_str());
                }
            }

            if (!isRedundant) {
                mergedPreBarriers.push_back(barrier);
                // Update known state
                lastKnownState[barrier.resourceName] = barrier;
            }
        }

        pass->preBarriers = eastl::move(mergedPreBarriers);

        // Update known states from POST-BARRIERS (flush barriers update resource state)
        for (const auto& barrier : pass->postBarriers) {
            lastKnownState[barrier.resourceName] = barrier;
        }
    }

    // Rule 3: Merge POST-BARRIER from pass[i] with PRE-BARRIER from pass[i+1]
    // Physical optimization: POST+PRE pairs describe the SAME synchronization relationship
    fmt::print("\nRule 3: Merging POST+PRE barrier pairs...\n");
    for (size_t i = 0; i + 1 < compiledPasses.size(); ++i) {
        auto* currentPass = compiledPasses[i];
        auto* nextPass = compiledPasses[i + 1];

        // For each POST-BARRIER in current pass
        for (auto& postBarrier : currentPass->postBarriers) {
            // Look for matching PRE-BARRIER in next pass (same resource)
            bool merged = false;
            eastl::vector<ImageMemoryBarrier2> nextPreBarriers;

            for (const auto& preBarrier : nextPass->preBarriers) {
                if (preBarrier.resourceName == postBarrier.resourceName) {
                    // Found matching PRE-BARRIER - merge into POST-BARRIER
                    // Merged barrier combines:
                    //   - src* fields from POST-BARRIER (write completion)
                    //   - dst* fields from PRE-BARRIER (next usage)
                    //   - oldLayout from POST, newLayout from PRE (complete transition)
                    postBarrier.dstStageMask = preBarrier.dstStageMask;
                    postBarrier.dstAccessMask = preBarrier.dstAccessMask;
                    postBarrier.newLayout = preBarrier.newLayout;

                    merged = true;
                    barrierStats.mergedPostPreBarriers++;
                    fmt::print("  Merged POST+PRE barrier for '{}' between pass '{}' and '{}'\n",
                              postBarrier.resourceName.c_str(), currentPass->name.c_str(), nextPass->name.c_str());
                    fmt::print("    Result: {} ({} → {}) → {} ({} → {})\n",
                              toString(postBarrier.srcStageMask).c_str(),
                              toString(postBarrier.srcAccessMask).c_str(),
                              toString(postBarrier.dstAccessMask).c_str(),
                              toString(postBarrier.dstStageMask).c_str(),
                              toString(postBarrier.oldLayout).c_str(),
                              toString(postBarrier.newLayout).c_str());
                } else {
                    // Keep PRE-BARRIER that doesn't match
                    nextPreBarriers.push_back(preBarrier);
                }
            }

            // If no merge happened, keep all PRE-BARRIERS
            if (!merged) {
                nextPreBarriers = nextPass->preBarriers;
            }

            // Update next pass's PRE-BARRIERS
            nextPass->preBarriers = eastl::move(nextPreBarriers);
        }
    }

    fmt::print("\nMerging results:\n");
    fmt::print("  Total generated: {}\n", barrierStats.totalGenerated);
    fmt::print("  Merged flush barriers: {}\n", barrierStats.mergedFlushBarriers);
    fmt::print("  Removed redundant PRE-BARRIERS: {}\n", barrierStats.removedRedundantPreBarriers);
    fmt::print("  Merged POST+PRE barriers: {}\n", barrierStats.mergedPostPreBarriers);
    fmt::print("  Total removed: {}\n", barrierStats.totalRemoved());
    fmt::print("  Final barrier count: {}\n", barrierStats.final());

    if (barrierStats.totalGenerated > 0) {
        float reduction = (float)barrierStats.totalRemoved() / (float)barrierStats.totalGenerated * 100.0f;
        fmt::print("  Reduction: {:.1f}%\n", reduction);
    }
}

// ============================================================================
// Compilation & Execution
// ============================================================================

void TestRenderGraph::compile() {
    if (!built) {
        fmt::print("ERROR: Must call build() before compile()\n");
        return;
    }

    fmt::print("\n=== Compiling (Barrier Generation) ===\n");
    generateBarriers();
    mergeBarriers();  // Optimize barriers after generation
    compiled = true;
    fmt::print("Compilation complete\n");
}

void TestRenderGraph::execute(const eastl::string& basename) {
    if (!compiled) {
        fmt::print("ERROR: Must call compile() before execute()\n");
        return;
    }

    fmt::print("\n=== Execution Sequence ===\n");
    fmt::print("Total passes: {}\n\n", compiledPasses.size());

    // Output execution sequence showing barriers + passes
    for (auto* pass : compiledPasses) {
        // PRE-BARRIERS (Invalidate Bucket)
        if (!pass->preBarriers.empty()) {
            fmt::print("┌─────────────────────────────────────────────────────────────────┐\n");
            fmt::print("│ PRE-BARRIERS for Pass [{}] '{}'                                \n",
                      pass->passIndex, pass->name.c_str());
            fmt::print("└─────────────────────────────────────────────────────────────────┘\n");

            for (size_t i = 0; i < pass->preBarriers.size(); ++i) {
                fmt::print("  Barrier #{}\n", i + 1);
                printBarrier(pass->preBarriers[i]);
                fmt::print("\n");
            }
        }

        // PASS EXECUTION
        fmt::print("╔═════════════════════════════════════════════════════════════════╗\n");
        fmt::print("║ EXECUTE PASS [{}] '{}'                                          \n",
                  pass->passIndex, pass->name.c_str());
        fmt::print("╚═════════════════════════════════════════════════════════════════╝\n");

        // Show resource accesses
        fmt::print("  Resource Accesses:\n");
        for (const auto& access : pass->accesses) {
            const char* accessType = access.isWrite ? "WRITE" : "READ";
            fmt::print("    {} '{}' (usage: {})\n",
                      accessType, access.resourceName.c_str(),
                      static_cast<int>(access.usage));
        }
        fmt::print("\n");

        // POST-BARRIERS (Flush Bucket)
        if (!pass->postBarriers.empty()) {
            fmt::print("┌─────────────────────────────────────────────────────────────────┐\n");
            fmt::print("│ POST-BARRIERS for Pass [{}] '{}'                               \n",
                      pass->passIndex, pass->name.c_str());
            fmt::print("└─────────────────────────────────────────────────────────────────┘\n");

            for (size_t i = 0; i < pass->postBarriers.size(); ++i) {
                fmt::print("  Barrier #{}\n", i + 1);
                printBarrier(pass->postBarriers[i]);
                fmt::print("\n");
            }
        }

        fmt::print("\n");
    }

    // Export to text file
    eastl::string txtFile = basename + ".txt";
    exportExecutionSequence(txtFile);

    // Export barrier sequence DOT for visualization
    eastl::string dotFile = basename + "_sequence.dot";
    exportBarrierSequenceDot(dotFile);

    fmt::print("Execution sequence complete\n");
}

/**
 * Export execution sequence to text file for visualization
 */
void TestRenderGraph::exportExecutionSequence(const eastl::string& filename) const {
    FILE* file = fopen(filename.c_str(), "w");
    if (!file) {
        fmt::print("ERROR: Cannot open file '{}' for writing\n", filename.c_str());
        return;
    }

    fprintf(file, "=================================================================\n");
    fprintf(file, "  RenderGraph Execution Sequence with Barriers\n");
    fprintf(file, "=================================================================\n");
    fprintf(file, "Total Passes: %zu\n\n", compiledPasses.size());

    for (auto* pass : compiledPasses) {
        // PRE-BARRIERS
        if (!pass->preBarriers.empty()) {
            fprintf(file, "┌─────────────────────────────────────────────────────────────────┐\n");
            fprintf(file, "│ PRE-BARRIERS for Pass [%u] '%s'\n",
                   pass->passIndex, pass->name.c_str());
            fprintf(file, "└─────────────────────────────────────────────────────────────────┘\n");

            for (size_t i = 0; i < pass->preBarriers.size(); ++i) {
                const auto& b = pass->preBarriers[i];
                fprintf(file, "  Barrier #%zu\n", i + 1);
                fprintf(file, "    Resource: '%s'\n", b.resourceName.c_str());
                fprintf(file, "      srcStage: %s → dstStage: %s\n",
                       toString(b.srcStageMask).c_str(), toString(b.dstStageMask).c_str());
                fprintf(file, "      srcAccess: %s → dstAccess: %s\n",
                       toString(b.srcAccessMask).c_str(), toString(b.dstAccessMask).c_str());
                fprintf(file, "      oldLayout: %s → newLayout: %s\n",
                       toString(b.oldLayout).c_str(), toString(b.newLayout).c_str());
                fprintf(file, "      %s\n", b.isEmpty() ? "[EMPTY BARRIER]" : "[TRANSITION REQUIRED]");
                fprintf(file, "\n");
            }
        }

        // PASS EXECUTION
        fprintf(file, "╔═════════════════════════════════════════════════════════════════╗\n");
        fprintf(file, "║ EXECUTE PASS [%u] '%s'\n", pass->passIndex, pass->name.c_str());
        fprintf(file, "╚═════════════════════════════════════════════════════════════════╝\n");

        fprintf(file, "  Resource Accesses:\n");
        for (const auto& access : pass->accesses) {
            const char* accessType = access.isWrite ? "WRITE" : "READ";
            fprintf(file, "    %s '%s'\n", accessType, access.resourceName.c_str());
        }
        fprintf(file, "\n");

        // POST-BARRIERS
        if (!pass->postBarriers.empty()) {
            fprintf(file, "┌─────────────────────────────────────────────────────────────────┐\n");
            fprintf(file, "│ POST-BARRIERS for Pass [%u] '%s'\n",
                   pass->passIndex, pass->name.c_str());
            fprintf(file, "└─────────────────────────────────────────────────────────────────┘\n");

            for (size_t i = 0; i < pass->postBarriers.size(); ++i) {
                const auto& b = pass->postBarriers[i];
                fprintf(file, "  Barrier #%zu\n", i + 1);
                fprintf(file, "    Resource: '%s'\n", b.resourceName.c_str());
                fprintf(file, "      srcStage: %s → dstStage: %s\n",
                       toString(b.srcStageMask).c_str(), toString(b.dstStageMask).c_str());
                fprintf(file, "      srcAccess: %s → dstAccess: %s\n",
                       toString(b.srcAccessMask).c_str(), toString(b.dstAccessMask).c_str());
                fprintf(file, "      oldLayout: %s → newLayout: %s\n",
                       toString(b.oldLayout).c_str(), toString(b.newLayout).c_str());
                fprintf(file, "      %s\n", b.isEmpty() ? "[EMPTY BARRIER]" : "[TRANSITION REQUIRED]");
                fprintf(file, "\n");
            }
        }

        fprintf(file, "\n");
    }

    fprintf(file, "=================================================================\n");
    fprintf(file, "End of Execution Sequence\n");
    fprintf(file, "=================================================================\n");

    fclose(file);
    fmt::print("Exported execution sequence to: {}\n", filename.c_str());
}

// ============================================================================
// Utilities
// ============================================================================

void TestRenderGraph::clear() {
    resources.clear();
    passes.clear();
    compiledPasses.clear();
    built = false;
    compiled = false;
}

void TestRenderGraph::debugPrint() const {
    fmt::print("\n=== Dependency Graph Debug ===\n");

    // Resources
    fmt::print("\nResources: {}\n", resources.size());
    for (const auto& [name, res] : resources) {
        const char* typeStr = res.isExternal ? "external" :
                             (res.isPersistent ? "persistent" : "transient");
        fmt::print("  '{}': {} [lifetime: {}, {}]\n",
                  name.c_str(), typeStr, res.firstUse, res.lastUse);
    }

    // All passes
    fmt::print("\nAll Passes: {}\n", passes.size());
    for (const auto& pass : passes) {
        const char* status = pass->reachable ? "REACHABLE" : "CULLED";
        fmt::print("  [{}] '{}' ({})\n", pass->passIndex, pass->name.c_str(), status);

        // Accesses
        for (const auto& access : pass->accesses) {
            const char* accessType = access.isWrite ? "WRITE" : "READ";
            fmt::print("    {} '{}'\n", accessType, access.resourceName.c_str());
        }

        // Dependencies
        if (!pass->dependencies.empty()) {
            fmt::print("    Dependencies: [");
            for (size_t i = 0; i < pass->dependencies.size(); ++i) {
                if (i > 0) fmt::print(", ");
                fmt::print("{}", pass->dependencies[i]);
            }
            fmt::print("]\n");
        }
    }

    // Compiled passes
    fmt::print("\nCompiled Passes: {}\n", compiledPasses.size());
    for (const auto& pass : compiledPasses) {
        fmt::print("  [{}] '{}'\n", pass->passIndex, pass->name.c_str());
    }

    fmt::print("==============================\n");
}

const LogicalResource* TestRenderGraph::getResource(const eastl::string& name) const {
    auto it = resources.find(name);
    return (it != resources.end()) ? &it->second : nullptr;
}

// ============================================================================
// Topological Sort with Optimization
// ============================================================================

/**
 * Get expected image layout for a given resource usage
 */
ImageLayout TestRenderGraph::getLayoutForUsage(ResourceUsage usage) const {
    switch (usage) {
        case ResourceUsage::ColorAttachment: return ImageLayout::ColorAttachment;
        case ResourceUsage::DepthAttachment: return ImageLayout::DepthStencilAttachment;
        case ResourceUsage::ShaderRead: return ImageLayout::ShaderReadOnly;
        case ResourceUsage::ShaderWrite: return ImageLayout::General;
        case ResourceUsage::TransferSrc: return ImageLayout::TransferSrc;
        case ResourceUsage::TransferDst: return ImageLayout::TransferDst;
        case ResourceUsage::Present: return ImageLayout::PresentSrc;
        default: return ImageLayout::Undefined;
    }
}

/**
 * Count shared resources between two passes
 */
int TestRenderGraph::countSharedResources(PassNode* a, PassNode* b) {
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

/**
 * Calculate number of layout transitions between two consecutive passes
 */
int TestRenderGraph::calculateLayoutTransitions(PassNode* next, PassNode* prev) {
    if (!next || !prev) return 0;

    int transitions = 0;

    // Check each resource in next pass
    for (const auto& accessNext : next->accesses) {
        // Find if prev pass accessed the same resource
        for (const auto& accessPrev : prev->accesses) {
            if (accessNext.resourceName == accessPrev.resourceName) {
                ImageLayout prevLayout = getLayoutForUsage(accessPrev.usage);
                ImageLayout nextLayout = getLayoutForUsage(accessNext.usage);

                if (prevLayout != nextLayout) {
                    transitions++;
                }
                break;
            }
        }
    }

    return transitions;
}

/**
 * Select optimal pass from ready queue using heuristics
 * Considers:
 * 1. External resource constraints (hard constraints)
 * 2. Resource locality (shared resources with previous pass)
 * 3. Layout transition cost
 */
PassNode* TestRenderGraph::selectOptimalPass(
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

/**
 * Topological sort with optimization
 * Uses Kahn's algorithm with custom heuristics to optimize execution order
 */
void TestRenderGraph::topologicalSortWithOptimization() {
    if (compiledPasses.empty()) {
        fmt::print("WARNING: No passes to sort\n");
        return;
    }

    fmt::print("\n=== Topological Sort with Optimization ===\n");

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

    // Identify external resource constraints
    eastl::hash_map<eastl::string, uint32_t> externalFirstUse;
    eastl::hash_map<eastl::string, uint32_t> externalLastUse;

    for (const auto& [name, res] : resources) {
        if (res.isExternal && res.firstUse != UINT32_MAX) {
            externalFirstUse[name] = res.firstUse;
            externalLastUse[name] = res.lastUse;
        }
    }

    fmt::print("External resource constraints:\n");
    for (const auto& [name, firstUse] : externalFirstUse) {
        fmt::print("  '{}': firstUse={}, lastUse={}\n",
                  name.c_str(), firstUse, externalLastUse[name]);
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

    fmt::print("\nSorting passes:\n");
    while (!readyQueue.empty()) {
        // Select optimal pass from ready queue
        PassNode* bestPass = selectOptimalPass(readyQueue, lastExecuted,
                                               externalFirstUse, externalLastUse);

        fmt::print("  Selected: '{}'\n", bestPass->name.c_str());
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
                    fmt::print("    → '{}' is now ready\n", pass->name.c_str());
                }
            }
        }
    }

    // Check if all passes were sorted (cycle detection)
    if (sorted.size() != compiledPasses.size()) {
        fmt::print("ERROR: Cyclic dependency detected! Only {} of {} passes sorted\n",
                  sorted.size(), compiledPasses.size());
        return;
    }

    // Update compiledPasses with sorted order
    compiledPasses = sorted;

    // Reassign pass indices to match new order
    for (uint32_t i = 0; i < compiledPasses.size(); ++i) {
        compiledPasses[i]->passIndex = i;
    }

    fmt::print("Topological sort complete: {} passes\n", compiledPasses.size());
}

/**
 * Export barrier sequence to Graphviz DOT format for execution flow visualization
 * Shows: PRE-BARRIERS → PASS → POST-BARRIERS in execution order
 * WITH resource nodes and edges showing barrier→resource ownership
 * Usage: dot -Tpng output.dot -o output.png
 */
void TestRenderGraph::exportBarrierSequenceDot(const eastl::string& filename) const {
    FILE* file = fopen(filename.c_str(), "w");
    if (!file) {
        fmt::print("ERROR: Cannot open file '{}' for writing\n", filename.c_str());
        return;
    }

    fprintf(file, "digraph BarrierSequence {\n");
    fprintf(file, "  rankdir=LR;\n");  // Left to right layout (horizontal)
    fprintf(file, "  node [fontname=\"Arial\", fontsize=10];\n");
    fprintf(file, "  edge [fontname=\"Arial\", fontsize=9];\n\n");

    // Create nodes for each step in execution sequence
    uint32_t nodeId = 0;

    for (uint32_t passIdx = 0; passIdx < compiledPasses.size(); ++passIdx) {
        auto* pass = compiledPasses[passIdx];

        // Create subgraph for this pass
        fprintf(file, "  subgraph cluster_pass%u {\n", passIdx);
        fprintf(file, "    label=\"Pass [%u] %s\";\n", passIdx, pass->name.c_str());
        fprintf(file, "    style=dashed;\n");
        fprintf(file, "    color=blue;\n\n");

        // PRE-BARRIERS
        if (!pass->preBarriers.empty()) {
            fprintf(file, "    // PRE-BARRIERS\n");
            for (size_t i = 0; i < pass->preBarriers.size(); ++i) {
                const auto& barrier = pass->preBarriers[i];

                // Create label with barrier details including stage and access
                eastl::string label;
                label += "PRE-BARRIER [";
                label += barrier.resourceName.c_str();
                label += "]\\n";
                label += "Layout: ";
                label += toString(barrier.oldLayout).c_str();
                label += " → ";
                label += toString(barrier.newLayout).c_str();
                label += "\\n";
                label += "Stage: ";
                label += toString(barrier.srcStageMask).c_str();
                label += " → ";
                label += toString(barrier.dstStageMask).c_str();
                label += "\\n";
                label += "Access: ";
                label += toString(barrier.srcAccessMask).c_str();
                label += " → ";
                label += toString(barrier.dstAccessMask).c_str();

                fprintf(file, "    node%u [label=\"%s\", shape=box, style=filled, fillcolor=\"lightyellow\"];\n",
                       nodeId, label.c_str());

                if (i == 0 && passIdx > 0) {
                    // Connect to previous pass's last node
                    fprintf(file, "    node%u -> node%u [style=dashed];\n", nodeId - 1, nodeId);
                }
                if (i > 0) {
                    fprintf(file, "    node%u -> node%u;\n", nodeId - 1, nodeId);
                }
                nodeId++;
            }
            fprintf(file, "\n");
        }

        // PASS EXECUTION
        fprintf(file, "    // PASS EXECUTION\n");
        eastl::string passLabel;
        passLabel += "EXECUTE\\n";
        passLabel += pass->name.c_str();

        fprintf(file, "    node%u [label=\"%s\", shape=box, style=\"filled,bold\", fillcolor=\"lightblue\", penwidth=2];\n",
               nodeId, passLabel.c_str());

        if (!pass->preBarriers.empty()) {
            fprintf(file, "    node%u -> node%u [penwidth=2];\n", nodeId - 1, nodeId);
        } else if (passIdx > 0) {
            fprintf(file, "    node%u -> node%u [style=dashed];\n", nodeId - 1, nodeId);
        }
        nodeId++;
        fprintf(file, "\n");

        // POST-BARRIERS
        if (!pass->postBarriers.empty()) {
            fprintf(file, "    // POST-BARRIERS\n");
            for (size_t i = 0; i < pass->postBarriers.size(); ++i) {
                const auto& barrier = pass->postBarriers[i];

                // Create label with barrier details including stage and access
                eastl::string label;
                label += "POST-BARRIER [";
                label += barrier.resourceName.c_str();
                label += "]\\n";
                label += "Layout: ";
                label += toString(barrier.oldLayout).c_str();
                label += " → ";
                label += toString(barrier.newLayout).c_str();
                label += "\\n";
                label += "Stage: ";
                label += toString(barrier.srcStageMask).c_str();
                label += " → ";
                label += toString(barrier.dstStageMask).c_str();
                label += "\\n";
                label += "Access: ";
                label += toString(barrier.srcAccessMask).c_str();
                label += " → ";
                label += toString(barrier.dstAccessMask).c_str();

                fprintf(file, "    node%u [label=\"%s\", shape=box, style=filled, fillcolor=\"lightgreen\"];\n",
                       nodeId, label.c_str());
                fprintf(file, "    node%u -> node%u;\n", nodeId - 1, nodeId);
                nodeId++;
            }
            fprintf(file, "\n");
        }

        fprintf(file, "  }\n\n");
    }

    // Legend
    fprintf(file, "  // Legend\n");
    fprintf(file, "  subgraph cluster_legend {\n");
    fprintf(file, "    label=\"Legend\";\n");
    fprintf(file, "    style=dashed;\n");
    fprintf(file, "    rankdir=LR;\n");
    fprintf(file, "    legend_pre [label=\"PRE-BARRIER\\n[resource]\\n(Invalidate)\", shape=box, style=filled, fillcolor=lightyellow];\n");
    fprintf(file, "    legend_pass [label=\"PASS\\n(Execute)\", shape=box, style=\"filled,bold\", fillcolor=lightblue, penwidth=2];\n");
    fprintf(file, "    legend_post [label=\"POST-BARRIER\\n[resource]\\n(Flush)\", shape=box, style=filled, fillcolor=lightgreen];\n");
    fprintf(file, "    legend_pre -> legend_pass -> legend_post [style=invis];\n");
    fprintf(file, "  }\n");

    fprintf(file, "}\n");
    fclose(file);

    fmt::print("Exported barrier sequence DOT: {}\n", filename.c_str());
    fmt::print("Generate image with: dot -Tpng {} -o sequence.png\n", filename.c_str());
}

/**
 * Export dependency graph to Graphviz DOT format
 * Usage: dot -Tpng output.dot -o output.png
 */
void TestRenderGraph::exportDot(const eastl::string& filename) const {
    FILE* file = fopen(filename.c_str(), "w");
    if (!file) {
        fmt::print("ERROR: Cannot open file '{}' for writing\n", filename.c_str());
        return;
    }

    fprintf(file, "digraph RenderGraph {\n");
    fprintf(file, "  rankdir=LR;\n");  // Left to right layout
    fprintf(file, "  node [shape=box];\n\n");

    // Style definitions
    fprintf(file, "  // Styles\n");
    fprintf(file, "  node [fontname=\"Arial\"];\n");
    fprintf(file, "  edge [fontname=\"Arial\", fontsize=10];\n\n");

    // Pass nodes
    fprintf(file, "  // Pass nodes\n");
    for (const auto& pass : passes) {
        const char* color = pass->reachable ? "lightblue" : "lightgray";
        const char* style = pass->reachable ? "filled,bold" : "filled,dashed";
        const char* status = pass->reachable ? "" : " [CULLED]";

        fprintf(file, "  pass_%u [label=\"[%u] %s%s\", style=\"%s\", fillcolor=\"%s\"];\n",
                pass->passIndex, pass->passIndex, pass->name.c_str(), status, style, color);
    }

    // Resource nodes
    fprintf(file, "\n  // Resource nodes\n");
    for (const auto& [name, res] : resources) {
        const char* shape = (res.type == ResourceType::Image) ? "ellipse" : "box";
        const char* color = res.isExternal ? "lightgreen" :
                           (res.isPersistent ? "lightyellow" : "white");

        eastl::string label = name;
        if (res.firstUse != UINT32_MAX) {
            char lifetime[64];
            snprintf(lifetime, sizeof(lifetime), "\\n[%u, %u]", res.firstUse, res.lastUse);
            label += lifetime;
        }

        fprintf(file, "  res_%s [label=\"%s\", shape=%s, style=filled, fillcolor=\"%s\"];\n",
                name.c_str(), label.c_str(), shape, color);
    }

    // Dependency edges (pass → pass)
    fprintf(file, "\n  // Dependencies (pass → pass)\n");
    for (const auto& pass : passes) {
        if (!pass->reachable) continue;  // Skip culled passes

        for (uint32_t depIdx : pass->dependencies) {
            fprintf(file, "  pass_%u -> pass_%u [color=red, penwidth=2, label=\"depends\"];\n",
                    depIdx, pass->passIndex);
        }
    }

    // Resource access edges (pass → resource, resource → pass)
    fprintf(file, "\n  // Resource accesses\n");
    for (const auto& pass : passes) {
        if (!pass->reachable) continue;  // Skip culled passes

        for (const auto& access : pass->accesses) {
            if (access.isWrite) {
                // pass writes resource
                const char* color = (access.usage == ResourceUsage::Present) ? "green" : "blue";
                const char* label = (access.usage == ResourceUsage::Present) ? "present" : "write";
                fprintf(file, "  pass_%u -> res_%s [color=%s, label=\"%s\"];\n",
                        pass->passIndex, access.resourceName.c_str(), color, label);
            } else {
                // pass reads resource
                fprintf(file, "  res_%s -> pass_%u [color=gray, label=\"read\"];\n",
                        access.resourceName.c_str(), pass->passIndex);
            }
        }
    }

    // Legend
    fprintf(file, "\n  // Legend\n");
    fprintf(file, "  subgraph cluster_legend {\n");
    fprintf(file, "    label=\"Legend\";\n");
    fprintf(file, "    style=dashed;\n");
    fprintf(file, "    legend_reachable [label=\"Reachable Pass\", style=filled, fillcolor=lightblue];\n");
    fprintf(file, "    legend_culled [label=\"Culled Pass\", style=\"filled,dashed\", fillcolor=lightgray];\n");
    fprintf(file, "    legend_external [label=\"External Resource\", shape=ellipse, style=filled, fillcolor=lightgreen];\n");
    fprintf(file, "    legend_transient [label=\"Transient Resource\", shape=ellipse, style=filled, fillcolor=white];\n");
    fprintf(file, "  }\n");

    fprintf(file, "}\n");
    fclose(file);

    fmt::print("Exported DOT file: {}\n", filename.c_str());
    fmt::print("Generate image with: dot -Tpng {} -o graph.png\n", filename.c_str());
}

} // namespace violet