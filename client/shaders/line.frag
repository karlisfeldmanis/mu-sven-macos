#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform vec3 color;
uniform float alpha;
uniform sampler2D ribbonTex;
uniform bool useTexture;
void main() {
    if (useTexture) {
        vec4 t = texture(ribbonTex, TexCoord);
        float brightness = dot(t.rgb, vec3(0.299, 0.587, 0.114));
        FragColor = vec4(t.rgb * color, brightness * alpha);
    } else {
        FragColor = vec4(color, alpha);
    }
}
