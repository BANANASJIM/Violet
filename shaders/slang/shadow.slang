// Shadow Shader - Slang version
// Depth-only rendering for shadow mapping

// Push constants for shadow rendering
struct ShadowPushConstants {
    column_major float4x4 lightSpaceMatrix;
    column_major float4x4 model;
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
    // Transform to world space: model * position
    // For column_major matrices, use mul(matrix, vector), NOT mul(vector, matrix)
    float4 worldPos = mul(push.model, float4(input.position, 1.0));

    // Transform to light clip space: (proj * view) * worldPos
    // push.lightSpaceMatrix already contains (proj * view) with Vulkan Y-flip applied
    return mul(push.lightSpaceMatrix, worldPos);
}