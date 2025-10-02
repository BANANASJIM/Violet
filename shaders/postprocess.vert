#version 450

// Fullscreen quad vertex shader using gl_VertexIndex
// No vertex buffer needed - generates positions from vertex index

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Generate fullscreen triangle using gl_VertexIndex
    // Vertices: (-1,-1), (3,-1), (-1,3) - covers entire screen with one triangle
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 texCoords[3] = vec2[](
        vec2(0.0, 0.0),
        vec2(2.0, 0.0),
        vec2(0.0, 2.0)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragTexCoord = texCoords[gl_VertexIndex];
}
