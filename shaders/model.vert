#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec2 texCoordOffset;
uniform float outlineOffset; // Vertex displacement along normal for outlines

out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord + texCoordOffset;
    
    gl_Position = projection * view * vec4(FragPos, 1.0);
    
    if (outlineOffset > 0.0) {
        // Project normal direction to screen space (ignoring translation)
        vec4 normalClip = projection * view * model * vec4(aNormal, 0.0);
        if (length(normalClip.xy) > 0.0001) {
            vec2 offset = normalize(normalClip.xy) * (outlineOffset * 0.003) * gl_Position.w;
            gl_Position.xy += offset;
        }
    }
}
