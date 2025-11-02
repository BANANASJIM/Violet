// Simple test shader to verify reflection and auto-registration
// IMPORTANT: All matrices use row-major layout (Slang default, matches GLM_FORCE_ROW_MAJOR)
// Matrix multiplication order: mul(vector, matrix), e.g., mul(pos, model)

// Test descriptor set
[[vk::binding(0, 0)]]
ConstantBuffer<float4x4> testTransform;

[[vk::binding(1, 0)]]
Sampler2D testTexture;

// Test push constant
struct TestPushConstants {
    float4x4 model;
    uint materialID;
};

[[vk::push_constant]]
TestPushConstants push;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 texCoord : TEXCOORD;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texCoord : TEXCOORD;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;
    // Row-major layout: mul(vector, matrix)
    float4 worldPos = mul(float4(input.position, 1.0), push.model);
    output.position = mul(worldPos, testTransform);
    output.texCoord = input.texCoord;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    return testTexture.Sample(input.texCoord) * float4(1, 1, 1, 1);
}