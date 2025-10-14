#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/functional.h>
#include "ResourceHandle.hpp"

namespace violet {

class VulkanContext;
class TransientPool;
struct ImageResource;
struct BufferResource;

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
    vk::PipelineStageFlags stage = vk::PipelineStageFlagBits::eTopOfPipe;
    vk::AccessFlags access = {};
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
    eastl::string name;

    struct ResourceAccess {
        eastl::string resourceName;  // Reference to LogicalResource
        ResourceUsage usage;
        bool isWrite;
        // Removed: ImageDesc and BufferDesc (stored in LogicalResource)
    };

    eastl::vector<ResourceAccess> accesses;
    eastl::function<void(vk::CommandBuffer, uint32_t)> executeCallback;

    bool reachable = false;
    uint32_t passIndex = 0;

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
    ResourceHandle importImage(const eastl::string& name, const ImageResource* imageRes);
    ResourceHandle importBuffer(const eastl::string& name, const BufferResource* bufferRes);

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
        PassNode& node;  // Reference to node already in RenderGraph::passes
    };

    PassBuilder addPass(const eastl::string& name);

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
    eastl::vector<PassNode> passes;
    eastl::vector<PassNode> compiledPasses;

    struct Barrier {
        eastl::string resourceName;
        vk::ImageMemoryBarrier imageBarrier;
        vk::BufferMemoryBarrier bufferBarrier;
        vk::PipelineStageFlags srcStage;
        vk::PipelineStageFlags dstStage;
        bool isImage;
    };
    //invalidate
    eastl::vector<eastl::vector<Barrier>> preBarriers;
    //flush
    eastl::vector<eastl::vector<Barrier>> postBarriers;

    bool built = false;
    bool compiled = false;

    void buildDependencyGraph();
    void pruneUnreachable();
    void computeLifetimes();
    void allocatePhysicalResources();
    void buildRenderingInfos();  // Build vk::RenderingInfo for each pass
    void generateBarriers();
    void insertPreBarriers(vk::CommandBuffer cmd, uint32_t passIndex);
    void insertPostBarriers(vk::CommandBuffer cmd, uint32_t passIndex);

    vk::ImageLayout getLayoutForUsage(ResourceUsage usage) const;
    vk::PipelineStageFlags getStageForUsage(ResourceUsage usage) const;
    vk::AccessFlags getAccessForUsage(ResourceUsage usage) const;

    ResourceHandle getOrCreateResource(const eastl::string& name);
};

} // namespace violet