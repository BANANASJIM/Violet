// Simple test shader to verify reflection and auto-registration

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
    float4 worldPos = mul(push.model, float4(input.position, 1.0));
    output.position = mul(testTransform, worldPos);
    output.texCoord = input.texCoord;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    return testTexture.Sample(input.texCoord) * float4(1, 1, 1, 1);
}