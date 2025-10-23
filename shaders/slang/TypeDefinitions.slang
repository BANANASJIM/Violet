// TypeDefinitions.slang - Shared type definitions (use with __include)
// NOTE: This file should be __include'd, not import'ed

struct CameraUBO {
    float4x4 view;
    float4x4 proj;
    float3 cameraPos;
    float padding0;

    // Light data (physical units: lux for directional, lumens for point)
    float4 lightPositions[8];
    float4 lightColors[8];
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
    uint padding1_0;
    uint padding1_1;

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
    uint padding[3];
};

struct ShadowData {
    float4x4 cascadeViewProjMatrices[4];
    float4 cascadeSplitDepths;
    float4 atlasRects[4];

    float4 shadowParams;
    uint lightType;
    uint cascadeCount;
    uint atlasIndex;
    uint padding0;

    float4x4 cubeFaceMatrices[6];
    uint padding1_0;
    uint padding1_1;
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
    uint padding[3];
};

struct PushConstants {
    float4x4 model;
    uint materialID;
    uint padding[3];
};