#version 450

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float padding0;

    // Light data (physical units: lux for directional, lumens for point)
    vec4 lightPositions[8];  // xyz=position/direction, w=type (0=dir, 1=point)
    vec4 lightColors[8];     // xyz=color*intensity (physical units), w=radius
    int numLights;
    vec3 ambientLight;

    // Skybox data
    float skyboxExposure;
    float skyboxRotation;
    int skyboxEnabled;
    float iblIntensity;

    // Shadow data
    int shadowsEnabled;
    int cascadeDebugMode;  // 0=off, 1=visualize cascades with colors
    uint padding1_0;
    uint padding1_1;  // Align to 16 bytes

    // IBL bindless texture indices
    uint environmentMapIndex;
    uint irradianceMapIndex;
    uint prefilteredMapIndex;
    uint brdfLUTIndex;
} global;

layout(location = 0) out vec3 fragTexCoord;

void main() {
    // Generate full-screen triangle
    // Vertex positions: (-1,-1), (3,-1), (-1,3)
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 1.0, 1.0);  // Set depth to 1.0 (far plane)

    // Calculate world space direction for cubemap sampling
    // Remove translation from view matrix
    mat3 viewRot = mat3(global.view);
    mat3 invViewRot = transpose(viewRot);

    // Transform screen position to world direction
    vec4 clipPos = vec4(pos, 1.0, 1.0);
    vec4 viewPos = inverse(global.proj) * clipPos;
    viewPos = vec4(viewPos.xy, -1.0, 0.0);  // Point towards negative Z in view space

    // Transform to world space and remove translation
    fragTexCoord = invViewRot * viewPos.xyz;

    // Apply rotation around Y axis
    float s = sin(global.skyboxRotation);
    float c = cos(global.skyboxRotation);
    mat3 rotY = mat3(
        c, 0.0, s,
        0.0, 1.0, 0.0,
        -s, 0.0, c
    );
    fragTexCoord = rotY * fragTexCoord;
}