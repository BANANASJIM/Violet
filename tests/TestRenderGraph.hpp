// TestRenderGraph - Simplified RenderGraph for testing DAG building
// Reference: src/renderer/graph/RenderGraph.hpp

#pragma once

#include <EASTL/vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/functional.h>
#include <EASTL/unique_ptr.h>
#include <cstdint>

namespace violet {

// Forward declarations
class MockVulkanContext;
class MockRenderPass;
class MockComputePass;

// Enums matching real RenderGraph
enum class ResourceType {
    Image,
    Buffer,
    Unknown
};

enum class ResourceUsage {
    ColorAttachment,
    DepthAttachment,
    ShaderRead,
    ShaderWrite,
    TransferSrc,
    TransferDst,
    Present
};

// Simplified layout enum (no Vulkan dependency)
enum class ImageLayout {
    Undefined,
    ColorAttachment,
    DepthStencilAttachment,
    ShaderReadOnly,
    General,
    TransferSrc,
    TransferDst,
    PresentSrc
};

// Simplified pipeline stage (single stage enum)
enum class PipelineStage {
    TopOfPipe,
    ColorAttachmentOutput,
    EarlyFragmentTests,
    LateFragmentTests,
    FragmentShader,
    ComputeShader,
    Transfer,
    BottomOfPipe
};

// Pipeline stage flags 2 (bitfield matching VkPipelineStageFlagBits2 from VK_KHR_synchronization2)
// 64-bit flags provide more granular control and better alignment with access flags
enum class PipelineStageFlags2 : uint64_t {
    None                            = 0ULL,
    TopOfPipe                       = 0x00000001ULL,
    DrawIndirect                    = 0x00000002ULL,
    VertexInput                     = 0x00000004ULL,
    VertexShader                    = 0x00000008ULL,
    TessellationControlShader       = 0x00000010ULL,
    TessellationEvaluationShader    = 0x00000020ULL,
    GeometryShader                  = 0x00000040ULL,
    FragmentShader                  = 0x00000080ULL,
    EarlyFragmentTests              = 0x00000100ULL,
    LateFragmentTests               = 0x00000200ULL,
    ColorAttachmentOutput           = 0x00000400ULL,
    ComputeShader                   = 0x00000800ULL,
    AllTransfer                     = 0x00001000ULL,  // Replaces Transfer
    BottomOfPipe                    = 0x00002000ULL,
    AllGraphics                     = 0x00008000ULL,
    AllCommands                     = 0x00010000ULL,

    // New synchronization2 stages (more granular)
    Copy                            = 0x100000000ULL,  // Explicit copy operations
    Resolve                         = 0x200000000ULL,  // Resolve operations
    Blit                            = 0x400000000ULL,  // Blit operations
    Clear                           = 0x800000000ULL,  // Clear operations
    IndexInput                      = 0x1000000000ULL, // Index buffer binding
    VertexAttributeInput            = 0x2000000000ULL, // Vertex attribute input
    PreRasterizationShaders         = 0x4000000000ULL, // All pre-rasterization shaders

    // Useful combined flags
    FragmentShadingRateAttachment   = 0x8000000000ULL,
    TaskShader                      = 0x10000000000ULL,
    MeshShader                      = 0x20000000000ULL
};

// Bitwise operators for PipelineStageFlags2
inline PipelineStageFlags2 operator|(PipelineStageFlags2 a, PipelineStageFlags2 b) {
    return static_cast<PipelineStageFlags2>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}
inline PipelineStageFlags2 operator&(PipelineStageFlags2 a, PipelineStageFlags2 b) {
    return static_cast<PipelineStageFlags2>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}
inline PipelineStageFlags2& operator|=(PipelineStageFlags2& a, PipelineStageFlags2 b) {
    a = a | b;
    return a;
}
inline bool operator==(PipelineStageFlags2 a, PipelineStageFlags2 b) {
    return static_cast<uint64_t>(a) == static_cast<uint64_t>(b);
}
inline bool operator!=(PipelineStageFlags2 a, PipelineStageFlags2 b) {
    return static_cast<uint64_t>(a) != static_cast<uint64_t>(b);
}
inline bool operator!(PipelineStageFlags2 a) {
    return static_cast<uint64_t>(a) == 0ULL;
}

// Access flags 2 (bitfield matching VkAccessFlagBits2 from VK_KHR_synchronization2)
// 64-bit flags better aligned with PipelineStageFlags2
enum class AccessFlags2 : uint64_t {
    None                           = 0ULL,
    IndirectCommandRead            = 0x00000001ULL,
    IndexRead                      = 0x00000002ULL,
    VertexAttributeRead            = 0x00000004ULL,
    UniformRead                    = 0x00000008ULL,
    InputAttachmentRead            = 0x00000010ULL,
    ShaderRead                     = 0x00000020ULL,
    ShaderWrite                    = 0x00000040ULL,
    ColorAttachmentRead            = 0x00000080ULL,
    ColorAttachmentWrite           = 0x00000100ULL,
    DepthStencilAttachmentRead     = 0x00000200ULL,
    DepthStencilAttachmentWrite    = 0x00000400ULL,
    TransferRead                   = 0x00000800ULL,
    TransferWrite                  = 0x00001000ULL,
    HostRead                       = 0x00002000ULL,
    HostWrite                      = 0x00004000ULL,
    MemoryRead                     = 0x00008000ULL,
    MemoryWrite                    = 0x00010000ULL,

    // New synchronization2 access types
    ShaderSampledRead              = 0x100000000ULL,  // Explicit sampled image read
    ShaderStorageRead              = 0x200000000ULL,  // Explicit storage image/buffer read
    ShaderStorageWrite             = 0x400000000ULL,  // Explicit storage image/buffer write

    // Useful combined flags
    ShaderReadWrite                = ShaderRead | ShaderWrite,
    ColorAttachmentReadWrite       = ColorAttachmentRead | ColorAttachmentWrite,
    DepthStencilAttachmentReadWrite = DepthStencilAttachmentRead | DepthStencilAttachmentWrite
};

// Bitwise operators for AccessFlags2
inline AccessFlags2 operator|(AccessFlags2 a, AccessFlags2 b) {
    return static_cast<AccessFlags2>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}
inline AccessFlags2 operator&(AccessFlags2 a, AccessFlags2 b) {
    return static_cast<AccessFlags2>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}
inline AccessFlags2& operator|=(AccessFlags2& a, AccessFlags2 b) {
    a = a | b;
    return a;
}
inline bool operator==(AccessFlags2 a, AccessFlags2 b) {
    return static_cast<uint64_t>(a) == static_cast<uint64_t>(b);
}
inline bool operator!=(AccessFlags2 a, AccessFlags2 b) {
    return static_cast<uint64_t>(a) != static_cast<uint64_t>(b);
}
inline bool operator!(AccessFlags2 a) {
    return static_cast<uint64_t>(a) == 0ULL;
}

// Simplified descriptors (no Vulkan types)
struct ImageDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
};

struct BufferDesc {
    uint64_t size = 0;
};

// Simplified resource handle
struct ResourceHandle {
    uint32_t id = 0;

    static ResourceHandle allocate() {
        static uint32_t nextId = 1;
        return ResourceHandle{nextId++};
    }

    constexpr bool valid() const noexcept { return id != 0; }
};

constexpr ResourceHandle InvalidResource{0};

// Vulkan queue family index for "ignored" (no ownership transfer)
constexpr uint32_t VK_QUEUE_FAMILY_IGNORED = 0xFFFFFFFF;

// Image memory barrier 2 (matching VkImageMemoryBarrier2 from VK_KHR_synchronization2)
// Self-contained barrier with 64-bit stage/access masks for better precision
struct ImageMemoryBarrier2 {
    eastl::string resourceName;  // For lookup during execution

    // synchronization2 barrier properties (64-bit flags)
    PipelineStageFlags2 srcStageMask = PipelineStageFlags2::None;
    PipelineStageFlags2 dstStageMask = PipelineStageFlags2::None;
    AccessFlags2 srcAccessMask = AccessFlags2::None;
    AccessFlags2 dstAccessMask = AccessFlags2::None;
    ImageLayout oldLayout = ImageLayout::Undefined;
    ImageLayout newLayout = ImageLayout::Undefined;
    uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    // Check if barrier is empty (no transition needed)
    bool isEmpty() const {
        return oldLayout == newLayout &&
               srcStageMask == dstStageMask &&
               srcAccessMask == dstAccessMask;
    }
};

// Barrier optimization statistics
struct BarrierStats {
    uint32_t totalGenerated = 0;
    uint32_t removedRedundantReads = 0;
    uint32_t mergedFlushBarriers = 0;
    uint32_t removedRedundantPreBarriers = 0;
    uint32_t mergedPostPreBarriers = 0;  // POST+PRE barrier pairs merged into single barriers

    uint32_t totalRemoved() const {
        return removedRedundantReads + mergedFlushBarriers + removedRedundantPreBarriers + mergedPostPreBarriers;
    }

    uint32_t final() const {
        return totalGenerated - totalRemoved();
    }
};

// Logical resource
struct LogicalResource {
    ResourceHandle handle{0};
    eastl::string name;
    ResourceType type = ResourceType::Unknown;

    bool isExternal = false;
    bool isPersistent = false;

    ImageDesc imageDesc;
    BufferDesc bufferDesc;

    uint32_t firstUse = UINT32_MAX;
    uint32_t lastUse = 0;

    // External resource constraints (initial/final layout and stage)
    struct ExternalConstraints {
        ImageLayout initialLayout = ImageLayout::Undefined;
        ImageLayout finalLayout = ImageLayout::Undefined;
        PipelineStage initialStage = PipelineStage::TopOfPipe;
        PipelineStage finalStage = PipelineStage::BottomOfPipe;
    } externalConstraints;

    // Current resource state (tracked during barrier generation)
    // Using synchronization2 64-bit flags for better precision
    struct State {
        ImageLayout layout = ImageLayout::Undefined;
        PipelineStage stage = PipelineStage::TopOfPipe;

        // Per-stage invalidation tracking (for barrier optimization)
        PipelineStageFlags2 validStages = PipelineStageFlags2::None;
        AccessFlags2 validAccess = AccessFlags2::None;
    } state;
};

// Pass node
struct PassNode {
    eastl::string name;
    uint32_t passIndex = 0;
    bool reachable = false;

    struct ResourceAccess {
        eastl::string resourceName;
        ResourceUsage usage;
        bool isWrite;
    };
    eastl::vector<ResourceAccess> accesses;

    // Dependency graph
    eastl::vector<uint32_t> dependencies;  // Indices of passes this depends on

    // Dual bucket barrier system (generated during compile())
    // Using synchronization2 barriers
    eastl::vector<ImageMemoryBarrier2> preBarriers;   // Invalidate bucket (validate - before pass execution)
    eastl::vector<ImageMemoryBarrier2> postBarriers;  // Flush bucket (after pass execution)
};

// Main test render graph class
class TestRenderGraph {
public:
    TestRenderGraph() = default;
    ~TestRenderGraph() = default;

    TestRenderGraph(const TestRenderGraph&) = delete;
    TestRenderGraph& operator=(const TestRenderGraph&) = delete;

    // PassBuilder - API for declaring resource dependencies
    class PassBuilder {
    public:
        PassBuilder(PassNode& node);

        PassBuilder& read(const eastl::string& resourceName, ResourceUsage usage = ResourceUsage::ShaderRead);
        PassBuilder& write(const eastl::string& resourceName, ResourceUsage usage = ResourceUsage::ColorAttachment);
        PassBuilder& execute(eastl::function<void()> callback);

    private:
        PassNode& node;
};

    // Lifecycle
    void init(MockVulkanContext* ctx);
    void cleanup();

    // Resource creation
    ResourceHandle importImage(
        const eastl::string& name,
        void* externalImage,
        ImageLayout initialLayout = ImageLayout::Undefined,
        ImageLayout finalLayout = ImageLayout::Undefined,
        PipelineStage initialStage = PipelineStage::TopOfPipe,
        PipelineStage finalStage = PipelineStage::BottomOfPipe
    );
    ResourceHandle importBuffer(const eastl::string& name, void* externalBuffer);
    ResourceHandle createImage(const eastl::string& name, const ImageDesc& desc, bool persistent);
    ResourceHandle createBuffer(const eastl::string& name, const BufferDesc& desc, bool persistent);

    // Pass creation
    void addPass(const eastl::string& name,
                 eastl::function<void(PassBuilder&, MockRenderPass&)> setupCallback);
    void addComputePass(const eastl::string& name,
                        eastl::function<void(PassBuilder&, MockComputePass&)> setupCallback);

    // Graph building
    void build();       // TODO: Implement DAG building with backward traversal
    void compile();     // TODO: Implement barrier generation
    void execute(const eastl::string& basename = "barrier_sequence");     // Execute and export barrier sequence

    // Utilities
    void clear();
    void debugPrint() const;
    void exportDot(const eastl::string& filename) const;  // Export Graphviz DOT file
    const LogicalResource* getResource(const eastl::string& name) const;

private:
    MockVulkanContext* context = nullptr;

    eastl::hash_map<eastl::string, LogicalResource> resources;
    eastl::vector<eastl::unique_ptr<PassNode>> passes;
    eastl::vector<PassNode*> compiledPasses;  // Non-owning pointers to reachable passes

    bool built = false;
    bool compiled = false;

    // Core algorithms
    void buildDependencyGraph();  // Backward traversal from Present passes
    void pruneUnreachable();      // Remove culled passes
    void computeLifetimes();      // Calculate resource firstUse/lastUse
    void topologicalSortWithOptimization();  // Optimize pass execution order

    // Optimization heuristics
    PassNode* selectOptimalPass(
        const eastl::vector<PassNode*>& readyQueue,
        PassNode* lastExecuted,
        const eastl::hash_map<eastl::string, uint32_t>& externalFirstUse,
        const eastl::hash_map<eastl::string, uint32_t>& externalLastUse
    );

    int countSharedResources(PassNode* a, PassNode* b);
    int calculateLayoutTransitions(PassNode* next, PassNode* prev);
    ImageLayout getLayoutForUsage(ResourceUsage usage) const;

    // Resource usage tracking for barrier generation
    struct ResourceUsageInfo {
        uint32_t passIndex;
        ResourceUsage usage;
        bool isWrite;
        PipelineStageFlags2 stage;
        AccessFlags2 access;
        ImageLayout layout;
    };

    // Resource usage table: resource name → list of all usages
    eastl::hash_map<eastl::string, eastl::vector<ResourceUsageInfo>> resourceUsageTable;

    // Barrier generation helpers (synchronization2)
    PipelineStageFlags2 getStageForUsage(ResourceUsage usage) const;
    AccessFlags2 getAccessForUsage(ResourceUsage usage, bool isWrite) const;
    void buildResourceUsageTable();  // Build usage table before barrier generation
    const ResourceUsageInfo* findNextUser(const eastl::string& resourceName, uint32_t currentPassIndex) const;  // Forward lookup
    void generateBarriers();  // Core barrier generation algorithm
    void mergeBarriers();  // Barrier optimization (merge redundant barriers)
    bool barriersAreEquivalent(const ImageMemoryBarrier2& a, const ImageMemoryBarrier2& b, bool compareResourceName = true) const;
    void exportExecutionSequence(const eastl::string& filename) const;

    // Barrier optimization statistics
    BarrierStats barrierStats;

    // String conversion helpers (synchronization2)
    eastl::string toString(PipelineStageFlags2 flags) const;
    eastl::string toString(AccessFlags2 flags) const;
    eastl::string toString(ImageLayout layout) const;
    void printBarrier(const ImageMemoryBarrier2& barrier) const;

    // Barrier sequence visualization
    void exportBarrierSequenceDot(const eastl::string& filename) const;
};

// Mock classes for API compatibility
class MockVulkanContext {
public:
    // Empty stub - no initialization needed for testing
};

class MockRenderPass {
public:
    void init(MockVulkanContext* ctx, const eastl::string& name) { this->name = name; }
    void cleanup() {}

    void execute() {}  // Simplified (no command buffer in test)

    const eastl::string& getName() const { return name; }

    const eastl::vector<ResourceHandle>& getReadResources() const { return reads; }
    const eastl::vector<ResourceHandle>& getWriteResources() const { return writes; }

    void setReadResources(const eastl::vector<ResourceHandle>& r) { reads = r; }
    void setWriteResources(const eastl::vector<ResourceHandle>& w) { writes = w; }

    void setExecuteCallback(eastl::function<void()> cb) { executeCallback = eastl::move(cb); }

private:
    eastl::string name;
    eastl::vector<ResourceHandle> reads;
    eastl::vector<ResourceHandle> writes;
    eastl::function<void()> executeCallback;
};

class MockComputePass {
public:
    void init(MockVulkanContext* ctx, const eastl::string& name) { this->name = name; }
    void cleanup() {}

    void execute() {}  // Simplified (no command buffer in test)

    const eastl::string& getName() const { return name; }

    const eastl::vector<ResourceHandle>& getReadResources() const { return reads; }
    const eastl::vector<ResourceHandle>& getWriteResources() const { return writes; }

    void setReadResources(const eastl::vector<ResourceHandle>& r) { reads = r; }
    void setWriteResources(const eastl::vector<ResourceHandle>& w) { writes = w; }

    void setExecuteCallback(eastl::function<void()> cb) { executeCallback = eastl::move(cb); }

private:
    eastl::string name;
    eastl::vector<ResourceHandle> reads;
    eastl::vector<ResourceHandle> writes;
    eastl::function<void()> executeCallback;
};

} // namespace violet