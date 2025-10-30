// TypeDefinitions.slang - Shared type definitions (use with __include)
// NOTE: This file should be __include'd, not import'ed

struct CameraUBO {
    column_major float4x4 view;
    column_major float4x4 proj;
    float3 cameraPos;

    // Light count (actual light data in StructuredBuffer<LightData>)
    int numLights;
    float3 ambientLight;

    // Skybox parameters
    float skyboxExposure;
    float skyboxRotation;
    int skyboxEnabled;
    float iblIntensity;

    // Shadow parameters
    int shadowsEnabled;
    int cascadeDebugMode;

    // IBL bindless indices
    uint environmentMapIndex;
    uint irradianceMapIndex;
    uint prefilteredMapIndex;
    uint brdfLUTIndex;
};

struct LightData {
    float4 positionAndType;  // xyz=position/direction, w=type
    float4 colorAndRadius;   // xyz=color*intensity, w=radius
    int shadowIndex;
};

struct ShadowData {
    column_major float4x4 cascadeViewProjMatrices[4];
    float4 cascadeSplitDepths;
    float4 atlasRects[4];

    float4 shadowParams;
    uint lightType;
    uint cascadeCount;
    uint atlasIndex;

    column_major float4x4 cubeFaceMatrices[6];
};

struct MaterialData {
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float3 emissiveFactor;
    float alphaCutoff;

    uint baseColorTexIndex;
    uint metallicRoughnessTexIndex;
    uint normalTexIndex;
    uint occlusionTexIndex;
    uint emissiveTexIndex;
};

struct PushConstants {
    column_major float4x4 model;
    uint materialID;
};