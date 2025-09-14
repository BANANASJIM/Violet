#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    vec4 baseColor;
    int hasTexture;
} pc;

void main() {
    vec3 baseCol = pc.baseColor.rgb;

    if (pc.hasTexture != 0) {
        vec3 texColor = texture(texSampler, fragTexCoord).rgb;
        baseCol *= texColor;
    }

    baseCol *= fragColor;

    // Simple lighting
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float NdotL = max(dot(fragNormal, lightDir), 0.2);

    outColor = vec4(baseCol * NdotL, pc.baseColor.a);
}