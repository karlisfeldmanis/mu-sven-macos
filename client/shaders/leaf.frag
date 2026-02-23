#version 330 core
in vec2 TexCoord;

uniform sampler2D leafTexture;
uniform float leafAlpha;

out vec4 FragColor;

void main() {
    vec4 tex = texture(leafTexture, TexCoord);
    // Alpha test: discard transparent pixels
    if (tex.a < 0.15) discard;
    FragColor = vec4(tex.rgb, tex.a * leafAlpha);
}
