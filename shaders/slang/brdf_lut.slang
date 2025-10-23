// BRDF LUT Generation - Slang version
// Generates 2D lookup table for split-sum approximation of specular IBL

import Sampling;

// ========================================
// Descriptors
// ========================================

[[vk::binding(0, 0)]]
RWTexture2D<float2> brdfLUT;

// Push constants
struct PushConstants {
    uint lutSize;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

// ========================================
// Helper Functions
// ========================================

// Random number generator for reducing banding artifacts
float random(float2 co) {
    float a = 12.9898;
    float b = 78.233;
    float c = 43758.5453;
    float dt = dot(co, float2(a, b));
    float sn = fmod(dt, 3.14);
    return frac(sin(sn) * c);
}

// GGX importance sampling with random perturbation to reduce banding
float3 importanceSampleGGX_Perturbed(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;

    // Add random perturbation to reduce banding artifacts
    float phi = 2.0 * PI * Xi.x + random(N.xz) * 0.1;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Spherical to cartesian (tangent space)
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Tangent to world space
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// Geometry function (Schlick-GGX for IBL)
// Note: k formula is different for IBL vs direct lighting
float GeometrySchlickGGX_IBL(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0;  // IBL-specific k

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith_IBL(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX_IBL(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX_IBL(NdotL, roughness);

    return ggx1 * ggx2;
}

// Integrate BRDF for given NdotV and roughness
// Returns (scale, bias) for split-sum: F0 * scale + bias
float2 integrateBRDF(float NdotV, float roughness) {
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    float3 N = float3(0.0, 0.0, 1.0);

    const uint SAMPLE_COUNT = 1024;
    for (uint i = 0; i < SAMPLE_COUNT; ++i) {
        float2 Xi = hammersley(i, SAMPLE_COUNT);
        float3 H = importanceSampleGGX_Perturbed(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            float G = GeometrySmith_IBL(N, V, L, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);

    return float2(A, B);
}

// ========================================
// Compute Shader
// ========================================

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 globalId : SV_DispatchThreadID) {
    uint2 pixelCoord = globalId.xy;

    // Check bounds
    if (pixelCoord.x >= pc.lutSize || pixelCoord.y >= pc.lutSize) {
        return;
    }

    // Map pixel to [0, 1] range
    // X axis: NdotV (cosTheta)
    // Y axis: roughness
    float2 uv = (float2(pixelCoord) + 0.5) / float(pc.lutSize);
    float NdotV = uv.x;
    float roughness = uv.y;

    // Integrate BRDF
    float2 brdf = integrateBRDF(NdotV, roughness);

    // Store result (RG format: scale, bias)
    brdfLUT[pixelCoord] = brdf;
}