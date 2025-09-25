#version 450

// Simple material uniform
layout(set = 1, binding = 0) uniform UnlitMaterial {
    vec4 baseColor;
} material;

// Optional base color texture
layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    // Sample base color texture and multiply with material color
    vec4 textureColor = texture(baseColorTexture, fragTexCoord);
    vec4 finalColor = material.baseColor * textureColor;

    // Also blend in vertex color for visibility
    finalColor.rgb *= fragColor;

    outColor = finalColor;
}