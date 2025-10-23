// Debug Shader - Slang version
// Simple colored geometry rendering for debugging

import Common;

#include "TypeDefinitions.slang"

// ========================================
// Descriptor Bindings
// ========================================

// Set 0: PER-FRAME (Dynamic Offset)
[[vk::binding(0, 0)]]
ConstantBuffer<CameraUBO> camera;
#define global camera

[[vk::binding(1, 0)]]
StructuredBuffer<LightData> lights;

[[vk::binding(2, 0)]]
StructuredBuffer<ShadowData> shadows;

// Set 1: BINDLESS (Static, UPDATE_AFTER_BIND)
[[vk::binding(0, 1)]]
Texture2D textures[1024];

[[vk::binding(1, 1)]]
TextureCube cubemaps[64];

[[vk::binding(2, 1)]]
SamplerState linearSampler;

[[vk::binding(3, 1)]]
SamplerState nearestSampler;

[[vk::binding(4, 1)]]
SamplerState shadowSampler;

// Set 2: PER-MATERIAL
[[vk::binding(0, 2)]]
StructuredBuffer<MaterialData> materials;

// Push Constants
[[vk::push_constant]]
ConstantBuffer<PushConstants> push;

// ========================================
// Vertex Shader
// ========================================

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 texCoord : TEXCOORD;
    [[vk::location(3)]] float3 color : COLOR;
    [[vk::location(4)]] float4 tangent : TANGENT;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 fragColor : COLOR;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;

    float4 worldPos = mul(push.model, float4(input.position, 1.0));
    float4 viewPos = mul(global.view, worldPos);
    output.position = mul(global.proj, viewPos);
    output.fragColor = input.color;

    return output;
}

// ========================================
// Fragment Shader
// ========================================

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    // Semi-transparent wireframe rendering
    return float4(input.fragColor, 0.7);
}