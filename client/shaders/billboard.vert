#version 330 core

// Per-vertex quad corner
layout(location = 0) in vec2 aCorner; // -0.5 to 0.5

// Per-instance data
layout(location = 1) in vec3 iWorldPos;
layout(location = 2) in float iScale;
layout(location = 3) in float iRotation;
layout(location = 4) in float iFrame;
layout(location = 5) in vec3 iColor;
layout(location = 6) in float iAlpha;

uniform mat4 view;
uniform mat4 projection;

out vec2 TexCoord;
out vec3 vColor;
out float vAlpha;

void main() {
    // Extract camera right/up from view matrix for billboarding
    vec3 right = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 up    = vec3(view[0][1], view[1][1], view[2][1]);

    // Rotate corner by iRotation around view direction
    float c = cos(iRotation);
    float s = sin(iRotation);
    vec2 rotCorner = mat2(c, -s, s, c) * aCorner;

    // Billboard: expand quad in world space facing camera
    vec3 worldPos = iWorldPos + (right * rotCorner.x + up * rotCorner.y) * iScale;
    gl_Position = projection * view * vec4(worldPos, 1.0);

    // UV: frame >= 0 = sprite sheet (4 columns), frame < 0 = full texture
    float frame = floor(iFrame);
    float u = aCorner.x + 0.5;
    float v = 1.0 - (aCorner.y + 0.5);
    if (frame >= 0.0) {
        TexCoord = vec2(u * 0.25 + frame * 0.25, v);
    } else {
        TexCoord = vec2(u, v);
    }

    vColor = iColor;
    vAlpha = iAlpha;
}
