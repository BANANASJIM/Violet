// Sampling.slang - Common sampling and conversion utilities

static const float PI = 3.14159265359;

// ========================================
// Cubemap Direction Conversions
// ========================================

// Convert cubemap UV coordinates to 3D direction vector
float3 uvToDirection(float2 uv, uint face) {
    // Map UV from [0, 1] to [-1, 1]
    float2 ndc = uv * 2.0 - 1.0;

    float3 dir;
    switch (face) {
        case 0: dir = float3( 1.0, -ndc.y, -ndc.x); break; // +X
        case 1: dir = float3(-1.0, -ndc.y,  ndc.x); break; // -X
        case 2: dir = float3( ndc.x,  1.0,  ndc.y); break; // +Y
        case 3: dir = float3( ndc.x, -1.0, -ndc.y); break; // -Y
        case 4: dir = float3( ndc.x, -ndc.y,  1.0); break; // +Z
        case 5: dir = float3(-ndc.x, -ndc.y, -1.0); break; // -Z
        default: dir = float3(0.0, 0.0, 1.0); break;
    }

    return normalize(dir);
}

// Convert 3D direction to equirectangular UV coordinates
float2 directionToEquirectUV(float3 dir) {
    // Calculate spherical coordinates
    float theta = atan2(dir.z, dir.x);
    float phi = acos(clamp(dir.y, -1.0, 1.0));

    // Convert to UV coordinates [0, 1]
    float u = (theta + PI) / (2.0 * PI);
    float v = 1.0 - phi / PI;

    return float2(u, v);
}

// ========================================
// Low-Discrepancy Sequences
// ========================================

// Van der Corput radical inverse (base 2)
float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

// Hammersley low-discrepancy sequence
float2 hammersley(uint i, uint N) {
    return float2(float(i) / float(N), radicalInverse_VdC(i));
}

// ========================================
// Importance Sampling
// ========================================

// Cosine-weighted hemisphere sampling (Malley's method)
float3 cosineSampleHemisphere(float2 u) {
    float r = sqrt(u.x);
    float phi = 2.0 * PI * u.y;

    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0, 1.0 - u.x));

    return float3(x, y, z);
}

// GGX importance sampling for specular IBL
float3 importanceSampleGGX(float2 u, float3 N, float roughness) {
    float a = roughness * roughness;

    float phi = 2.0 * PI * u.x;
    float cosTheta = sqrt((1.0 - u.y) / (1.0 + (a * a - 1.0) * u.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Spherical to Cartesian
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Build TBN
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    // Transform to world space
    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// ========================================
// Orthonormal Basis Construction
// ========================================

// Build orthonormal basis from normal (Frisvad method)
void buildOrthonormalBasis(float3 n, out float3 tangent, out float3 bitangent) {
    if (n.z < -0.9999999) {
        tangent = float3(0.0, -1.0, 0.0);
        bitangent = float3(-1.0, 0.0, 0.0);
        return;
    }
    float a = 1.0 / (1.0 + n.z);
    float b = -n.x * n.y * a;
    tangent = float3(1.0 - n.x * n.x * a, b, -n.x);
    bitangent = float3(b, 1.0 - n.y * n.y * a, -n.y);
}