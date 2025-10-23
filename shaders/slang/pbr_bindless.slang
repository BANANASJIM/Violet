// Physically Based Rendering - Bindless (Slang)
// Cook-Torrance microfacet BRDF with image-based lighting

import Common;
import PBR;
import Sampling;

#include "TypeDefinitions.slang"

// ========================================
// Descriptor Bindings
// ========================================

// Set 0: PER-FRAME (Dynamic Offset)
[[vk::binding(0, 0)]]
ConstantBuffer<CameraUBO> camera;
#define global camera

[[vk::binding(1, 0)]]
StructuredBuffer<LightData> lights;

[[vk::binding(2, 0)]]
StructuredBuffer<ShadowData> shadows;

// Set 1: BINDLESS (Static, UPDATE_AFTER_BIND)
[[vk::binding(0, 1)]]
Texture2D textures[1024];

[[vk::binding(1, 1)]]
TextureCube cubemaps[64];

[[vk::binding(2, 1)]]
SamplerState linearSampler;

[[vk::binding(3, 1)]]
SamplerState nearestSampler;

[[vk::binding(4, 1)]]
SamplerState shadowSampler;

// Set 2: PER-MATERIAL
[[vk::binding(0, 2)]]
StructuredBuffer<MaterialData> materials;

// Push Constants
[[vk::push_constant]]
ConstantBuffer<PushConstants> push;

// ========================================
// Vertex Shader
// ========================================

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 texCoord : TEXCOORD;
    [[vk::location(3)]] float3 color : COLOR;
    [[vk::location(4)]] float4 tangent : TANGENT;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 fragPos : POSITION0;
    [[vk::location(1)]] float3 fragNormal : NORMAL;
    [[vk::location(2)]] float2 fragTexCoord : TEXCOORD;
    [[vk::location(3)]] float3 fragTangent : TANGENT;
    [[vk::location(4)]] float3 fragBitangent : BITANGENT;
    [[vk::location(5)]] float3 fragViewPos : POSITION1;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;

    float4 worldPos = mul(push.model, float4(input.position, 1.0));
    output.fragPos = worldPos.xyz;

    // Calculate view space position
    float4 viewPos = mul(global.view, worldPos);
    output.fragViewPos = viewPos.xyz;

    // Transform normal, tangent, bitangent to world space
    float3x3 normalMatrix = transpose((float3x3)push.model);
    output.fragNormal = normalize(mul(normalMatrix, input.normal));
    output.fragTangent = normalize(mul(normalMatrix, input.tangent.xyz));
    output.fragBitangent = cross(output.fragNormal, output.fragTangent) * input.tangent.w;

    output.fragTexCoord = input.texCoord;
    output.position = mul(global.proj, viewPos);

    return output;
}

// ========================================
// Fragment Shader Helper Functions
// ========================================

float3 getNormalFromMap(float2 texCoord, MaterialData material, float3 fragNormal, float3 fragTangent, float3 fragBitangent) {
    uint normalIdx = material.normalTexIndex;
    if (normalIdx == 0) {
        return normalize(fragNormal);
    }

    float3 tangentNormal = textures[NonUniformResourceIndex(normalIdx)].Sample(linearSampler, texCoord).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalScale;

    float3 N = normalize(fragNormal);
    float3 T = normalize(fragTangent);
    float3 B = normalize(fragBitangent);
    float3x3 TBN = float3x3(T, B, N);

    return normalize(mul(TBN, tangentNormal));
}

float3 rotateY(float3 v, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return float3(
        c * v.x + s * v.z,
        v.y,
        -s * v.x + c * v.z
    );
}

// Shadow functions
int selectCascade(float viewDepth, float4 cascadeSplits, uint cascadeCount) {
    for (int i = 0; i < int(cascadeCount) - 1; i++) {
        if (viewDepth < cascadeSplits[i]) {
            return i;
        }
    }
    return int(cascadeCount) - 1;
}

float sampleShadowCascade(ShadowData shadow, int cascadeIndex, float3 fragPos, float3 normal, float3 lightDir) {
    float4x4 lightSpaceMatrix = shadow.cascadeViewProjMatrices[cascadeIndex];
    float4 lightSpacePos = mul(lightSpaceMatrix, float4(fragPos, 1.0));
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }

    float bias = max(shadow.shadowParams.y * (1.0 - dot(normal, lightDir)), shadow.shadowParams.x);
    float currentDepth = projCoords.z - bias;

    float4 atlasRect = shadow.atlasRects[cascadeIndex];
    float2 atlasUV = atlasRect.xy + projCoords.xy * atlasRect.zw;

    // PCF
    float shadowValue = 0.0;
    float2 texelSize = 1.0 / 4096.0;
    int pcfSamples = 2;

    for(int x = -pcfSamples; x <= pcfSamples; ++x) {
        for(int y = -pcfSamples; y <= pcfSamples; ++y) {
            float2 offset = float2(x, y) * texelSize;
            float pcfDepth = textures[NonUniformResourceIndex(shadow.atlasIndex)].Sample(linearSampler, atlasUV + offset).r;
            shadowValue += currentDepth > pcfDepth ? 0.0 : 1.0;
        }
    }

    int totalSamples = (pcfSamples * 2 + 1) * (pcfSamples * 2 + 1);
    shadowValue /= float(totalSamples);

    return shadowValue;
}

float calculateShadow(int shadowIndex, float3 fragPos, float3 fragViewPos, float3 normal, float3 lightDir) {
    if (global.shadowsEnabled == 0 || shadowIndex < 0) {
        return 1.0;
    }

    ShadowData shadow = shadows[shadowIndex];

    if (shadow.lightType == 0 && shadow.cascadeCount > 1) {
        float viewDepth = abs(fragViewPos.z);
        int cascadeIndex = selectCascade(viewDepth, shadow.cascadeSplitDepths, shadow.cascadeCount);
        float shadowValue = sampleShadowCascade(shadow, cascadeIndex, fragPos, normal, lightDir);

        // Cascade blending
        float blendRange = shadow.shadowParams.z;
        if (blendRange > 0.0 && cascadeIndex < int(shadow.cascadeCount) - 1) {
            float nextSplit = shadow.cascadeSplitDepths[cascadeIndex];
            float prevSplit = (cascadeIndex > 0) ? shadow.cascadeSplitDepths[cascadeIndex - 1] : 0.0;
            float splitRange = nextSplit - prevSplit;
            float blendDist = viewDepth - (nextSplit - splitRange * blendRange);

            if (blendDist > 0.0) {
                float nextShadow = sampleShadowCascade(shadow, cascadeIndex + 1, fragPos, normal, lightDir);
                float blendFactor = saturate(blendDist / (splitRange * blendRange));
                shadowValue = lerp(shadowValue, nextShadow, blendFactor);
            }
        }

        return shadowValue;
    } else {
        return sampleShadowCascade(shadow, 0, fragPos, normal, lightDir);
    }
}

// ========================================
// Main Fragment Shader
// ========================================

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    MaterialData material = materials[push.materialID];

    // Sample material textures
    uint baseColorIdx = material.baseColorTexIndex;
    float4 baseColor = (baseColorIdx != 0)
        ? textures[NonUniformResourceIndex(baseColorIdx)].Sample(linearSampler, input.fragTexCoord)
        : float4(1.0, 1.0, 1.0, 1.0);
    baseColor *= material.baseColorFactor;

    uint metallicRoughnessIdx = material.metallicRoughnessTexIndex;
    float3 metallicRoughnessMap = (metallicRoughnessIdx != 0)
        ? textures[NonUniformResourceIndex(metallicRoughnessIdx)].Sample(linearSampler, input.fragTexCoord).rgb
        : float3(1.0, 1.0, 1.0);

    float metallic = metallicRoughnessMap.b * material.metallicFactor;
    float roughness = metallicRoughnessMap.g * material.roughnessFactor;

    uint occlusionIdx = material.occlusionTexIndex;
    float occlusion = (occlusionIdx != 0)
        ? textures[NonUniformResourceIndex(occlusionIdx)].Sample(linearSampler, input.fragTexCoord).r
        : 1.0;

    uint emissiveIdx = material.emissiveTexIndex;
    float3 emissive = (emissiveIdx != 0)
        ? textures[NonUniformResourceIndex(emissiveIdx)].Sample(linearSampler, input.fragTexCoord).rgb
        : float3(0.0, 0.0, 0.0);
    emissive *= material.emissiveFactor;

    if (baseColor.a < material.alphaCutoff) {
        discard;
    }

    float3 albedo = baseColor.rgb;
    float3 N = getNormalFromMap(input.fragTexCoord, material, input.fragNormal, input.fragTangent, input.fragBitangent);
    float3 V = normalize(global.cameraPos - input.fragPos);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // Direct lighting
    float3 Lo = float3(0.0, 0.0, 0.0);

    for (uint i = 0; i < uint(global.numLights); i++) {
        LightData light = lights[i];

        float3 L;
        float3 radiance;

        if (light.positionAndType.w < 0.5) {
            // Directional light
            L = -normalize(light.positionAndType.xyz);
            radiance = light.colorAndRadius.xyz;
        } else {
            // Point light
            float3 lightPos = light.positionAndType.xyz;
            L = normalize(lightPos - input.fragPos);
            float distance = length(lightPos - input.fragPos);
            float radius = light.colorAndRadius.w;

            if (distance > radius) {
                continue;
            }

            float illuminance = 1.0 / (4.0 * PI * distance * distance);
            float distanceRatio = distance / radius;
            float windowTerm = pow(saturate(1.0 - pow(distanceRatio, 4.0)), 2.0);
            radiance = light.colorAndRadius.xyz * illuminance * windowTerm;
        }

        float3 H = normalize(V + L);
        float dotNH = max(dot(N, H), 0.0);
        float dotNV = max(dot(N, V), 0.0);
        float dotNL = max(dot(N, L), 0.0);

        if (dotNL > 0.0) {
            float D = D_GGX(dotNH, roughness);
            float G = G_SchlicksmithGGX(dotNL, dotNV, roughness);
            float3 F = F_Schlick(max(dot(H, V), 0.0), F0);

            float3 kS = F;
            float3 kD = (1.0 - kS) * (1.0 - metallic);
            float3 specular = D * F * G / (4.0 * dotNL * dotNV + 0.001);

            float shadow = calculateShadow(light.shadowIndex, input.fragPos, input.fragViewPos, N, L);
            Lo += (kD * albedo / PI + specular) * radiance * dotNL * shadow;
        }
    }

    // IBL
    float3 F = F_SchlickR(max(dot(N, V), 0.0), F0, roughness);
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);
    float3 ambient = float3(0.0, 0.0, 0.0);

    if (global.irradianceMapIndex != 0 && global.prefilteredMapIndex != 0 && global.brdfLUTIndex != 0) {
        float3 N_rotated = rotateY(N, global.skyboxRotation);
        float3 R = reflect(-V, N);
        float3 R_rotated = rotateY(R, global.skyboxRotation);

        // Diffuse IBL
        float3 irradiance = cubemaps[NonUniformResourceIndex(global.irradianceMapIndex)].Sample(linearSampler, N_rotated).rgb;
        float3 diffuse = irradiance * albedo;

        // Specular IBL
        uint prefilteredIdx = global.prefilteredMapIndex;
        // Query mipmap levels (Slang returns max LOD as number of levels - 1)
        float maxLod = 4.0; // Assume 5 mip levels (128x128 cubemap)
        float3 prefilteredColor = cubemaps[NonUniformResourceIndex(prefilteredIdx)].SampleLevel(linearSampler, R_rotated, roughness * maxLod).rgb;

        float2 envBRDF = textures[NonUniformResourceIndex(global.brdfLUTIndex)].Sample(linearSampler, float2(max(dot(N, V), 0.0), roughness)).rg;
        float3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

        float ao = lerp(1.0, occlusion, material.occlusionStrength);
        ambient = (kD * diffuse * ao + specular) * global.iblIntensity;
    } else {
        ambient = global.ambientLight * albedo * lerp(1.0, occlusion, material.occlusionStrength);
    }

    float3 color = ambient + Lo + emissive;
    return float4(color, baseColor.a);
}