// Simple test shader without imports to validate reflection
// IMPORTANT: All matrices use row-major layout (Slang default, matches GLM_FORCE_ROW_MAJOR)
// Matrix multiplication order: mul(vector, matrix), e.g., mul(pos, model)

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

    // Row-major layout: mul(vector, matrix)
    // Transformation order: pos * model * view * proj
    float4 worldPos = mul(float4(input.position, 1.0), push.model);
    float4 viewPos = mul(worldPos, global.view);
    output.position = mul(viewPos, global.proj);
    output.texCoord = input.texCoord;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    float4 color = testTexture.Sample(input.texCoord);
    return color;
}