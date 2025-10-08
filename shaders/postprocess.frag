#version 450

// PostProcess fragment shader
// Samples offscreen color and depth textures from main pass
// Applies ACES Filmic tone mapping and gamma correction
// Reference: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//            Used by Unreal Engine 4/5 as default tone mapper

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// set 1: PostProcess material set
layout(set = 1, binding = 0) uniform sampler2D colorTexture;
layout(set = 1, binding = 1) uniform sampler2D depthTexture;

// Push constants for tone mapping parameters
layout(push_constant) uniform PostProcessParams {
    float ev100;  // Exposure Value at ISO 100
    float gamma;
} params;

// EV100 to exposure conversion (Frostbite/UE4 standard)
// EV100 = exposure value at ISO 100 (photographic exposure)
// Formula: exposure = 1.0 / (1.2 * 2^EV100)
// Reference: https://learnopengl.com/PBR/IBL/Diffuse-irradiance
//            https://seblagarde.wordpress.com/2014/11/04/
// Typical EV100 values:
//   -2: Night scene
//    0: Overcast day
//    9-10: Sunny day (standard)
//   15: Direct sunlight
float ev100ToExposure(float ev100) {
    // Max exposure to prevent division by zero/infinity
    float maxLuminance = 1.2 * exp2(ev100);
    return 1.0 / max(maxLuminance, 0.0001);
}

// ACES Filmic tone mapping operator (Krzysztof Narkowicz approximation)
// Academy Color Encoding System - industry standard for film and games
// Provides smooth highlight roll-off and excellent color preservation
vec3 ACESFilmic(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    // Sample HDR linear color from main pass
    vec3 color = texture(colorTexture, fragTexCoord).rgb;

    // Sample depth texture
    float depth = texture(depthTexture, fragTexCoord).r;

    // Convert EV100 to exposure multiplier
    float exposure = ev100ToExposure(params.ev100);

    // ACES Filmic tone mapping with exposure
    // Output is already in [0,1] range, no white point normalization needed
    color = ACESFilmic(color * exposure);

    // Gamma correction (linear to sRGB)
    color = pow(color, vec3(1.0 / params.gamma));

    outColor = vec4(color, 1.0);
    gl_FragDepth = depth;
}
