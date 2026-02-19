#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float outlineOffset; // Pixel-width offset

void main() {
    vec4 viewPos = view * model * vec4(aPos, 1.0);
    gl_Position = projection * viewPos;
    
    if (outlineOffset > 0.0) {
        // Project normal to clip space
        // We use the normal in clip space to displace the vertex outwards relative to the camera
        vec4 normalClip = projection * view * model * vec4(aNormal, 0.0);
        if (length(normalClip.xy) > 0.0001) {
            vec2 offset = normalize(normalClip.xy) * (outlineOffset * 0.003) * gl_Position.w;
            gl_Position.xy += offset;
        }
    }
}
