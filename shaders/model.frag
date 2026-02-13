#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

uniform sampler2D texture_diffuse;
uniform vec3 lightColor;
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform bool useFog;

void main() {
    // Basic lighting
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(abs(dot(norm, lightDir)), 0.5); // Two-sided lighting
    vec3 diffuse = diff * lightColor;

    vec4 texColor = texture(texture_diffuse, TexCoord);
    if (texColor.a < 0.1) discard;

    FragColor = vec4(diffuse, 1.0) * texColor;
    
    if (useFog) {
        float distance = length(FragPos - viewPos);
        float fogFactor = clamp((8000.0 - distance) / (8000.0 - 2000.0), 0.0, 1.0);
        vec3 fogColor = vec3(0.1, 0.1, 0.1);
        FragColor.rgb = mix(fogColor, FragColor.rgb, fogFactor);
    }
}
