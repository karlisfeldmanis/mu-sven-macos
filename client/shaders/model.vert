#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in float aBoneIndex; // GPU skinning: bone index per vertex

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec2 texCoordOffset;
uniform float outlineOffset; // Vertex displacement along normal for outlines

// GPU skeletal animation (trees, low-instance animated objects)
uniform bool useSkinning;
const int MAX_BONES = 48;
uniform mat4 boneMatrices[MAX_BONES];

// Per-instance tree sway: phase offset + time for unique wind per tree
uniform float swayPhase; // unique per instance (derived from position)
uniform float swayTime;  // global time

out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;

void main() {
    vec3 localPos = aPos;
    vec3 localNorm = aNormal;

    if (useSkinning) {
        int bi = int(aBoneIndex);
        if (bi >= 0 && bi < MAX_BONES) {
            localPos = vec3(boneMatrices[bi] * vec4(aPos, 1.0));
            localNorm = mat3(boneMatrices[bi]) * aNormal;
        }

        // Per-instance wind sway: gentle vertex displacement on top of bone animation
        float heightWeight = clamp((localPos.z - 50.0) / 250.0, 0.0, 1.0);
        if (heightWeight > 0.0 && swayPhase >= 0.0) {
            float t = swayTime + swayPhase;
            // Per-instance amplitude variation (0.3 to 1.0) so each tree sways differently
            float ampScale = 0.3 + 0.7 * fract(swayPhase * 2.17 + 0.5);
            // Two overlapping sine waves with different frequencies per instance
            float freqVar = 0.8 + 0.4 * fract(swayPhase * 1.53);
            float swayX = sin(t * 0.7 * freqVar + swayPhase * 3.7) * 3.0
                        + sin(t * 1.5 * freqVar + swayPhase * 5.1) * 1.2;
            float swayY = sin(t * 0.6 * freqVar + swayPhase * 2.3) * 2.5
                        + cos(t * 1.1 * freqVar + swayPhase * 4.2) * 1.0;
            localPos.x += swayX * heightWeight * ampScale;
            localPos.y += swayY * heightWeight * ampScale;
        }
    }

    FragPos = vec3(model * vec4(localPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * localNorm;
    TexCoord = aTexCoord + texCoordOffset;

    gl_Position = projection * view * vec4(FragPos, 1.0);

    if (outlineOffset > 0.0) {
        vec4 normalClip = projection * view * model * vec4(localNorm, 0.0);
        if (length(normalClip.xy) > 0.0001) {
            vec2 offset = normalize(normalClip.xy) * (outlineOffset * 0.003) * gl_Position.w;
            gl_Position.xy += offset;
        }
    }
}
