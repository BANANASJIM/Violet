#version 450
#extension GL_EXT_nonuniform_qualifier : enable

// ========================================
// Physically Based Rendering - Bindless
// ========================================
// Cook-Torrance microfacet BRDF with image-based lighting
// Reference: https://github.com/SaschaWillems/Vulkan (pbribl example)
//            http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// ========================================

// Set 0: Global UBO (camera + lights)
layout(set = 0, binding = 0) uniform GlobalUBO {
mat4 view;
mat4 proj;
vec3 cameraPos;
float padding0;

// Light data (physical units: lux for directional, lumens for point)
vec4 lightPositions[8];  // xyz=position/direction, w=type (0=dir, 1=point)
vec4 lightColors[8];     // xyz=color*intensity (physical units), w=radius
int numLights;
vec3 ambientLight;

// Skybox data
float skyboxExposure;
float skyboxRotation;
int skyboxEnabled;
float iblIntensity;

// IBL bindless texture indices
uint environmentMapIndex;
uint irradianceMapIndex;
uint prefilteredMapIndex;
uint brdfLUTIndex;
} global;

// Set 1: Bindless texture array
layout(set = 1, binding = 0) uniform sampler2D textures[];
layout(set = 1, binding = 1) uniform samplerCube cubemaps[];  // Cubemaps use binding 1

// Set 2: Materials SSBO (must match DescriptorManager::MaterialData exactly)
struct MaterialData {
// Material parameters
vec4 baseColorFactor;
float metallicFactor;
float roughnessFactor;
float normalScale;
float occlusionStrength;
vec3 emissiveFactor;
float alphaCutoff;

// Texture indices (index into bindless textures[])
uint baseColorTexIndex;
uint metallicRoughnessTexIndex;
uint normalTexIndex;
uint occlusionTexIndex;
uint emissiveTexIndex;
uint padding[3];  // Align to 16 bytes
};

layout(set = 2, binding = 0) readonly buffer MaterialDataBuffer {
MaterialData data[];
} materials;

// Push constants
layout(push_constant) uniform PushConstants {
mat4 model;
uint materialID;  // Index into materials[] SSBO
} push;

// Inputs from vertex shader
layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;
layout(location = 5) in vec3 fragViewPos;

// Output
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// ========================================
// Helper Functions
// ========================================

vec3 getNormalFromMap() {
uint normalIdx = materials.data[push.materialID].normalTexIndex;
if (normalIdx == 0) {
    return normalize(fragNormal);
}

vec3 tangentNormal = texture(textures[nonuniformEXT(normalIdx)], fragTexCoord).xyz * 2.0 - 1.0;
tangentNormal.xy *= materials.data[push.materialID].normalScale;

vec3 N = normalize(fragNormal);
vec3 T = normalize(fragTangent);
vec3 B = normalize(fragBitangent);
mat3 TBN = mat3(T, B, N);

return normalize(TBN * tangentNormal);
}

// ========================================
// PBR Functions (Cook-Torrance BRDF)
// ========================================

// Normal Distribution Function - GGX/Trowbridge-Reitz
// D(h) = α² / (π((n·h)²(α²-1)+1)²)
// Describes the distribution of microfacet normals
float D_GGX(float dotNH, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PI * denom * denom);
}

// Geometric Shadowing - Schlick-Smith approximation for direct lighting
// G(l,v,h) = G_Schlick(n·l) · G_Schlick(n·v)
// G_Schlick(n·v) = (n·v) / ((n·v)(1-k)+k)
// where k = (roughness+1)²/8 for direct lighting
float G_SchlicksmithGGX(float dotNL, float dotNV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float GL = dotNL / (dotNL * (1.0 - k) + k);
    float GV = dotNV / (dotNV * (1.0 - k) + k);
    return GL * GV;
}

// Fresnel-Schlick approximation
// F(v,h) = F₀ + (1-F₀)(1-(v·h))⁵
// Describes reflectance depending on viewing angle
vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Fresnel-Schlick approximation with roughness term for IBL
// F(v,h) = F₀ + (max(1-roughness, F₀)-F₀)(1-(v·h))⁵
vec3 F_SchlickR(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// Rotate vector around Y-axis by given angle (in radians)
vec3 rotateY(vec3 v, float angle) {
float c = cos(angle);
float s = sin(angle);
return vec3(
    c * v.x + s * v.z,
    v.y,
    -s * v.x + c * v.z
);
}

// ========================================
// Main Fragment Shader
// ========================================

void main() {
// Sample textures using bindless indices
uint baseColorIdx = materials.data[push.materialID].baseColorTexIndex;
vec4 baseColor = (baseColorIdx != 0)
    ? texture(textures[nonuniformEXT(baseColorIdx)], fragTexCoord)
    : vec4(1.0);
baseColor *= materials.data[push.materialID].baseColorFactor;

uint metallicRoughnessIdx = materials.data[push.materialID].metallicRoughnessTexIndex;
vec3 metallicRoughnessMap = (metallicRoughnessIdx != 0)
    ? texture(textures[nonuniformEXT(metallicRoughnessIdx)], fragTexCoord).rgb
    : vec3(1.0);

// glTF standard: R=unused, G=roughness, B=metallic
float metallic = metallicRoughnessMap.b * materials.data[push.materialID].metallicFactor;
float roughness = metallicRoughnessMap.g * materials.data[push.materialID].roughnessFactor;

uint occlusionIdx = materials.data[push.materialID].occlusionTexIndex;
float occlusion = (occlusionIdx != 0)
    ? texture(textures[nonuniformEXT(occlusionIdx)], fragTexCoord).r
    : 1.0;

uint emissiveIdx = materials.data[push.materialID].emissiveTexIndex;
vec3 emissive = (emissiveIdx != 0)
    ? texture(textures[nonuniformEXT(emissiveIdx)], fragTexCoord).rgb
    : vec3(0.0);
emissive *= materials.data[push.materialID].emissiveFactor;

// Alpha test
if (baseColor.a < materials.data[push.materialID].alphaCutoff) {
    discard;
}

vec3 albedo = baseColor.rgb; // Already linear (sRGB textures auto-converted by Vulkan)

// Get normal from normal map
vec3 N = getNormalFromMap();
vec3 V = normalize(global.cameraPos - fragPos); // View direction in world space

// Calculate F0 (surface reflection at zero incidence)
vec3 F0 = vec3(0.04);
F0 = mix(F0, albedo, metallic);

// Reflectance equation - accumulate lighting from all lights
vec3 Lo = vec3(0.0);

// Process each light
for (int i = 0; i < global.numLights; i++) {
    vec3 L;
    vec3 radiance;

    float lightType = global.lightPositions[i].w;
    vec3 lightColorIntensity = global.lightColors[i].xyz;  // color * physical intensity

    if (lightType < 0.5) {
        // Directional light (type == 0)
        // lightColorIntensity = color * illuminance (lux)
        L = -normalize(global.lightPositions[i].xyz);
        radiance = lightColorIntensity;
    } else {
        // Point light (type == 1)
        // Physical point light attenuation: Φ / (4π * r²)
        // where Φ is luminous power in lumens
        vec3 lightPos = global.lightPositions[i].xyz;
        L = normalize(lightPos - fragPos);
        float distance = length(lightPos - fragPos);

        // Check if within light radius (for early culling)
        float radius = global.lightColors[i].w;
        if (distance > radius) {
            continue;
        }

        // Inverse square law: illuminance = luminousPower / (4π * distance²)
        // lightColorIntensity = color * luminousPower (lumens)
        float illuminance = 1.0 / (4.0 * PI * distance * distance);

        // Windowing function for smooth falloff at radius boundary (UE4/Frostbite method)
        // Avoids hard cutoff and maintains energy conservation
        float distanceRatio = distance / radius;
        float windowTerm = pow(clamp(1.0 - pow(distanceRatio, 4.0), 0.0, 1.0), 2.0);

        radiance = lightColorIntensity * illuminance * windowTerm;
    }

    // Cook-Torrance BRDF:
    // f(l,v) = kD·(c/π) + kS·(D·F·G)/(4(n·l)(n·v))
    // where kD = (1-F)(1-metallic), kS = F
    vec3 H = normalize(V + L);
    float dotNH = max(dot(N, H), 0.0);
    float dotNV = max(dot(N, V), 0.0);
    float dotNL = max(dot(N, L), 0.0);

    if (dotNL > 0.0) {
        // D = Normal distribution (microfacet distribution)
        float D = D_GGX(dotNH, roughness);
        // G = Geometric shadowing (microfacet shadowing)
        float G = G_SchlicksmithGGX(dotNL, dotNV, roughness);
        // F = Fresnel (reflectance at angle)
        vec3 F = F_Schlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        vec3 specular = D * F * G / (4.0 * dotNL * dotNV + 0.001);

        Lo += (kD * albedo / PI + specular) * radiance * dotNL;
    }
}

// ===== Image-Based Lighting (IBL) =====
// Split-sum approximation (Epic Games):
// ∫ Li(l)·BRDF(l,v)·(n·l) dl ≈
//   ∫ Li(l)·(n·l) dl · ∫ BRDF(l,v)·(n·l) dl
// = prefiltered_color · (F·scale + bias)
vec3 F = F_SchlickR(max(dot(N, V), 0.0), F0, roughness);
vec3 kS = F;
vec3 kD = (1.0 - kS) * (1.0 - metallic);

vec3 ambient = vec3(0.0);

// Check if IBL textures are available
if (global.irradianceMapIndex != 0 && global.prefilteredMapIndex != 0 && global.brdfLUTIndex != 0) {
    // Apply environment rotation to sampling directions
    vec3 N_rotated = rotateY(N, global.skyboxRotation);
    vec3 R = reflect(-V, N);
    vec3 R_rotated = rotateY(R, global.skyboxRotation);

    // --- Diffuse IBL: Irradiance convolution ---
    // Precomputed: ∫ Li(l)·(n·l) dl
    vec3 irradiance = texture(cubemaps[nonuniformEXT(global.irradianceMapIndex)], N_rotated).rgb;
    vec3 diffuse = irradiance * albedo;

    // --- Specular IBL: Prefiltered environment + BRDF LUT ---
    // Prefiltered map: ∫ Li(l)·D(l,h)·(n·l) dl for varying roughness
    float maxLod = float(textureQueryLevels(cubemaps[nonuniformEXT(global.prefilteredMapIndex)]) - 1);
    vec3 prefilteredColor = textureLod(cubemaps[nonuniformEXT(global.prefilteredMapIndex)], R_rotated, roughness * maxLod).rgb;

    // BRDF LUT: ∫ F·G·(v·h)/(n·h)(n·v) d(h)
    // Returns (scale, bias) for: F·scale + bias
    vec2 envBRDF = texture(textures[nonuniformEXT(global.brdfLUTIndex)], vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    // Combine diffuse and specular contributions
    // Apply occlusion only to diffuse (ambient occlusion affects indirect diffuse)
    float ao = mix(1.0, occlusion, materials.data[push.materialID].occlusionStrength);
    ambient = (kD * diffuse * ao + specular) * global.iblIntensity;
} else {
    // Fallback to simple ambient if IBL not available
    ambient = global.ambientLight * albedo * mix(1.0, occlusion, materials.data[push.materialID].occlusionStrength);
}

vec3 color = ambient + Lo + emissive;

// Output HDR linear color (tone mapping and gamma correction handled in post-process)
outColor = vec4(color, baseColor.a);
}