// PBR.slang - Physically Based Rendering functions
// Cook-Torrance microfacet BRDF implementation

static const float PI = 3.14159265359;

// ========================================
// PBR BRDF Functions
// ========================================

// Normal Distribution Function - GGX/Trowbridge-Reitz
// D(h) = α² / (π((n·h)²(α²-1)+1)²)
float D_GGX(float dotNH, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PI * denom * denom);
}

// Geometric Shadowing - Schlick-Smith approximation
// G(l,v,h) = G_Schlick(n·l) · G_Schlick(n·v)
float G_SchlicksmithGGX(float dotNL, float dotNV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float GL = dotNL / (dotNL * (1.0 - k) + k);
    float GV = dotNV / (dotNV * (1.0 - k) + k);
    return GL * GV;
}

// Fresnel-Schlick approximation
// F(v,h) = F₀ + (1-F₀)(1-(v·h))⁵
float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Fresnel-Schlick with roughness term for IBL
// F(v,h) = F₀ + (max(1-roughness, F₀)-F₀)(1-(v·h))⁵
float3 F_SchlickR(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0, 1.0, 1.0) * (1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}