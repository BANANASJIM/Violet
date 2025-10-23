// Simple test shader without imports to validate reflection

// Test push constants
struct PushConstants {
    float4x4 model;
    uint materialID;
    uint padding[3];
};

[[vk::push_constant]]
PushConstants push;

// Test descriptor set 0
struct GlobalUBO {
    float4x4 view;
    float4x4 proj;
    float3 cameraPos;
    float time;
};

[[vk::binding(0, 0)]]
ConstantBuffer<GlobalUBO> global;

[[vk::binding(1, 0)]]
Sampler2D testTexture;

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
    float4 viewPos = mul(global.view, worldPos);
    output.position = mul(global.proj, viewPos);
    output.texCoord = input.texCoord;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    float4 color = testTexture.Sample(input.texCoord);
    return color;
}