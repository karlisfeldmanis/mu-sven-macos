#version 330 core
out vec4 FragColor;
in vec2 vPos;
uniform float playerHPRatio;
uniform float damageFlash;

void main() {
    float dist = length(vPos);
    vec4 finalColor = vec4(0.0);
    
    // Vignette for low HP (< 30%)
    if (playerHPRatio < 0.3) {
        float intensity = 1.0 - (playerHPRatio / 0.3);
        float vignette = smoothstep(0.4, 1.2, dist);
        finalColor += vec4(0.8, 0.0, 0.0, vignette * intensity * 0.5);
    }
    
    // Damage flash
    finalColor += vec4(1.0, 0.0, 0.0, damageFlash * 0.4);
    
    FragColor = finalColor;
}
