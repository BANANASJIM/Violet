// Types.slang - Data structures for 3-set descriptor layout

// ========================================
// Set 0: Per-Frame Data
// ========================================

// Camera and view parameters (Dynamic Offset for triple buffering)
struct CameraUBO {
    float4x4 view;
    float4x4 proj;
    float3 cameraPos;
    float exposure;  // From AutoExposure system

    // Skybox parameters
    float skyboxRotation;
    float skyboxExposure;
    int skyboxEnabled;
    float iblIntensity;

    // Shadow parameters
    int shadowsEnabled;
    int cascadeDebugMode;
    uint lightCount;  // Number of active lights
    uint padding0;

    // IBL bindless indices (moved here for better cache locality)
    uint environmentMapIndex;
    uint irradianceMapIndex;
    uint prefilteredMapIndex;
    uint brdfLUTIndex;
};

// Light data structure (unchanged)
struct LightData {
    float4 positionAndType;  // xyz=position/direction, w=type
    float4 colorAndRadius;   // xyz=color*intensity, w=radius
    int shadowIndex;
    uint padding[3];
};

// Shadow data structure (unchanged from original)
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

// ========================================
// Set 2: Material Data
// ========================================

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

// ========================================
// Push Constants
// ========================================

struct PushConstants {
    float4x4 model;
    uint materialID;
    uint padding[3];  // Align to 16 bytes
};

struct ShadowPushConstants {
    float4x4 lightSpaceMatrix;
    float4x4 model;
};

struct PostProcessParams {
    float ev100;
    float gamma;
    uint tonemapMode;
    float padding;
};
