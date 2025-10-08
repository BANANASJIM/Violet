#version 450
#extension GL_EXT_nonuniform_qualifier : enable

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

// Bindless cubemap array (set 1, binding 1, separate from 2D textures at binding 0)
layout(set = 1, binding = 1) uniform samplerCube cubemaps[];

layout(location = 0) in vec3 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    // Sample from the environment cubemap using bindless index
    vec3 direction = normalize(fragTexCoord);

    // Check if environment map is available
    if (global.environmentMapIndex == 0) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);  // Black if no environment map
        return;
    }

    vec3 color = texture(cubemaps[nonuniformEXT(global.environmentMapIndex)], direction).rgb;

    // Apply exposure for visual adjustment
    color *= global.skyboxExposure;

    // Output HDR linear color (tone mapping and gamma correction handled in post-process)
    outColor = vec4(color, 1.0);
}