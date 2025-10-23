// Debug Shader - Slang version
// Simple colored geometry rendering for debugging

import Types;
import Bindless;

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