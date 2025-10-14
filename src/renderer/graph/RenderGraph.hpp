#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/functional.h>

namespace violet {

class VulkanContext;
class TransientPool;
struct ImageResource;
struct BufferResource;

using ResourceHandle = uint32_t;
constexpr ResourceHandle InvalidResource = 0;

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

    LogicalResource() : physicalHandle(nullptr) {}
};

struct PassNode {
    eastl::string name;

    struct ResourceAccess {
        eastl::string name;
        ResourceUsage usage;
        ImageDesc imageDesc;
        BufferDesc bufferDesc;
        bool isWrite;
    };

    eastl::vector<ResourceAccess> accesses;
    eastl::function<void(vk::CommandBuffer, uint32_t)> executeCallback;

    bool reachable = false;
    uint32_t passIndex = 0;
};

class RenderGraph {
public:
    RenderGraph() = default;
    ~RenderGraph() = default;

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    void init(VulkanContext* ctx);
    void cleanup();

    ResourceHandle importImage(const eastl::string& name, const ImageResource* imageRes);
    ResourceHandle importBuffer(const eastl::string& name, const BufferResource* bufferRes);
    ResourceHandle declareImage(const eastl::string& name, const ImageDesc& desc, bool persistent);

    class PassBuilder {
    public:
        PassBuilder(RenderGraph* graph, const eastl::string& name);

        PassBuilder& read(const eastl::string& name, ResourceUsage usage = ResourceUsage::ShaderRead);
        PassBuilder& write(const eastl::string& name, const ImageDesc& desc, ResourceUsage usage = ResourceUsage::ColorAttachment);
        PassBuilder& execute(eastl::function<void(vk::CommandBuffer, uint32_t)> callback);

        void build();

    private:
        RenderGraph* graph;
        PassNode node;
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
    void generateBarriers();
    void insertPreBarriers(vk::CommandBuffer cmd, uint32_t passIndex);
    void insertPostBarriers(vk::CommandBuffer cmd, uint32_t passIndex);

    vk::ImageLayout getLayoutForUsage(ResourceUsage usage) const;
    vk::PipelineStageFlags getStageForUsage(ResourceUsage usage) const;
    vk::AccessFlags getAccessForUsage(ResourceUsage usage) const;

    ResourceHandle getOrCreateResource(const eastl::string& name);
};

} // namespace violet