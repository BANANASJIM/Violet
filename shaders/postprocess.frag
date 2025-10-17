#version 450

// PostProcess fragment shader
// Samples offscreen color and depth textures from main pass
// Applies ACES Filmic tone mapping and gamma correction
// Reference: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//            Used by Unreal Engine 4/5 as default tone mapper

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Set 0: PostProcess descriptor set
layout(set = 0, binding = 0) uniform sampler2D colorTexture;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;

// Push constants for tone mapping parameters
layout(push_constant) uniform PostProcessParams {
    float ev100;    // Exposure Value at ISO 100
    float gamma;
    uint tonemapMode;  // 0=ACES Fitted, 1=ACES Narkowicz, 2=Uncharted2, 3=Reinhard, 4=None
    float padding;  // Alignment
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

// ============================================================================
// Tone Mapping Operators
// ============================================================================

// ACES Fitted (Stephen Hill / MJP) - UE4/UE5 Default
// Reference: https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
// Most accurate ACES implementation with proper color space transforms

const mat3 ACESInputMat = mat3(
    0.59719, 0.35458, 0.04823,
    0.07600, 0.90834, 0.01566,
    0.02840, 0.13383, 0.83777
);

const mat3 ACESOutputMat = mat3(
     1.60475, -0.53108, -0.07367,
    -0.10208,  1.10813, -0.00605,
    -0.00327, -0.07276,  1.07602
);

vec3 RRTAndODTFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 ACESFitted(vec3 color) {
    color = ACESInputMat * color;
    color = RRTAndODTFit(color);
    color = ACESOutputMat * color;
    return clamp(color, 0.0, 1.0);
}

// ACES Filmic (Narkowicz approximation) - Fast but less accurate
// Reference: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
// Simplified curve without color space transforms
vec3 ACESNarkowicz(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Uncharted 2 Filmic (John Hable)
// Reference: http://filmicworlds.com/blog/filmic-tonemapping-operators/
// Classic game industry tone mapper with white point normalization
vec3 Uncharted2Tonemap(vec3 x) {
    float A = 0.15;  // Shoulder strength
    float B = 0.50;  // Linear strength
    float C = 0.10;  // Linear angle
    float D = 0.20;  // Toe strength
    float E = 0.02;  // Toe numerator
    float F = 0.30;  // Toe denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 Uncharted2Filmic(vec3 color) {
    const float W = 11.2;  // Linear white point value
    vec3 curr = Uncharted2Tonemap(color);
    vec3 whiteScale = 1.0 / Uncharted2Tonemap(vec3(W));
    return curr * whiteScale;
}

// Reinhard tone mapping
// Simple and fast, but can look washed out
vec3 Reinhard(vec3 color) {
    return color / (color + vec3(1.0));
}

// Reinhard Luminance-based (preserves color better)
vec3 ReinhardLuminance(vec3 color) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float toneMappedLuma = luma / (1.0 + luma);
    return color * (toneMappedLuma / max(luma, 0.001));
}

void main() {
    // Sample HDR linear color from main pass
    vec3 color = texture(colorTexture, fragTexCoord).rgb;

    // Sample depth texture
    float depth = texture(depthTexture, fragTexCoord).r;

    // Convert EV100 to exposure multiplier
    float exposure = ev100ToExposure(params.ev100);

    // Apply exposure
    color = color * exposure;

    // Apply tone mapping based on selected mode
    switch (params.tonemapMode) {
        case 0u:  // ACES Fitted (default, recommended)
            color = ACESFitted(color);
            break;
        case 1u:  // ACES Narkowicz
            color = ACESNarkowicz(color);
            break;
        case 2u:  // Uncharted 2
            color = Uncharted2Filmic(color);
            break;
        case 3u:  // Reinhard (luminance-based)
            color = ReinhardLuminance(color);
            break;
        case 4u:  // None (linear, for debugging)
            color = clamp(color, 0.0, 1.0);
            break;
        default:  // Fallback to ACES Fitted
            color = ACESFitted(color);
            break;
    }

    // Gamma correction (linear to sRGB)
    color = pow(color, vec3(1.0 / params.gamma));

    outColor = vec4(color, 1.0);
    gl_FragDepth = depth;
}
