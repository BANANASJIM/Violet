#version 450
#extension GL_EXT_nonuniform_qualifier : enable

// Set 0: Global UBO (camera + lights)
layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float padding0;

    // Light data
    vec4 lightPositions[8];
    vec4 lightColors[8];
    vec4 lightParams[8];
    int numLights;
    vec3 ambientLight;

    // Skybox data
    float skyboxExposure;
    float skyboxRotation;
    int skyboxEnabled;
    float padding1;

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

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

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

    vec3 albedo = pow(baseColor.rgb, vec3(2.2)); // Convert from sRGB to linear

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
        vec3 lightColor = global.lightColors[i].xyz;

        if (lightType < 0.5) {
            // Directional light (type == 0)
            L = -normalize(global.lightPositions[i].xyz);
            radiance = lightColor;
        } else {
            // Point light (type == 1)
            vec3 lightPos = global.lightPositions[i].xyz;
            L = normalize(lightPos - fragPos);
            float distance = length(lightPos - fragPos);

            // Check if within light radius
            float radius = global.lightColors[i].w;
            if (distance > radius) {
                continue;
            }

            // Attenuation
            float linear = global.lightParams[i].x;
            float quadratic = global.lightParams[i].y;
            float attenuation = 1.0 / (1.0 + linear * distance + quadratic * distance * distance);

            // Soft cutoff at radius boundary
            float falloff = clamp(1.0 - (distance / radius), 0.0, 1.0);
            attenuation *= falloff;

            radiance = lightColor * attenuation;
        }

        // Calculate light contribution
        vec3 H = normalize(V + L);

        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // Image-Based Lighting (IBL)
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    vec3 ambient = vec3(0.0);

    // Check if IBL textures are available
    if (global.irradianceMapIndex != 0 && global.prefilteredMapIndex != 0 && global.brdfLUTIndex != 0) {
        // Diffuse IBL - irradiance
        vec3 irradiance = texture(cubemaps[nonuniformEXT(global.irradianceMapIndex)], N).rgb;
        vec3 diffuse = irradiance * albedo;

        // Specular IBL - prefiltered environment map + BRDF LUT
        vec3 R = reflect(-V, N);
        const float MAX_REFLECTION_LOD = 4.0;
        vec3 prefilteredColor = textureLod(cubemaps[nonuniformEXT(global.prefilteredMapIndex)], R, roughness * MAX_REFLECTION_LOD).rgb;

        // Sample BRDF LUT with NdotV and roughness
        vec2 envBRDF = texture(textures[nonuniformEXT(global.brdfLUTIndex)], vec2(max(dot(N, V), 0.0), roughness)).rg;
        vec3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

        // Combine diffuse and specular IBL
        ambient = (kD * diffuse + specular) * mix(1.0, occlusion, materials.data[push.materialID].occlusionStrength);
    } else {
        // Fallback to simple ambient if IBL not available
        ambient = global.ambientLight * albedo * mix(1.0, occlusion, materials.data[push.materialID].occlusionStrength);
    }

    vec3 color = ambient + Lo + emissive;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, baseColor.a);
}