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
