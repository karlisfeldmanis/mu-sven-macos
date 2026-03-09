#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D uScene;      // Main scene texture
uniform sampler2D uBloom;      // Blurred bright pixels
uniform float uBloomIntensity; // Bloom strength (0.0-1.0)
uniform float uVignetteStrength; // Vignette strength (0.0-1.0)
uniform vec3 uColorTint;       // Per-map color grading multiplier

void main() {
    vec3 scene = texture(uScene, TexCoord).rgb;
    vec3 bloom = texture(uBloom, TexCoord).rgb;

    // Additive bloom
    vec3 color = scene + bloom * uBloomIntensity;

    // Clamp to avoid super-bright artifacts (sRGB pipeline, not HDR)
    color = min(color, vec3(1.5));

    // Per-map color grading
    color *= uColorTint;

    // Vignette: darken edges for cinematic framing
    if (uVignetteStrength > 0.001) {
        vec2 uv = TexCoord * 2.0 - 1.0; // [-1, 1]
        float dist = length(uv);
        float vignette = 1.0 - smoothstep(0.5, 1.4, dist) * uVignetteStrength;
        color *= vignette;
    }

    // Dithering: prevent color banding in gradients (8-bit output)
    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    color += (noise - 0.5) / 255.0;

    // Final clamp
    color = clamp(color, 0.0, 1.0);

    FragColor = vec4(color, 1.0);
}
