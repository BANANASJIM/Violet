// Shadow Shader - Slang version
// Depth-only rendering for shadow mapping

// Push constants for shadow rendering
struct ShadowPushConstants {
    float4x4 lightSpaceMatrix;
    float4x4 model;
};

[[vk::push_constant]]
ConstantBuffer<ShadowPushConstants> push;

// ========================================
// Vertex Shader
// ========================================

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
};

[shader("vertex")]
float4 vertexMain(VSInput input) : SV_Position {
    return mul(push.lightSpaceMatrix, mul(push.model, float4(input.position, 1.0)));
}