#version 450

// Global UBO (matches vertex shader)
layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
} global;

// Material UBO
layout(set = 1, binding = 0) uniform MaterialData {
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    vec3 emissiveFactor;
    float alphaCutoff;
} material;

// Textures
layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 1, binding = 3) uniform sampler2D normalTexture;
layout(set = 1, binding = 4) uniform sampler2D occlusionTexture;
layout(set = 1, binding = 5) uniform sampler2D emissiveTexture;

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;
layout(location = 5) in vec3 fragViewPos;

layout(location = 0) out vec4 outColor;

const vec3 lightPos = vec3(5.0, 5.0, 5.0); // World space light position
const vec3 lightColor = vec3(50.0, 45.0, 40.0); // Much brighter light for visibility
const vec3 ambientColor = vec3(0.8); // Much brighter ambient for testing

const float PI = 3.14159265359;

vec3 getNormalFromMap() {
    vec3 tangentNormal = texture(normalTexture, fragTexCoord).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalScale;

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

void main() {
    // Sample textures
    vec4 baseColor = texture(baseColorTexture, fragTexCoord) * material.baseColorFactor;
    vec3 metallicRoughnessMap = texture(metallicRoughnessTexture, fragTexCoord).rgb;
    // glTF standard: R=unused, G=roughness, B=metallic
    float metallic = metallicRoughnessMap.b * material.metallicFactor;
    float roughness = metallicRoughnessMap.g * material.roughnessFactor;
    float occlusion = texture(occlusionTexture, fragTexCoord).r;
    vec3 emissive = texture(emissiveTexture, fragTexCoord).rgb * material.emissiveFactor;

    // Alpha test
    if (baseColor.a < material.alphaCutoff) {
        discard;
    }

    vec3 albedo = pow(baseColor.rgb, vec2(2.2).xxx); // Convert from sRGB to linear

    // Get normal from normal map
    vec3 N = getNormalFromMap();
    vec3 V = normalize(global.cameraPos - fragPos); // View direction in world space

    // Calculate F0 (surface reflection at zero incidence)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // Reflectance equation
    vec3 Lo = vec3(0.0);

    // Calculate light contribution (use world space coordinates for light position)
    vec3 L = normalize(lightPos - fragPos); // Light vector in world space
    vec3 H = normalize(V + L);
    float distance = length(lightPos - fragPos);
    float attenuation = 1.0 / (distance * distance);
    vec3 radiance = lightColor * attenuation;

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

    // Ambient with occlusion
    vec3 ambient = ambientColor * albedo * mix(1.0, occlusion, material.occlusionStrength);

    vec3 color = ambient + Lo + emissive;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, baseColor.a);
}