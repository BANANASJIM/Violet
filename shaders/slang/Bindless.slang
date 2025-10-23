// Bindless.slang - 3-set descriptor layout with bindless resources

import Types;

// ========================================
// Set 0: PER-FRAME (Dynamic Offset)
// ========================================

// Binding 0: Camera data (with dynamic offset for triple buffering)
[[vk::binding(0, 0)]]
ConstantBuffer<CameraUBO> camera;

// Binding 1: Lighting data (SSBO array, count in camera.lightCount)
[[vk::binding(1, 0)]]
StructuredBuffer<LightData> lights;

// Binding 2: Shadow data
[[vk::binding(2, 0)]]
StructuredBuffer<ShadowData> shadows;

// ========================================
// Set 1: BINDLESS (Static, UPDATE_AFTER_BIND)
// ========================================

// Binding 0: 2D texture array
[[vk::binding(0, 1)]]
[[bindless]]
Texture2D textures[1024];

// Binding 1: Cubemap array
[[vk::binding(1, 1)]]
[[bindless]]
TextureCube cubemaps[64];

// Binding 2-4: Common samplers (immutable, shared across all materials)
[[vk::binding(2, 1)]]
SamplerState linearSampler;

[[vk::binding(3, 1)]]
SamplerState nearestSampler;

[[vk::binding(4, 1)]]
SamplerState shadowSampler;

// ========================================
// Set 2: PER-MATERIAL
// ========================================

// Binding 0: Material data buffer
[[vk::binding(0, 2)]]
StructuredBuffer<MaterialData> materials;

// ========================================
// Push Constants
// ========================================

[[vk::push_constant]]
ConstantBuffer<PushConstants> push;