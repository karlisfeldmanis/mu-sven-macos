#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D uScene;
uniform float uThreshold; // Brightness threshold for bloom extraction

void main() {
    vec3 color = texture(uScene, TexCoord).rgb;
    // Luminance-weighted brightness check
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    if (brightness > uThreshold) {
        // Soft knee: gradually include pixels above threshold
        float excess = brightness - uThreshold;
        float contribution = excess / (excess + 0.5);
        FragColor = vec4(color * contribution, 1.0);
    } else {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
