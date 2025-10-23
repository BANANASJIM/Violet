// Prefilter Environment - Slang version
// Generates mip chain for specular IBL using GGX importance sampling

import Sampling;
import PBR;

// ========================================
// Descriptors
// ========================================

[[vk::binding(0, 0)]]
TextureCube environmentMap;

[[vk::binding(1, 0)]]
RWTexture2DArray<float4> prefilteredMap;

[[vk::binding(2, 0)]]
SamplerState texSampler;

// Push constants
struct PushConstants {
    uint outputSize;
    uint currentFace;
    float roughness;
    uint padding;
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

    // Check bounds
    if (pixelCoord.x >= pc.outputSize || pixelCoord.y >= pc.outputSize) {
        return;
    }

    // Calculate UV for this pixel
    float2 uv = (float2(pixelCoord) + 0.5) / float(pc.outputSize);

    // Get 3D direction for this cubemap pixel
    float3 N = uvToDirection(uv, pc.currentFace);
    float3 R = N;
    float3 V = R;

    // Prefilter convolution using importance sampling
    const uint SAMPLE_COUNT = 1024;
    float totalWeight = 0.0;
    float3 prefilteredColor = float3(0.0, 0.0, 0.0);

    for (uint i = 0; i < SAMPLE_COUNT; ++i) {
        // Generate sample direction using importance sampling
        float2 Xi = hammersley(i, SAMPLE_COUNT);
        float3 H = importanceSampleGGX(Xi, N, pc.roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            // Sample environment map at lower mip levels for higher roughness
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float D = D_GGX(NdotH, pc.roughness);
            float pdf = (D * NdotH / (4.0 * HdotV)) + 0.0001;

            // Calculate mip level based on PDF
            float resolution = 512.0; // Source environment map resolution
            float saTexel = 4.0 * PI / (6.0 * resolution * resolution);
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);

            // Biased mip level for better result
            float mipLevel = pc.roughness == 0.0 ? 0.0 : max(0.5 * log2(saSample / saTexel) + 1.0, 0.0);

            prefilteredColor += environmentMap.SampleLevel(texSampler, L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor = prefilteredColor / totalWeight;

    // Write to prefiltered map
    prefilteredMap[uint3(pixelCoord, pc.currentFace)] = float4(prefilteredColor, 1.0);
}