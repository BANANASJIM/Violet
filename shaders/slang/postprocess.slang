// PostProcess Shader - Slang version
// Tone mapping and gamma correction

// ========================================
// Descriptors
// ========================================

[[vk::binding(0, 0)]]
Texture2D colorTexture;

[[vk::binding(1, 0)]]
Texture2D depthTexture;

[[vk::binding(2, 0)]]
SamplerState texSampler;

// Push constants for tone mapping parameters
struct PostProcessParams {
    float ev100;    // Exposure Value at ISO 100
    float gamma;
    uint tonemapMode;  // 0=ACES Fitted, 1=ACES Narkowicz, 2=Uncharted2, 3=Reinhard, 4=None
    float padding;
};

[[vk::push_constant]]
ConstantBuffer<PostProcessParams> params;

// ========================================
// Vertex Shader
// ========================================

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Generate fullscreen triangle
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };

    float2 texCoords[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };

    output.position = float4(positions[vertexID], 0.0, 1.0);
    output.fragTexCoord = texCoords[vertexID];

    return output;
}

// ========================================
// Fragment Shader - Tone Mapping Functions
// ========================================

// EV100 to exposure conversion
float ev100ToExposure(float ev100) {
    float maxLuminance = 1.2 * exp2(ev100);
    return 1.0 / max(maxLuminance, 0.0001);
}

// ACES Fitted (Stephen Hill / MJP) - UE4/UE5 Default
static const float3x3 ACESInputMat = float3x3(
    0.59719, 0.35458, 0.04823,
    0.07600, 0.90834, 0.01566,
    0.02840, 0.13383, 0.83777
);

static const float3x3 ACESOutputMat = float3x3(
     1.60475, -0.53108, -0.07367,
    -0.10208,  1.10813, -0.00605,
    -0.00327, -0.07276,  1.07602
);

float3 RRTAndODTFit(float3 v) {
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

float3 ACESFitted(float3 color) {
    color = mul(ACESInputMat, color);
    color = RRTAndODTFit(color);
    color = mul(ACESOutputMat, color);
    return saturate(color);
}

// ACES Filmic (Narkowicz approximation)
float3 ACESNarkowicz(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Uncharted 2 Filmic (John Hable)
float3 Uncharted2Tonemap(float3 x) {
    float A = 0.15;  // Shoulder strength
    float B = 0.50;  // Linear strength
    float C = 0.10;  // Linear angle
    float D = 0.20;  // Toe strength
    float E = 0.02;  // Toe numerator
    float F = 0.30;  // Toe denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 Uncharted2Filmic(float3 color) {
    const float W = 11.2;  // Linear white point
    float3 curr = Uncharted2Tonemap(color);
    float3 whiteScale = 1.0 / Uncharted2Tonemap(float3(W, W, W));
    return curr * whiteScale;
}

// Reinhard tone mapping (luminance-based)
float3 ReinhardLuminance(float3 color) {
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float toneMappedLuma = luma / (1.0 + luma);
    return color * (toneMappedLuma / max(luma, 0.001));
}

// ========================================
// Fragment Shader Main
// ========================================

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    // Sample HDR linear color
    float3 color = colorTexture.Sample(texSampler, input.fragTexCoord).rgb;

    // Sample depth
    float depth = depthTexture.Sample(texSampler, input.fragTexCoord).r;

    // Convert EV100 to exposure
    float exposure = ev100ToExposure(params.ev100);

    // Apply exposure
    color = color * exposure;

    // Apply tone mapping based on selected mode
    switch (params.tonemapMode) {
        case 0:  // ACES Fitted
            color = ACESFitted(color);
            break;
        case 1:  // ACES Narkowicz
            color = ACESNarkowicz(color);
            break;
        case 2:  // Uncharted 2
            color = Uncharted2Filmic(color);
            break;
        case 3:  // Reinhard
            color = ReinhardLuminance(color);
            break;
        case 4:  // None (linear)
            color = saturate(color);
            break;
        default:
            color = ACESFitted(color);
            break;
    }

    // Gamma correction (linear to sRGB)
    color = pow(color, float3(1.0 / params.gamma, 1.0 / params.gamma, 1.0 / params.gamma));

    return float4(color, 1.0);
}