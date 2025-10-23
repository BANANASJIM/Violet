#include "DebugRenderer.hpp"

#include "core/Log.hpp"
#include "core/FileSystem.hpp"
#include "resource/Material.hpp"
#include "resource/Mesh.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "renderer/vulkan/GraphicsPipeline.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/gpu/UniformBuffer.hpp"
#include "renderer/vulkan/DescriptorSet.hpp"
#include "renderer/ForwardRenderer.hpp"
#include "ecs/World.hpp"
#include "ecs/Components.hpp"
#include "resource/shader/ShaderLibrary.hpp"

namespace violet {

DebugRenderer::~DebugRenderer() {
    cleanup();
}

void DebugRenderer::init(VulkanContext* ctx, GlobalUniforms* globalUnif,
                         DescriptorManager* descMgr, ShaderLibrary* shaderLib, uint32_t framesInFlight) {
    context = ctx;
    maxFramesInFlight = framesInFlight;
    globalUniforms = globalUnif;

    // Create debug material
    debugMaterial = eastl::make_unique<Material>();
    debugMaterial->create(context);

    // Get shaders from ShaderLibrary
    auto debugVert = shaderLib->get("debug_vert");
    auto debugFrag = shaderLib->get("debug_frag");

    // Create debug pipeline with line topology
    debugPipeline = eastl::make_unique<GraphicsPipeline>();
    PipelineConfig config;
    config.topology = vk::PrimitiveTopology::eLineList;
    config.cullMode = vk::CullModeFlagBits::eNone;
    config.enableDepthTest = true;
    config.enableDepthWrite = false;
    config.enableBlending = true;
    config.globalDescriptorSetLayout = descMgr->getLayout("Global");  // Use DescriptorManager for layout

    // Add push constant range for model matrix (used in debug.vert)
    vk::PushConstantRange pushConstant;
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushConstant.offset = 0;
    pushConstant.size = 64;  // sizeof(mat4)
    config.pushConstantRanges.push_back(pushConstant);

    // Query available device features to determine what we can use
    vk::PhysicalDeviceFeatures availableFeatures = context->getPhysicalDevice().getFeatures();

    // For line topology, always use Fill mode. eLine mode is for wireframing triangles, not lines.
    config.polygonMode = vk::PolygonMode::eFill;

    // Use wider lines only if wideLines feature is supported
    if (availableFeatures.wideLines) {
        config.lineWidth = 2.0f;
    } else {
        config.lineWidth = 1.0f;  // Default line width
    }

    // Use new shader-based init API (dynamic rendering - no RenderPass needed)
    debugPipeline->init(context, descMgr, debugMaterial.get(), debugVert, debugFrag, config);

    // Create triangle pipeline for filled geometry (rays, mesh wireframes converted to lines)
    trianglePipeline = eastl::make_unique<GraphicsPipeline>();
    PipelineConfig triangleConfig = config;  // Copy base config (including globalDescriptorSetLayout)
    triangleConfig.topology = vk::PrimitiveTopology::eTriangleList;  // Use triangle list
    triangleConfig.polygonMode = vk::PolygonMode::eFill;  // Filled triangles
    triangleConfig.cullMode = vk::CullModeFlagBits::eNone;  // No culling for debug geometry

    trianglePipeline->init(context, descMgr, debugMaterial.get(), debugVert, debugFrag, triangleConfig);

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

    if (trianglePipeline) {
        trianglePipeline->cleanup();
        trianglePipeline.reset();
    }

    if (debugMaterial) {
        debugMaterial->cleanup();
        debugMaterial.reset();
    }
}

void DebugRenderer::render(vk::CommandBuffer commandBuffer, uint32_t frameIndex) {
    // Base render method - this would be called if DebugRenderer is used standalone
    // For now, this is empty as debug rendering is typically done through specific methods
    // like renderFrustum, renderAABBs, etc.
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

void DebugRenderer::generateSphereGeometry(const glm::vec3& center, float radius, const glm::vec3& color,
                                          eastl::vector<Vertex>& vertices, eastl::vector<uint32_t>& indices,
                                          uint32_t segments, uint32_t rings) {
    uint32_t baseVertexIndex = static_cast<uint32_t>(vertices.size());

    // Generate vertices using spherical coordinates
    // Latitude rings (horizontal circles) and longitude segments (vertical semicircles)
    for (uint32_t ring = 0; ring <= rings; ++ring) {
        float theta = static_cast<float>(ring) * glm::pi<float>() / static_cast<float>(rings);
        float sinTheta = glm::sin(theta);
        float cosTheta = glm::cos(theta);

        for (uint32_t seg = 0; seg <= segments; ++seg) {
            float phi = static_cast<float>(seg) * 2.0f * glm::pi<float>() / static_cast<float>(segments);
            float sinPhi = glm::sin(phi);
            float cosPhi = glm::cos(phi);

            // Spherical to Cartesian coordinates
            glm::vec3 pos;
            pos.x = center.x + radius * sinTheta * cosPhi;
            pos.y = center.y + radius * cosTheta;
            pos.z = center.z + radius * sinTheta * sinPhi;

            Vertex v;
            v.pos = pos;
            v.color = color;
            vertices.push_back(v);
        }
    }

    // Generate line indices for latitude rings (horizontal circles)
    for (uint32_t ring = 0; ring <= rings; ++ring) {
        for (uint32_t seg = 0; seg < segments; ++seg) {
            uint32_t current = baseVertexIndex + ring * (segments + 1) + seg;
            uint32_t next = baseVertexIndex + ring * (segments + 1) + (seg + 1);
            indices.push_back(current);
            indices.push_back(next);
        }
    }

    // Generate line indices for longitude segments (vertical semicircles)
    for (uint32_t seg = 0; seg <= segments; ++seg) {
        for (uint32_t ring = 0; ring < rings; ++ring) {
            uint32_t current = baseVertexIndex + ring * (segments + 1) + seg;
            uint32_t below = baseVertexIndex + (ring + 1) * (segments + 1) + seg;
            indices.push_back(current);
            indices.push_back(below);
        }
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

    // Use BaseRenderer's bindGlobalDescriptors helper
    vk::DescriptorSet globalSet = globalUniforms->getDescriptorSet()->getDescriptorSet(frameIndex);
    bindGlobalDescriptors(commandBuffer, debugPipeline->getPipelineLayout(), globalSet, 0);

    // Bind buffers
    vk::Buffer vertexBuffers[] = { frame.vertexBuffer->buffer };
    vk::DeviceSize offsets[] = { 0 };
    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    commandBuffer.bindIndexBuffer(frame.indexBuffer->buffer, 0, vk::IndexType::eUint32);

    // Use BaseRenderer's pushModelMatrix helper
    pushModelMatrix(commandBuffer, debugPipeline->getPipelineLayout(), glm::mat4(1.0f));

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

void DebugRenderer::renderSphere(vk::CommandBuffer commandBuffer, uint32_t frameIndex,
                                const glm::vec3& center, float radius, const glm::vec3& color) {
    if (!enabled || !debugPipeline) {
        return;
    }

    // Generate sphere geometry
    eastl::vector<Vertex> vertices;
    eastl::vector<uint32_t> indices;
    generateSphereGeometry(center, radius, color, vertices, indices);

    if (vertices.empty() || indices.empty()) {
        return;
    }

    // Check buffer size limits
    if (vertices.size() > MAX_DEBUG_VERTICES || indices.size() > MAX_DEBUG_INDICES) {
        violet::Log::warn("Renderer", "Debug sphere geometry exceeds buffer limits: {} vertices, {} indices",
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

    // Use BaseRenderer's bindGlobalDescriptors helper
    vk::DescriptorSet globalSet = globalUniforms->getDescriptorSet()->getDescriptorSet(frameIndex);
    bindGlobalDescriptors(commandBuffer, debugPipeline->getPipelineLayout(), globalSet, 0);

    // Bind vertex and index buffers
    vk::Buffer vertexBuffers[] = { frame.vertexBuffer->buffer };
    vk::DeviceSize offsets[] = { 0 };
    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    commandBuffer.bindIndexBuffer(frame.indexBuffer->buffer, 0, vk::IndexType::eUint32);

    // Use BaseRenderer's pushModelMatrix helper (identity matrix since sphere is already in world space)
    pushModelMatrix(commandBuffer, debugPipeline->getPipelineLayout(), glm::mat4(1.0f));

    // Draw
    commandBuffer.drawIndexed(frame.indexCount, 1, 0, 0, 0);
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
        // Only generate geometry for visible AABBs to reduce overdraw
        if (visibilityMask[i]) {
            uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
            glm::vec3 color = DebugColors::VISIBLE_AABB;
            generateAABBGeometry(aabbs[i], color, vertices, indices, baseVertex);
        }
    }

    if (vertices.empty() || indices.empty()) {
        return;
    }

    // Check buffer size limits
    if (vertices.size() > MAX_DEBUG_VERTICES || indices.size() > MAX_DEBUG_INDICES) {
        violet::Log::warn("Renderer", "Debug geometry exceeds buffer limits: {} vertices, {} indices",
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

    // Use BaseRenderer's bindGlobalDescriptors helper
    vk::DescriptorSet globalSet = globalUniforms->getDescriptorSet()->getDescriptorSet(frameIndex);
    bindGlobalDescriptors(commandBuffer, debugPipeline->getPipelineLayout(), globalSet, 0);

    // Bind vertex and index buffers
    vk::Buffer vertexBuffers[] = { frame.vertexBuffer->buffer };
    vk::DeviceSize offsets[] = { 0 };
    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    commandBuffer.bindIndexBuffer(frame.indexBuffer->buffer, 0, vk::IndexType::eUint32);

    // Use BaseRenderer's pushModelMatrix helper
    pushModelMatrix(commandBuffer, debugPipeline->getPipelineLayout(), glm::mat4(1.0f));

    // Draw
    commandBuffer.drawIndexed(frame.indexCount, 1, 0, 0, 0);

    // Log occasionally
    static uint32_t logCounter = 0;
    if (++logCounter % 300 == 0) {
        size_t visibleCount = 0;
        for (bool vis : visibilityMask) {
            if (vis) visibleCount++;
        }
        violet::Log::info("Renderer", "Debug AABB rendering: {} AABBs ({} visible, {} culled), {} indices",
                aabbs.size(), visibleCount, aabbs.size() - visibleCount, frame.indexCount);
    }
}

void DebugRenderer::renderRay(vk::CommandBuffer commandBuffer, uint32_t frameIndex, const glm::vec3& origin, const glm::vec3& direction, float length) {
    if (!enabled || !trianglePipeline) {
        return;
    }

    // Generate ray as a box/beam mesh
    eastl::vector<Vertex> vertices;
    eastl::vector<uint32_t> indices;

    // Create a box beam along the ray direction
    float width = 1.0f;  // Width of the ray beam

    // Calculate perpendicular vectors for the beam
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(direction, up)) > 0.99f) {
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    glm::vec3 right = glm::normalize(glm::cross(direction, up)) * width;
    glm::vec3 upVec = glm::normalize(glm::cross(right, direction)) * width;

    glm::vec3 endPoint = origin + direction * length;

    // Create 8 vertices for a box beam
    vertices.reserve(8);

    // Near face (at origin)
    Vertex v0; v0.pos = origin - right - upVec; v0.color = DebugColors::RAY; vertices.push_back(v0);
    Vertex v1; v1.pos = origin + right - upVec; v1.color = DebugColors::RAY; vertices.push_back(v1);
    Vertex v2; v2.pos = origin + right + upVec; v2.color = DebugColors::RAY; vertices.push_back(v2);
    Vertex v3; v3.pos = origin - right + upVec; v3.color = DebugColors::RAY; vertices.push_back(v3);

    // Far face (at end)
    Vertex v4; v4.pos = endPoint - right - upVec; v4.color = DebugColors::RAY; vertices.push_back(v4);
    Vertex v5; v5.pos = endPoint + right - upVec; v5.color = DebugColors::RAY; vertices.push_back(v5);
    Vertex v6; v6.pos = endPoint + right + upVec; v6.color = DebugColors::RAY; vertices.push_back(v6);
    Vertex v7; v7.pos = endPoint - right + upVec; v7.color = DebugColors::RAY; vertices.push_back(v7);

    // Create indices for triangle faces (12 triangles, 36 indices)
    indices.reserve(36);

    // Near face
    indices.push_back(0); indices.push_back(1); indices.push_back(2);
    indices.push_back(0); indices.push_back(2); indices.push_back(3);

    // Far face
    indices.push_back(4); indices.push_back(6); indices.push_back(5);
    indices.push_back(4); indices.push_back(7); indices.push_back(6);

    // Top face
    indices.push_back(3); indices.push_back(2); indices.push_back(6);
    indices.push_back(3); indices.push_back(6); indices.push_back(7);

    // Bottom face
    indices.push_back(0); indices.push_back(4); indices.push_back(5);
    indices.push_back(0); indices.push_back(5); indices.push_back(1);

    // Right face
    indices.push_back(1); indices.push_back(5); indices.push_back(6);
    indices.push_back(1); indices.push_back(6); indices.push_back(2);

    // Left face
    indices.push_back(0); indices.push_back(3); indices.push_back(7);
    indices.push_back(0); indices.push_back(7); indices.push_back(4);

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

    // Bind triangle pipeline for filled mesh rendering
    trianglePipeline->bind(commandBuffer);

    // Use BaseRenderer's bindGlobalDescriptors helper
    vk::DescriptorSet globalSet = globalUniforms->getDescriptorSet()->getDescriptorSet(frameIndex);
    bindGlobalDescriptors(commandBuffer, trianglePipeline->getPipelineLayout(), globalSet, 0);

    // Bind buffers
    vk::Buffer vertexBuffers[] = { frame.vertexBuffer->buffer };
    vk::DeviceSize offsets[] = { 0 };
    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    commandBuffer.bindIndexBuffer(frame.indexBuffer->buffer, 0, vk::IndexType::eUint32);

    // Use BaseRenderer's pushModelMatrix helper
    pushModelMatrix(commandBuffer, trianglePipeline->getPipelineLayout(), glm::mat4(1.0f));

    // Draw
    commandBuffer.drawIndexed(frame.indexCount, 1, 0, 0, 0);
}

void DebugRenderer::generateWireframeGeometry(const eastl::vector<Vertex>& meshVertices,
                                             const eastl::vector<uint32_t>& meshIndices,
                                             const glm::vec3& color,
                                             eastl::vector<Vertex>& outVertices,
                                             eastl::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    // Copy vertices and set color
    outVertices.reserve(meshVertices.size());
    for (const auto& vertex : meshVertices) {
        Vertex wireVertex = vertex;
        wireVertex.color = color;
        outVertices.push_back(wireVertex);
    }

    // Convert triangle indices to line indices
    outIndices.reserve(meshIndices.size() * 2); // Each triangle becomes 3 lines

    for (size_t i = 0; i + 2 < meshIndices.size(); i += 3) {
        uint32_t v0 = meshIndices[i];
        uint32_t v1 = meshIndices[i + 1];
        uint32_t v2 = meshIndices[i + 2];

        // Three edges of the triangle
        outIndices.push_back(v0);
        outIndices.push_back(v1);

        outIndices.push_back(v1);
        outIndices.push_back(v2);

        outIndices.push_back(v2);
        outIndices.push_back(v0);
    }
}

void DebugRenderer::setRayData(const glm::vec3& origin, const glm::vec3& direction, float length, bool enabled) {
    storedRayOrigin = origin;
    storedRayDirection = direction;
    storedRayLength = length;
    rayDataEnabled = enabled;
}

void DebugRenderer::clearRayData() {
    rayDataEnabled = false;
    storedRayOrigin = glm::vec3(0.0f);
    storedRayDirection = glm::vec3(0.0f);
    storedRayLength = 100.0f;
}

void DebugRenderer::beginRayBatch() {
    batchedRayVertices.clear();
    batchedRayIndices.clear();
}

void DebugRenderer::addRayToBatch(const glm::vec3& origin, const glm::vec3& direction, float length) {
    if (!enabled) {
        return;
    }

    // Get current vertex offset for indices
    uint32_t baseVertexIndex = static_cast<uint32_t>(batchedRayVertices.size());

    // Create a box beam along the ray direction
    float width = 1.0f;  // Width of the ray beam

    // Calculate perpendicular vectors for the beam
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(direction, up)) > 0.99f) {
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    glm::vec3 right = glm::normalize(glm::cross(direction, up)) * width;
    glm::vec3 upVec = glm::normalize(glm::cross(right, direction)) * width;

    glm::vec3 endPoint = origin + direction * length;

    // Add 8 vertices for this ray's box beam
    // Near face (at origin)
    batchedRayVertices.push_back({origin - right - upVec, {}, {}, DebugColors::RAY, {}});
    batchedRayVertices.push_back({origin + right - upVec, {}, {}, DebugColors::RAY, {}});
    batchedRayVertices.push_back({origin + right + upVec, {}, {}, DebugColors::RAY, {}});
    batchedRayVertices.push_back({origin - right + upVec, {}, {}, DebugColors::RAY, {}});

    // Far face (at end)
    batchedRayVertices.push_back({endPoint - right - upVec, {}, {}, DebugColors::RAY, {}});
    batchedRayVertices.push_back({endPoint + right - upVec, {}, {}, DebugColors::RAY, {}});
    batchedRayVertices.push_back({endPoint + right + upVec, {}, {}, DebugColors::RAY, {}});
    batchedRayVertices.push_back({endPoint - right + upVec, {}, {}, DebugColors::RAY, {}});

    // Add indices for triangle faces (12 triangles, 36 indices per ray)
    // Near face
    batchedRayIndices.push_back(baseVertexIndex + 0); batchedRayIndices.push_back(baseVertexIndex + 1); batchedRayIndices.push_back(baseVertexIndex + 2);
    batchedRayIndices.push_back(baseVertexIndex + 0); batchedRayIndices.push_back(baseVertexIndex + 2); batchedRayIndices.push_back(baseVertexIndex + 3);

    // Far face
    batchedRayIndices.push_back(baseVertexIndex + 4); batchedRayIndices.push_back(baseVertexIndex + 6); batchedRayIndices.push_back(baseVertexIndex + 5);
    batchedRayIndices.push_back(baseVertexIndex + 4); batchedRayIndices.push_back(baseVertexIndex + 7); batchedRayIndices.push_back(baseVertexIndex + 6);

    // Top face
    batchedRayIndices.push_back(baseVertexIndex + 3); batchedRayIndices.push_back(baseVertexIndex + 2); batchedRayIndices.push_back(baseVertexIndex + 6);
    batchedRayIndices.push_back(baseVertexIndex + 3); batchedRayIndices.push_back(baseVertexIndex + 6); batchedRayIndices.push_back(baseVertexIndex + 7);

    // Bottom face
    batchedRayIndices.push_back(baseVertexIndex + 0); batchedRayIndices.push_back(baseVertexIndex + 4); batchedRayIndices.push_back(baseVertexIndex + 5);
    batchedRayIndices.push_back(baseVertexIndex + 0); batchedRayIndices.push_back(baseVertexIndex + 5); batchedRayIndices.push_back(baseVertexIndex + 1);

    // Right face
    batchedRayIndices.push_back(baseVertexIndex + 1); batchedRayIndices.push_back(baseVertexIndex + 5); batchedRayIndices.push_back(baseVertexIndex + 6);
    batchedRayIndices.push_back(baseVertexIndex + 1); batchedRayIndices.push_back(baseVertexIndex + 6); batchedRayIndices.push_back(baseVertexIndex + 2);

    // Left face
    batchedRayIndices.push_back(baseVertexIndex + 0); batchedRayIndices.push_back(baseVertexIndex + 3); batchedRayIndices.push_back(baseVertexIndex + 7);
    batchedRayIndices.push_back(baseVertexIndex + 0); batchedRayIndices.push_back(baseVertexIndex + 7); batchedRayIndices.push_back(baseVertexIndex + 4);
}

void DebugRenderer::renderRayBatch(vk::CommandBuffer commandBuffer, uint32_t frameIndex) {
    if (!enabled || !trianglePipeline || batchedRayVertices.empty()) {
        return;
    }

    auto& frame = frameData[frameIndex];

    // Update vertex buffer with all batched rays
    if (frame.vertexBuffer && frame.vertexBuffer->mappedData) {
        size_t dataSize = batchedRayVertices.size() * sizeof(Vertex);
        if (dataSize <= frame.vertexBuffer->size) {
            memcpy(frame.vertexBuffer->mappedData, batchedRayVertices.data(), dataSize);
            frame.vertexCount = static_cast<uint32_t>(batchedRayVertices.size());
        }
    }

    // Update index buffer with all batched rays
    if (frame.indexBuffer && frame.indexBuffer->mappedData) {
        size_t dataSize = batchedRayIndices.size() * sizeof(uint32_t);
        if (dataSize <= frame.indexBuffer->size) {
            memcpy(frame.indexBuffer->mappedData, batchedRayIndices.data(), dataSize);
            frame.indexCount = static_cast<uint32_t>(batchedRayIndices.size());
        }
    }

    // Bind triangle pipeline for filled mesh rendering
    trianglePipeline->bind(commandBuffer);

    // Use BaseRenderer's bindGlobalDescriptors helper
    vk::DescriptorSet globalSet = globalUniforms->getDescriptorSet()->getDescriptorSet(frameIndex);
    bindGlobalDescriptors(commandBuffer, trianglePipeline->getPipelineLayout(), globalSet, 0);

    // Bind buffers
    vk::Buffer vertexBuffers[] = { frame.vertexBuffer->buffer };
    vk::DeviceSize offsets[] = { 0 };
    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    commandBuffer.bindIndexBuffer(frame.indexBuffer->buffer, 0, vk::IndexType::eUint32);

    // Use BaseRenderer's pushModelMatrix helper
    pushModelMatrix(commandBuffer, trianglePipeline->getPipelineLayout(), glm::mat4(1.0f));

    // Draw all batched rays in one call
    commandBuffer.drawIndexed(frame.indexCount, 1, 0, 0, 0);
}

void DebugRenderer::renderSelectedEntity(vk::CommandBuffer commandBuffer, uint32_t frameIndex,
                                        entt::registry& world, const ForwardRenderer& renderer) {
    if (!enabled || selectedEntity == entt::null) {
        return;
    }

    // Get entity components
    auto* transformComp = world.try_get<TransformComponent>(selectedEntity);
    auto* lightComp = world.try_get<LightComponent>(selectedEntity);

    // Render point light influence sphere if entity is a point light
    if (lightComp && lightComp->type == LightType::Point && transformComp) {
        renderSphere(commandBuffer, frameIndex, transformComp->world.position,
                    lightComp->radius, DebugColors::SELECTED_ENTITY);
    }
    // Note: Mesh wireframe rendering removed for lightweight implementation
    // Can be re-added later using generateWireframeGeometry() + debugPipeline if needed
}


} // namespace violet