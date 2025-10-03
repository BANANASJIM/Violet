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
} global;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec4 inTangent;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;
layout(location = 5) out vec3 fragViewPos;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragPos = worldPos.xyz;

    // Calculate view space position for lighting
    vec4 viewPos = global.view * worldPos;
    fragViewPos = viewPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragTangent = normalize(normalMatrix * inTangent.xyz);
    fragBitangent = cross(fragNormal, fragTangent) * inTangent.w;

    fragTexCoord = inTexCoord;

    gl_Position = global.proj * viewPos;
}
