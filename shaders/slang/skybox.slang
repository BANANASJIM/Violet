// Skybox Shader - Slang version
// Full-screen triangle rendering with cubemap sampling

import Types;
import Bindless;

// ========================================
// Vertex Shader
// ========================================

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 fragTexCoord : TEXCOORD;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Generate full-screen triangle
    // Vertex positions: (-1,-1), (3,-1), (-1,3)
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };

    float2 pos = positions[vertexID];
    output.position = float4(pos, 1.0, 1.0);  // Set depth to 1.0 (far plane)

    // Calculate world space direction for cubemap sampling
    // Remove translation from view matrix
    float3x3 viewRot = (float3x3)global.view;
    float3x3 invViewRot = transpose(viewRot);

    // Transform screen position to world direction
    float4 clipPos = float4(pos, 1.0, 1.0);
    float4 viewPos = mul(inverse(global.proj), clipPos);
    viewPos = float4(viewPos.xy, -1.0, 0.0);  // Point towards negative Z in view space

    // Transform to world space
    output.fragTexCoord = mul(invViewRot, viewPos.xyz);

    // Apply rotation around Y axis
    float s = sin(global.skyboxRotation);
    float c = cos(global.skyboxRotation);
    float3x3 rotY = float3x3(
        c, 0.0, s,
        0.0, 1.0, 0.0,
        -s, 0.0, c
    );
    output.fragTexCoord = mul(rotY, output.fragTexCoord);

    return output;
}

// ========================================
// Fragment Shader
// ========================================

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    // Sample from the environment cubemap using bindless index
    float3 direction = normalize(input.fragTexCoord);

    // Debug: visualize direction if no environment map
    if (global.environmentMapIndex == 0) {
        return float4(abs(direction), 1.0);
    }

    float3 color = cubemaps[NonUniformResourceIndex(global.environmentMapIndex)].Sample(textureSampler, direction).rgb;

    // Apply exposure for visual adjustment
    color *= global.skyboxExposure;

    // Output HDR linear color (tone mapping handled in post-process)
    return float4(color, 1.0);
}