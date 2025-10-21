#version 450

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

    // Shadow data
    int shadowsEnabled;
    int cascadeDebugMode;  // 0=off, 1=visualize cascades with colors
    uint padding1_0;
    uint padding1_1;  // Align to 16 bytes

    // IBL bindless texture indices
    uint environmentMapIndex;
    uint irradianceMapIndex;
    uint prefilteredMapIndex;
    uint brdfLUTIndex;
} global;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec4 inTangent;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = global.proj * global.view * push.model * vec4(inPosition, 1.0);
    fragColor = inColor;
}