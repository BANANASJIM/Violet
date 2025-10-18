#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/functional.h>
#include <EASTL/unique_ptr.h>
#include "ResourceHandle.hpp"
#include "Pass.hpp"

namespace violet {

class VulkanContext;
class TransientPool;
struct ImageResource;
struct BufferResource;
class RenderPass;
class ComputePass;

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

struct ImageDesc {
    vk::Format format = vk::Format::eUndefined;
    vk::Extent3D extent = {0, 0, 1};
    vk::ImageUsageFlags usage = {};
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    vk::ClearValue clearValue = {};  // For attachment clear operations
};

struct BufferDesc {
    vk::DeviceSize size = 0;
    vk::BufferUsageFlags usage = {};
};

struct ResourceState {
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;
    vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eTopOfPipe;
    vk::AccessFlags2 access = {};
};

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

    // External resource constraints (initial/final layout, stage, and access)
    struct ExternalConstraints {
        vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined;
        vk::ImageLayout finalLayout = vk::ImageLayout::eUndefined;
        vk::PipelineStageFlags2 initialStage = vk::PipelineStageFlagBits2::eTopOfPipe;
        vk::PipelineStageFlags2 finalStage = vk::PipelineStageFlagBits2::eBottomOfPipe;
        vk::AccessFlags2 initialAccess = {};  // Access mask from external user before RenderGraph
        vk::AccessFlags2 finalAccess = {};    // Access mask for external user after RenderGraph
    } externalConstraints;

    ResourceState state;

    union {
        const ImageResource* imageResource;
        const BufferResource* bufferResource;
        void* physicalHandle;
    };

    // For transient images: ImageView created during compile phase
    vk::ImageView transientView = VK_NULL_HANDLE;

    LogicalResource() : physicalHandle(nullptr) {}
};

struct PassNode {
    eastl::unique_ptr<Pass> pass;  // Pass object (created during build())

    struct ResourceAccess {
        eastl::string resourceName;
        ResourceUsage usage;
        bool isWrite;
    };

    eastl::vector<ResourceAccess> accesses;

    bool reachable = false;
    uint32_t passIndex = 0;

    // Dependency graph (for topological sorting)
    eastl::vector<uint32_t> dependencies;

    // Compiled rendering state (built during compile phase)
    eastl::vector<vk::RenderingAttachmentInfo> colorAttachmentInfos;
    vk::RenderingAttachmentInfo depthAttachmentInfo;
    vk::RenderingAttachmentInfo stencilAttachmentInfo;
    bool hasDepth = false;
    bool hasStencil = false;
    vk::Extent2D renderArea = {0, 0};
};

class RenderGraph {
public:
    RenderGraph() = default;
    ~RenderGraph() = default;

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    void init(VulkanContext* ctx);
    void cleanup();

    // Import external resources
    ResourceHandle importImage(
        const eastl::string& name,
        const ImageResource* imageRes,
        vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined,
        vk::ImageLayout finalLayout = vk::ImageLayout::eUndefined,
        vk::PipelineStageFlags2 initialStage = vk::PipelineStageFlagBits2::eTopOfPipe,
        vk::PipelineStageFlags2 finalStage = vk::PipelineStageFlagBits2::eBottomOfPipe,
        vk::AccessFlags2 initialAccess = {},
        vk::AccessFlags2 finalAccess = {}
    );
    ResourceHandle importBuffer(
        const eastl::string& name,
        const BufferResource* bufferRes,
        vk::PipelineStageFlags2 initialStage = vk::PipelineStageFlagBits2::eTopOfPipe,
        vk::PipelineStageFlags2 finalStage = vk::PipelineStageFlagBits2::eBottomOfPipe,
        vk::AccessFlags2 initialAccess = {},
        vk::AccessFlags2 finalAccess = {}
    );

    // Create internal resources
    ResourceHandle createImage(const eastl::string& name, const ImageDesc& desc, bool persistent);
    ResourceHandle createBuffer(const eastl::string& name, const BufferDesc& desc, bool persistent);

    class PassBuilder {
    public:
        PassBuilder(PassNode& node);

        PassBuilder& read(const eastl::string& resourceName, ResourceUsage usage = ResourceUsage::ShaderRead);
        PassBuilder& write(const eastl::string& resourceName, ResourceUsage usage = ResourceUsage::ColorAttachment);
        PassBuilder& execute(eastl::function<void(vk::CommandBuffer, uint32_t)> callback);

    private:
        PassNode& node;
    };

    void addPass(const eastl::string& name, eastl::function<void(PassBuilder&, RenderPass&)> setupCallback);
    void addComputePass(const eastl::string& name, eastl::function<void(PassBuilder&, ComputePass&)> setupCallback);

    void build();
    void compile();
    void execute(vk::CommandBuffer cmd, uint32_t frameIndex);
    void clear();

    void debugPrint() const;
    const LogicalResource* getResource(const eastl::string& name) const;

private:
    VulkanContext* context = nullptr;
    TransientPool* transientPool = nullptr;

    eastl::hash_map<eastl::string, LogicalResource> resources;

    // Pass objects created immediately in addPass()
    eastl::vector<eastl::unique_ptr<PassNode>> passes;
    eastl::vector<PassNode*> compiledPasses;  // Non-owning pointers to reachable passes

    struct Barrier {
        eastl::string resourceName;
        vk::ImageMemoryBarrier2 imageBarrier;
        vk::BufferMemoryBarrier2 bufferBarrier;
        bool isImage;
    };
    //invalidate
    eastl::vector<eastl::vector<Barrier>> preBarriers;
    //flush
    eastl::vector<eastl::vector<Barrier>> postBarriers;

    bool built = false;
    bool compiled = false;

    // Resource usage tracking for forward-looking barrier generation
    struct ResourceUsageInfo {
        uint32_t passIndex;
        ResourceUsage usage;
        bool isWrite;
        vk::PipelineStageFlags2 stage;
        vk::AccessFlags2 access;
        vk::ImageLayout layout;
    };
    eastl::hash_map<eastl::string, eastl::vector<ResourceUsageInfo>> resourceUsageTable;

    void buildDependencyGraph();
    void pruneUnreachable();
    void computeLifetimes();
    void topologicalSortWithOptimization();  // Optimize pass execution order
    void allocatePhysicalResources(uint32_t frameIndex);
    void buildRenderingInfos();  // Build vk::RenderingInfo for each pass
    void generateBarriers();
    void insertPreBarriers(vk::CommandBuffer cmd, uint32_t passIndex);
    void insertPostBarriers(vk::CommandBuffer cmd, uint32_t passIndex);

    void buildResourceUsageTable();
    const ResourceUsageInfo* findNextUser(const eastl::string& resourceName, uint32_t currentPassIndex) const;

    // Optimization heuristics
    PassNode* selectOptimalPass(
        const eastl::vector<PassNode*>& readyQueue,
        PassNode* lastExecuted,
        const eastl::hash_map<eastl::string, uint32_t>& externalFirstUse,
        const eastl::hash_map<eastl::string, uint32_t>& externalLastUse
    );
    int countSharedResources(PassNode* a, PassNode* b);
    int calculateLayoutTransitions(PassNode* next, PassNode* prev);

    vk::ImageLayout getLayoutForUsage(ResourceUsage usage) const;
    vk::PipelineStageFlags2 getStageForUsage(ResourceUsage usage) const;
    vk::AccessFlags2 getAccessForUsage(ResourceUsage usage) const;

    ResourceHandle getOrCreateResource(const eastl::string& name);
};

} // namespace violet