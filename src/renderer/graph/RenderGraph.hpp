#pragma once

#include <vulkan/vulkan.hpp>
#include <EASTL/vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/functional.h>
#include <EASTL/algorithm.h>

namespace violet {

// Forward declarations
class Pass;
class VulkanContext;
struct ImageResource;
struct BufferResource;

/**
 * @brief Logical resource handle for render graph
 *
 * This is just an ID that references a logical resource in the graph.
 * The actual GPU resource is managed externally.
 */
using ResourceHandle = uint32_t;
constexpr ResourceHandle InvalidResource = 0;

/**
 * @brief Resource type for logical tracking
 */
enum class ResourceType {
    Texture2D,
    TextureCube,
    Buffer,
    Unknown
};

/**
 * @brief Resource usage hints for barrier generation
 */
enum class ResourceUsage {
    ColorAttachment,      // Written as color attachment
    DepthAttachment,      // Written as depth/stencil attachment
    ShaderRead,          // Read in shader (texture sampling)
    ShaderWrite,         // Written in shader (storage image/buffer)
    TransferSrc,         // Transfer source
    TransferDst,         // Transfer destination
    Present              // Present to swapchain
};

/**
 * @brief Logical resource info (decoupled from actual GPU resources)
 */
struct LogicalResource {
    eastl::string name;
    ResourceType type = ResourceType::Unknown;

    // Pointer to external GPU resource (not owned)
    union {
        const ImageResource* imageResource;
        const BufferResource* bufferResource;
        void* externalHandle;  // For custom resources
    };

    // For barrier generation (tracked state)
    struct State {
        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
        vk::PipelineStageFlags stage = vk::PipelineStageFlagBits::eTopOfPipe;
        vk::AccessFlags access = {};
    } currentState;

    // Constructor
    LogicalResource() : externalHandle(nullptr) {}
};

/**
 * @brief Pass node in the render graph
 */
struct PassNode {
    eastl::string name;

    // Resources accessed by this pass
    struct ResourceAccess {
        ResourceHandle handle;
        ResourceUsage usage;
    };
    eastl::vector<ResourceAccess> reads;
    eastl::vector<ResourceAccess> writes;

    // Execution callback
    eastl::function<void(vk::CommandBuffer, uint32_t)> execute;

    // Optional wrapped Pass object
    Pass* wrappedPass = nullptr;
};

/**
 * @brief Lightweight render graph for pass scheduling and automatic barriers
 *
 * Design principles:
 * - Does NOT create or own GPU resources
 * - Only tracks logical dependencies
 * - Generates barriers automatically
 * - Minimal API surface
 */
class RenderGraph {
public:
    RenderGraph() = default;
    ~RenderGraph() = default;

    // Delete copy operations
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // === Resource Declaration ===

    /**
     * @brief Import an external texture resource
     * @param name Logical name
     * @param imageRes External image resource (not owned)
     * @return Handle for referencing in passes
     */
    ResourceHandle importTexture(const eastl::string& name, const ImageResource* imageRes);

    /**
     * @brief Import an external buffer resource
     * @param name Logical name
     * @param bufferRes External buffer resource (not owned)
     * @return Handle for referencing in passes
     */
    ResourceHandle importBuffer(const eastl::string& name, const BufferResource* bufferRes);

    /**
     * @brief Declare a transient resource (created by graph)
     * @param name Logical name
     * @param type Resource type
     * @return Handle for referencing in passes
     *
     * Note: In this minimal version, transient resources are not implemented.
     * This is a placeholder for future extension.
     */
    ResourceHandle createTransient(const eastl::string& name, ResourceType type);

    // === Pass Declaration ===

    /**
     * @brief Begin declaring a new pass
     * @param name Pass name
     * @return Pass builder interface
     */
    class PassBuilder {
    public:
        PassBuilder(RenderGraph* graph, const eastl::string& name);

        // Declare resource access
        PassBuilder& read(ResourceHandle resource, ResourceUsage usage = ResourceUsage::ShaderRead);
        PassBuilder& write(ResourceHandle resource, ResourceUsage usage = ResourceUsage::ColorAttachment);

        // Set execution callback
        PassBuilder& execute(eastl::function<void(vk::CommandBuffer, uint32_t)> callback);

        // Finish building
        void build();

    private:
        RenderGraph* graph;
        PassNode node;
    };

    PassBuilder addPass(const eastl::string& name);

    // === Compilation and Execution ===

    /**
     * @brief Compile the graph
     * Analyzes dependencies and generates barriers
     */
    void compile();

    /**
     * @brief Execute the graph
     * @param cmd Command buffer to record into
     * @param frameIndex Current frame index
     */
    void execute(vk::CommandBuffer cmd, uint32_t frameIndex);

    /**
     * @brief Reset the graph for rebuilding
     */
    void clear();

    // === Debug ===

    /**
     * @brief Print graph structure for debugging
     */
    void debugPrint() const;

    /**
     * @brief Get resource by handle
     */
    const LogicalResource* getResource(ResourceHandle handle) const;

private:
    // Resource registry
    eastl::hash_map<ResourceHandle, LogicalResource> resources;
    eastl::hash_map<eastl::string, ResourceHandle> resourceNames;
    uint32_t nextHandle = 1;

    // Pass nodes
    eastl::vector<PassNode> passes;

    // Compiled barriers
    struct ResourceBarrier {
        size_t passIndex;           // Insert before this pass
        ResourceHandle resource;     // Resource being transitioned

        // Barrier data
        vk::ImageMemoryBarrier imageBarrier;
        vk::BufferMemoryBarrier bufferBarrier;
        bool isImage;
    };
    eastl::vector<ResourceBarrier> barriers;

    // Compilation state
    bool compiled = false;

    // Helper methods
    void generateBarriers();
    void insertBarriers(vk::CommandBuffer cmd, size_t passIndex);

    // State transition helpers
    vk::ImageLayout getLayoutForUsage(ResourceUsage usage) const;
    vk::PipelineStageFlags getStageForUsage(ResourceUsage usage) const;
    vk::AccessFlags getAccessForUsage(ResourceUsage usage) const;

    // Get Vulkan handles from resources
    vk::Image getImageHandle(const LogicalResource& res) const;
    vk::Buffer getBufferHandle(const LogicalResource& res) const;
};

// === Inline Implementation ===

inline RenderGraph::PassBuilder RenderGraph::addPass(const eastl::string& name) {
    return PassBuilder(this, name);
}

inline RenderGraph::PassBuilder::PassBuilder(RenderGraph* g, const eastl::string& n)
    : graph(g) {
    node.name = n;
}

inline RenderGraph::PassBuilder& RenderGraph::PassBuilder::read(ResourceHandle resource, ResourceUsage usage) {
    node.reads.push_back({resource, usage});
    return *this;
}

inline RenderGraph::PassBuilder& RenderGraph::PassBuilder::write(ResourceHandle resource, ResourceUsage usage) {
    node.writes.push_back({resource, usage});
    return *this;
}

inline RenderGraph::PassBuilder& RenderGraph::PassBuilder::execute(eastl::function<void(vk::CommandBuffer, uint32_t)> callback) {
    node.execute = callback;
    return *this;
}

inline void RenderGraph::PassBuilder::build() {
    graph->passes.push_back(node);
}

} // namespace violet