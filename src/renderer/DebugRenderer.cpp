#include "DebugRenderer.hpp"

#include <array>

#include "core/Log.hpp"
#include "renderer/Material.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/VulkanContext.hpp"
#include "renderer/Pipeline.hpp"
#include "renderer/RenderPass.hpp"
#include "renderer/ResourceFactory.hpp"
#include "renderer/UniformBuffer.hpp"
#include "renderer/DescriptorSet.hpp"
#include "renderer/Renderer.hpp"

namespace violet {

DebugRenderer::~DebugRenderer() {
    cleanup();
}

void DebugRenderer::init(VulkanContext* ctx, RenderPass* rp, GlobalUniforms* globalUnif, uint32_t framesInFlight) {
    context = ctx;
    renderPass = rp;
    globalUniforms = globalUnif;
    maxFramesInFlight = framesInFlight;

    // Create debug material
    debugMaterial = eastl::make_unique<Material>();
    debugMaterial->create(context, DescriptorSetType::UnlitMaterialTextures);

    // Create debug pipeline with line topology
    debugPipeline = eastl::make_unique<Pipeline>();
    PipelineConfig config;
    config.topology = vk::PrimitiveTopology::eLineList;
    config.polygonMode = vk::PolygonMode::eLine;
    config.cullMode = vk::CullModeFlagBits::eNone;
    config.lineWidth = 2.0f;
    config.enableDepthTest = true;
    config.enableDepthWrite = false;
    config.enableBlending = true;

    // Use global uniforms from the main renderer
    debugPipeline->init(context, renderPass, globalUniforms->getDescriptorSet(), debugMaterial.get(),
                       "build/shaders/debug.vert.spv", "build/shaders/debug.frag.spv", config);

    // Create per-frame buffers
    frameData.resize(maxFramesInFlight);
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        // Create dynamic vertex buffer
        BufferInfo vertexInfo{
            .size = sizeof(Vertex) * MAX_DEBUG_VERTICES,
            .usage = vk::BufferUsageFlagBits::eVertexBuffer,
            .memoryUsage = MemoryUsage::CPU_TO_GPU,
            .debugName = "Debug Vertex Buffer"
        };
        frameData[i].vertexBuffer = eastl::make_unique<BufferResource>(
            ResourceFactory::createBuffer(context, vertexInfo));

        // Create dynamic index buffer
        BufferInfo indexInfo{
            .size = sizeof(uint32_t) * MAX_DEBUG_INDICES,
            .usage = vk::BufferUsageFlagBits::eIndexBuffer,
            .memoryUsage = MemoryUsage::CPU_TO_GPU,
            .debugName = "Debug Index Buffer"
        };
        frameData[i].indexBuffer = eastl::make_unique<BufferResource>(
            ResourceFactory::createBuffer(context, indexInfo));
    }

    VT_INFO("Debug renderer initialized with {} frames in flight", maxFramesInFlight);
}

void DebugRenderer::cleanup() {
    // Explicitly clean up BufferResources before clearing frameData
    for (auto& frame : frameData) {
        if (frame.vertexBuffer) {
            ResourceFactory::destroyBuffer(context, *frame.vertexBuffer);
        }
        if (frame.indexBuffer) {
            ResourceFactory::destroyBuffer(context, *frame.indexBuffer);
        }
    }
    frameData.clear();

    if (debugPipeline) {
        debugPipeline->cleanup();
        debugPipeline.reset();
    }

    if (debugMaterial) {
        debugMaterial->cleanup();
        debugMaterial.reset();
    }
}

void DebugRenderer::generateFrustumGeometry(const Frustum& frustum, eastl::vector<Vertex>& vertices, eastl::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();

    // Calculate frustum corner points from planes
    // For simplicity, we'll create a visualization based on near and far planes
    // This is a simplified approach - in production, we'd extract corners from planes

    // For now, create a simple pyramid shape to represent the frustum
    // Center point (camera position approximation)
    Vertex center;
    center.pos = glm::vec3(0.0f, 0.0f, 0.0f);
    center.color = DebugColors::FRUSTUM;
    vertices.push_back(center);

    // Create 4 corner points for far plane (approximate)
    float extent = 10.0f; // Approximate extent for visualization
    eastl::array<glm::vec3, 4> corners = {{
        glm::vec3(-extent, -extent, -extent),
        glm::vec3( extent, -extent, -extent),
        glm::vec3( extent,  extent, -extent),
        glm::vec3(-extent,  extent, -extent)
    }};

    for (const auto& corner : corners) {
        Vertex v;
        v.pos = corner;
        v.color = DebugColors::FRUSTUM;
        vertices.push_back(v);
    }

    // Create lines from center to each corner
    indices.reserve(24); // 4 lines from center + 4 lines for far plane rectangle

    // Lines from center to corners
    for (uint32_t i = 1; i <= 4; ++i) {
        indices.push_back(0); // center
        indices.push_back(i); // corner
    }

    // Rectangle on far plane
    for (uint32_t i = 1; i <= 4; ++i) {
        indices.push_back(i);
        indices.push_back((i % 4) + 1);
    }
}

void DebugRenderer::generateAABBGeometry(const AABB& aabb, const glm::vec3& color, eastl::vector<Vertex>& vertices, eastl::vector<uint32_t>& indices, uint32_t baseVertexIndex) {
    // Generate 8 vertices for AABB corners
    eastl::array<glm::vec3, 8> corners = {{
        glm::vec3(aabb.min.x, aabb.min.y, aabb.min.z), // 0: min corner
        glm::vec3(aabb.max.x, aabb.min.y, aabb.min.z), // 1: +X
        glm::vec3(aabb.max.x, aabb.max.y, aabb.min.z), // 2: +X+Y
        glm::vec3(aabb.min.x, aabb.max.y, aabb.min.z), // 3: +Y
        glm::vec3(aabb.min.x, aabb.min.y, aabb.max.z), // 4: +Z
        glm::vec3(aabb.max.x, aabb.min.y, aabb.max.z), // 5: +X+Z
        glm::vec3(aabb.max.x, aabb.max.y, aabb.max.z), // 6: max corner
        glm::vec3(aabb.min.x, aabb.max.y, aabb.max.z), // 7: +Y+Z
    }};

    // Add vertices
    for (const auto& corner : corners) {
        Vertex v;
        v.pos = corner;
        v.color = color;
        vertices.push_back(v);
    }

    // Generate 12 lines for wireframe cube
    eastl::array<eastl::pair<uint32_t, uint32_t>, 12> edges = {{
        // Bottom face (Z = min)
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        // Top face (Z = max)
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        // Vertical edges
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    }};

    for (const auto& edge : edges) {
        indices.push_back(baseVertexIndex + edge.first);
        indices.push_back(baseVertexIndex + edge.second);
    }
}

void DebugRenderer::renderFrustum(vk::CommandBuffer commandBuffer, uint32_t frameIndex, const Frustum& frustum) {
    if (!enabled || !showFrustumDebug || !debugPipeline) {
        return;
    }

    eastl::vector<Vertex> vertices;
    eastl::vector<uint32_t> indices;
    generateFrustumGeometry(frustum, vertices, indices);

    if (vertices.empty() || indices.empty()) {
        return;
    }

    auto& frame = frameData[frameIndex];

    // Update buffers
    if (frame.vertexBuffer && frame.vertexBuffer->mappedData) {
        memcpy(frame.vertexBuffer->mappedData, vertices.data(),
               vertices.size() * sizeof(Vertex));
        frame.vertexCount = static_cast<uint32_t>(vertices.size());
    }

    if (frame.indexBuffer && frame.indexBuffer->mappedData) {
        memcpy(frame.indexBuffer->mappedData, indices.data(),
               indices.size() * sizeof(uint32_t));
        frame.indexCount = static_cast<uint32_t>(indices.size());
    }

    // Bind pipeline
    debugPipeline->bind(commandBuffer);

    // Bind global descriptor set
    vk::DescriptorSet globalSet = globalUniforms->getDescriptorSet()->getDescriptorSet(frameIndex);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        debugPipeline->getPipelineLayout(),
        0, 1, &globalSet, 0, nullptr
    );

    // Bind buffers
    vk::Buffer vertexBuffers[] = { frame.vertexBuffer->buffer };
    vk::DeviceSize offsets[] = { 0 };
    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    commandBuffer.bindIndexBuffer(frame.indexBuffer->buffer, 0, vk::IndexType::eUint32);

    // Push constants
    struct PushConstantData {
        glm::mat4 model;
    } pushData;
    pushData.model = glm::mat4(1.0f);

    commandBuffer.pushConstants(
        debugPipeline->getPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex,
        0, sizeof(PushConstantData), &pushData
    );

    // Draw
    commandBuffer.drawIndexed(frame.indexCount, 1, 0, 0, 0);
}

void DebugRenderer::renderAABB(vk::CommandBuffer commandBuffer, uint32_t frameIndex, const AABB& aabb, bool isVisible) {
    if (!enabled || !showAABBDebug) {
        return;
    }

    eastl::vector<AABB> aabbs = { aabb };
    eastl::vector<bool> visibility = { isVisible };
    renderAABBs(commandBuffer, frameIndex, aabbs, visibility);
}

void DebugRenderer::renderAABBs(vk::CommandBuffer commandBuffer, uint32_t frameIndex, const eastl::vector<AABB>& aabbs, const eastl::vector<bool>& visibilityMask) {
    if (!enabled || !showAABBDebug || aabbs.empty() || !debugPipeline) {
        return;
    }

    // Generate geometry for all AABBs
    eastl::vector<Vertex> vertices;
    eastl::vector<uint32_t> indices;
    vertices.reserve(aabbs.size() * 8);
    indices.reserve(aabbs.size() * 24);

    for (size_t i = 0; i < aabbs.size() && i < visibilityMask.size(); ++i) {
        uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
        glm::vec3 color = visibilityMask[i] ? DebugColors::VISIBLE_AABB : DebugColors::CULLED_AABB;
        generateAABBGeometry(aabbs[i], color, vertices, indices, baseVertex);
    }

    if (vertices.empty() || indices.empty()) {
        return;
    }

    // Check buffer size limits
    if (vertices.size() > MAX_DEBUG_VERTICES || indices.size() > MAX_DEBUG_INDICES) {
        VT_WARN("Debug geometry exceeds buffer limits: {} vertices, {} indices",
                vertices.size(), indices.size());
        return;
    }

    auto& frame = frameData[frameIndex];

    // Update vertex buffer
    if (frame.vertexBuffer && frame.vertexBuffer->mappedData) {
        memcpy(frame.vertexBuffer->mappedData, vertices.data(),
               vertices.size() * sizeof(Vertex));
        frame.vertexCount = static_cast<uint32_t>(vertices.size());
    }

    // Update index buffer
    if (frame.indexBuffer && frame.indexBuffer->mappedData) {
        memcpy(frame.indexBuffer->mappedData, indices.data(),
               indices.size() * sizeof(uint32_t));
        frame.indexCount = static_cast<uint32_t>(indices.size());
    }

    // Bind pipeline
    debugPipeline->bind(commandBuffer);

    // Bind global descriptor set (set 0)
    vk::DescriptorSet globalSet = globalUniforms->getDescriptorSet()->getDescriptorSet(frameIndex);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        debugPipeline->getPipelineLayout(),
        0,  // GLOBAL_SET
        1,
        &globalSet,
        0,
        nullptr
    );

    // Bind vertex and index buffers
    vk::Buffer vertexBuffers[] = { frame.vertexBuffer->buffer };
    vk::DeviceSize offsets[] = { 0 };
    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    commandBuffer.bindIndexBuffer(frame.indexBuffer->buffer, 0, vk::IndexType::eUint32);

    // Push model matrix (identity for debug rendering)
    struct PushConstantData {
        glm::mat4 model;
    } pushData;
    pushData.model = glm::mat4(1.0f);

    commandBuffer.pushConstants(
        debugPipeline->getPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex,
        0,
        sizeof(PushConstantData),
        &pushData
    );

    // Draw
    commandBuffer.drawIndexed(frame.indexCount, 1, 0, 0, 0);

    // Log occasionally
    static uint32_t logCounter = 0;
    if (++logCounter % 300 == 0) {
        size_t visibleCount = 0;
        for (bool vis : visibilityMask) {
            if (vis) visibleCount++;
        }
        VT_INFO("Debug AABB rendering: {} AABBs ({} visible, {} culled), {} indices",
                aabbs.size(), visibleCount, aabbs.size() - visibleCount, frame.indexCount);
    }
}

} // namespace violet