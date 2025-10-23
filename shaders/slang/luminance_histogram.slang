// Luminance Histogram - Slang version
// Builds a 64-bin histogram of log2 luminance for robust auto-exposure
// Reference: https://alextardif.com/HistogramLuminance.html
//            https://bruop.github.io/exposure/

// ========================================
// Descriptors
// ========================================

// Input: HDR scene color
[[vk::binding(0, 0)]]
Texture2D hdrScene;

[[vk::binding(1, 0)]]
SamplerState texSampler;

// Output: Histogram buffer (64 bins)
struct HistogramBuffer {
    uint bins[64];           // Histogram bins (atomic counters)
    float minLogLuminance;   // Minimum log2 luminance in range (e.g., -4.0)
    float maxLogLuminance;   // Maximum log2 luminance in range (e.g., 12.0)
    uint pixelCount;         // Total pixels processed
};

[[vk::binding(2, 0)]]
RWStructuredBuffer<HistogramBuffer> histogram;

// Push constants for parameters
struct HistogramParams {
    float minLogLum;         // -4.0 (covers very dark scenes)
    float maxLogLum;         // 12.0 (covers very bright scenes)
    float centerWeightPower; // 2.0 (Gaussian-like center weighting, 0 = uniform)
    uint enabled;            // 1 = enabled, 0 = disabled
};

[[vk::push_constant]]
ConstantBuffer<HistogramParams> params;

// ========================================
// Shared Memory
// ========================================

// Shared memory for local histogram (reduce global atomics)
groupshared uint localHistogram[64];

// ========================================
// Helper Functions
// ========================================

// Rec. 709 luminance weights
float getLuminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Center-weighted metering: higher weight for screen center
// Uses distance-based falloff (similar to Gaussian)
float getCenterWeight(float2 uv) {
    if (params.centerWeightPower <= 0.0) {
        return 1.0; // Uniform weighting
    }

    // Distance from center [0, 1]
    float2 centerOffset = uv - 0.5;
    float dist = length(centerOffset) * 2.0; // Normalize to [0, 1]

    // Gaussian-like falloff: weight = exp(-k * dist^2)
    // Using power instead for performance: weight = (1 - dist)^power
    float weight = pow(max(0.0, 1.0 - dist), params.centerWeightPower);
    return weight;
}

// ========================================
// Compute Shader
// ========================================

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(
    uint3 globalId : SV_DispatchThreadID,
    uint3 localId : SV_GroupThreadID,
    uint3 groupId : SV_GroupID
) {
    uint localIdx = localId.y * 16 + localId.x;

    // Initialize local histogram
    if (localIdx < 64) {
        localHistogram[localIdx] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // Get texture size
    uint2 texSize;
    hdrScene.GetDimensions(texSize.x, texSize.y);

    uint2 pixelCoord = globalId.xy;

    // Check bounds
    if (pixelCoord.x >= texSize.x || pixelCoord.y >= texSize.y) {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5) / float2(texSize);

    // Sample HDR scene
    float3 color = hdrScene.SampleLevel(texSampler, uv, 0).rgb;

    // Calculate luminance
    float lum = getLuminance(color);

    // Skip very dark/invalid pixels
    if (lum < 0.001) {
        return;
    }

    // Log2 luminance for perceptual distribution
    float logLum = log2(lum);

    // Map to histogram bin [0, 63]
    float range = params.maxLogLum - params.minLogLum;
    float normalizedLogLum = (logLum - params.minLogLum) / range;
    int binIndex = int(normalizedLogLum * 64.0);
    binIndex = clamp(binIndex, 0, 63);

    // Center-weighted metering
    float weight = getCenterWeight(uv);
    uint weightedCount = uint(weight * 256.0); // Scale to integer for atomics

    if (weightedCount > 0) {
        // Accumulate in local histogram (shared memory)
        InterlockedAdd(localHistogram[binIndex], weightedCount);
    }

    GroupMemoryBarrierWithGroupSync();

    // First 64 threads write local histogram to global
    if (localIdx < 64) {
        if (localHistogram[localIdx] > 0) {
            InterlockedAdd(histogram[0].bins[localIdx], localHistogram[localIdx]);
        }
    }

    // First thread updates metadata
    if (localId.x == 0 && localId.y == 0) {
        uint workgroupPixels = 16 * 16;
        InterlockedAdd(histogram[0].pixelCount, workgroupPixels);
    }
}