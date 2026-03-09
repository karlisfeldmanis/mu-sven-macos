#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D uImage;
uniform bool uHorizontal; // true = horizontal pass, false = vertical
uniform float uTexelSize;  // 1.0 / texture dimension (width or height)

// 9-tap Gaussian kernel (sigma ~2.0)
const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    vec3 result = texture(uImage, TexCoord).rgb * weights[0];

    vec2 offset;
    if (uHorizontal)
        offset = vec2(uTexelSize, 0.0);
    else
        offset = vec2(0.0, uTexelSize);

    for (int i = 1; i < 5; ++i) {
        result += texture(uImage, TexCoord + offset * float(i)).rgb * weights[i];
        result += texture(uImage, TexCoord - offset * float(i)).rgb * weights[i];
    }

    FragColor = vec4(result, 1.0);
}
