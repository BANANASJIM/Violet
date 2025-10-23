// Equirectangular to Cubemap - Slang version
// Converts HDR equirectangular map to cubemap format

import Sampling;

// ========================================
// Descriptors
// ========================================

[[vk::binding(0, 0)]]
Texture2D equirectangularMap;

[[vk::binding(1, 0)]]
RWTexture2DArray<float4> outputCubemap;

[[vk::binding(2, 0)]]
SamplerState texSampler;

// Push constants
struct PushConstants {
    uint cubemapSize;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

// ========================================
// Compute Shader
// ========================================

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 globalId : SV_DispatchThreadID) {
    uint2 pixelCoord = globalId.xy;
    uint currentFace = globalId.z;

    // Check bounds
    if (pixelCoord.x >= pc.cubemapSize || pixelCoord.y >= pc.cubemapSize || currentFace >= 6) {
        return;
    }

    // Calculate UV for this pixel (center of pixel)
    float2 uv = (float2(pixelCoord) + 0.5) / float(pc.cubemapSize);

    // Get 3D direction for this cubemap pixel
    float3 direction = uvToDirection(uv, currentFace);

    // Convert direction to equirectangular UV
    float2 equirectUV = directionToEquirectUV(direction);

    // Sample from equirectangular map
    float4 color = equirectangularMap.SampleLevel(texSampler, equirectUV, 0);

    // Write to cubemap face
    outputCubemap[uint3(pixelCoord, currentFace)] = color;
}