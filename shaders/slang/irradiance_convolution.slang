// Irradiance Convolution - Slang version
// Convolves environment map to generate diffuse irradiance cubemap

import Sampling;

// ========================================
// Descriptors
// ========================================

[[vk::binding(0, 0)]]
TextureCube environmentMap;

[[vk::binding(1, 0)]]
RWTexture2DArray<float4> irradianceMap;

[[vk::binding(2, 0)]]
SamplerState texSampler;

// Push constants
struct PushConstants {
    uint irradianceSize;
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
    if (pixelCoord.x >= pc.irradianceSize || pixelCoord.y >= pc.irradianceSize || currentFace >= 6) {
        return;
    }

    // Calculate UV for this pixel (center of pixel)
    float2 uv = (float2(pixelCoord) + 0.5) / float(pc.irradianceSize);

    // Get 3D normal direction for this cubemap pixel
    float3 N = uvToDirection(uv, currentFace);

    // Build tangent space
    float3 T, B;
    buildOrthonormalBasis(N, T, B);

    // Convolve environment map using importance sampling
    float3 irradiance = float3(0.0, 0.0, 0.0);

    // Number of samples for Monte Carlo integration
    const uint SAMPLE_COUNT = 1024;

    // Monte Carlo integration with cosine-weighted importance sampling
    for (uint i = 0; i < SAMPLE_COUNT; ++i) {
        // Generate low-discrepancy sample
        float2 u = hammersley(i, SAMPLE_COUNT);

        // Generate cosine-weighted hemisphere sample
        float3 tangentSample = cosineSampleHemisphere(u);

        // Transform from tangent space to world space
        float3 sampleDir = tangentSample.x * T + tangentSample.y * B + tangentSample.z * N;

        // Sample incoming radiance from environment map
        float3 Li = environmentMap.SampleLevel(texSampler, sampleDir, 0).rgb;

        // Accumulate contribution
        irradiance += Li;
    }

    // Monte Carlo estimator: multiply by PI and divide by sample count
    irradiance = (PI * irradiance) / float(SAMPLE_COUNT);

    // Write to irradiance map
    irradianceMap[uint3(pixelCoord, currentFace)] = float4(irradiance, 1.0);
}