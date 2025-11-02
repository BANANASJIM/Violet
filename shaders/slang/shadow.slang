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
    // Transform to world space: position * model
    // Row-major layout: mul(vector, matrix)
    float4 worldPos = mul(float4(input.position, 1.0), push.model);

    // Transform to light clip space: worldPos * (view * proj)
    // push.lightSpaceMatrix already contains (view * proj) with Vulkan Y-flip applied
    return mul(worldPos, push.lightSpaceMatrix);
}