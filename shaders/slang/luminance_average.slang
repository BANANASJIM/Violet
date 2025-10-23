// Luminance Average - Slang version
// Compute average scene luminance for auto-exposure
// Simplified approach: downsample and compute average in single workgroup
// Reference: https://bruop.github.io/exposure/

// ========================================
// Descriptors
// ========================================

// Input: HDR scene color
[[vk::binding(0, 0)]]
Texture2D hdrScene;

[[vk::binding(1, 0)]]
SamplerState texSampler;

// Output: Luminance statistics buffer
struct LuminanceBuffer {
    float avgLogLuminance;  // Average log luminance (output)
    float minLuminance;     // Minimum luminance (reserved)
    float maxLuminance;     // Maximum luminance (reserved)
    uint sampleCount;       // Number of workgroups
};

[[vk::binding(2, 0)]]
RWStructuredBuffer<LuminanceBuffer> luminance;

// ========================================
// Shared Memory
// ========================================

// Shared memory for reduction
groupshared float sharedLogLum[256];

// ========================================
// Helper Functions
// ========================================

// Rec. 709 luminance weights
float getLuminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// ========================================
// Compute Shader
// ========================================

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 localId : SV_GroupThreadID) {
    uint localIdx = localId.y * 16 + localId.x;

    // Map to grid position (divide scene into 16x16 grid)
    float2 gridPos = float2(localId.xy);
    float2 uv = (gridPos + 0.5) / 16.0;  // Normalize to [0,1]

    // Sample HDR scene
    float3 color = hdrScene.SampleLevel(texSampler, uv, 0).rgb;

    // Calculate luminance
    float lum = getLuminance(color);

    // Log luminance for perceptual averaging
    float logLum = log2(clamp(lum, 0.001, 65504.0));

    // Store in shared memory
    sharedLogLum[localIdx] = logLum;

    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction
    for (uint stride = 128; stride > 0; stride >>= 1) {
        if (localIdx < stride) {
            sharedLogLum[localIdx] += sharedLogLum[localIdx + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // First thread writes result
    if (localIdx == 0) {
        // Average of 256 samples
        float avgLog = sharedLogLum[0] / 256.0;

        // Only one workgroup, so directly write result
        luminance[0].avgLogLuminance = avgLog;
        luminance[0].sampleCount = 1;
    }
}