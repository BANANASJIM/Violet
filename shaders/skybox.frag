#version 450

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
} global;

layout(set = 0, binding = 1) uniform samplerCube environmentMap;

layout(location = 0) in vec3 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Simple tone mapping functions
vec3 reinhardToneMapping(vec3 color) {
    return color / (color + vec3(1.0));
}

vec3 gammaCorrection(vec3 color) {
    return pow(color, vec3(1.0 / 2.2));
}

void main() {
    // Sample from the environment cubemap
    vec3 direction = normalize(fragTexCoord);
    vec3 color = texture(environmentMap, direction).rgb;

    // Apply exposure
    color *= global.skyboxExposure;

    // Apply tone mapping for HDR
    color = reinhardToneMapping(color);

    // Gamma correction
    color = gammaCorrection(color);

    outColor = vec4(color, 1.0);
}