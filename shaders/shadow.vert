#version 450

layout(push_constant) uniform ShadowPushConstants {
    mat4 lightSpaceMatrix;
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = push.lightSpaceMatrix * push.model * vec4(inPosition, 1.0);
}