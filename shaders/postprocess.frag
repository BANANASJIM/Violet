#version 450

// PostProcess fragment shader
// Samples offscreen color and depth textures from main pass
// set 1: PostProcess descriptor set (color + depth textures)

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

// set 1: PostProcess material set
layout(set = 1, binding = 0) uniform sampler2D colorTexture;
layout(set = 1, binding = 1) uniform sampler2D depthTexture;

void main() {
    // Sample color texture
    vec3 color = texture(colorTexture, fragTexCoord).rgb;

    // Sample depth texture (optional - can be used for effects)
    float depth = texture(depthTexture, fragTexCoord).r;

    // For now, just pass through the color
    // Future: tonemapping, color grading, bloom, etc.
    outColor = vec4(color, 1.0);
    gl_FragDepth = depth;

}
