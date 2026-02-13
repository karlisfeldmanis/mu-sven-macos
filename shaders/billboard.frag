#version 330 core

in vec2 TexCoord;
in vec3 vColor;
in float vAlpha;

uniform sampler2D fireTexture;

out vec4 FragColor;

void main() {
    vec4 tex = texture(fireTexture, TexCoord);
    // Use luminance as alpha for additive blending falloff
    float brightness = dot(tex.rgb, vec3(0.299, 0.587, 0.114));
    FragColor = vec4(tex.rgb * vColor * vAlpha, brightness * vAlpha);
}
