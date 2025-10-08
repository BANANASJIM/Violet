#version 450

// PostProcess fragment shader
// Samples offscreen color and depth textures from main pass
// Applies Uncharted2 tone mapping and gamma correction
// Reference: http://filmicgames.com/archives/75

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// set 1: PostProcess material set
layout(set = 1, binding = 0) uniform sampler2D colorTexture;
layout(set = 1, binding = 1) uniform sampler2D depthTexture;

// Push constants for tone mapping parameters
layout(push_constant) uniform PostProcessParams {
    float exposure;
    float gamma;
} params;

// Uncharted2 tone mapping operator
// From http://filmicgames.com/archives/75
vec3 Uncharted2Tonemap(vec3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

void main() {
    // Sample HDR linear color from main pass
    vec3 color = texture(colorTexture, fragTexCoord).rgb;

    // Sample depth texture
    float depth = texture(depthTexture, fragTexCoord).r;

    // Uncharted2 tone mapping with exposure
    color = Uncharted2Tonemap(color * params.exposure);
    color = color * (1.0 / Uncharted2Tonemap(vec3(11.2)));

    // Gamma correction (linear to sRGB)
    color = pow(color, vec3(1.0 / params.gamma));

    outColor = vec4(color, 1.0);
    gl_FragDepth = depth;
}
