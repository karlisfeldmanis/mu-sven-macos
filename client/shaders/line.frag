#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform vec3 color;
uniform float alpha;
uniform sampler2D ribbonTex;
uniform bool useTexture;
uniform bool beamMode;
uniform bool trailMode;
void main() {
    if (trailMode) {
        // Weapon blur trail: U fades from bright (0) to dim (1), V across blade width
        float fade = 1.0 - TexCoord.x;
        // Soft edge falloff across blade width (V=0..1)
        float vDist = abs(TexCoord.y - 0.5) * 2.0;
        float edgeFade = 1.0 - vDist * vDist;
        float intensity = fade * edgeFade * alpha;
        // If blur texture is bound, modulate with texture brightness
        if (useTexture) {
            vec4 t = texture(ribbonTex, TexCoord);
            float texBright = dot(t.rgb, vec3(0.299, 0.587, 0.114));
            // Blend texture detail into the procedural trail
            intensity *= (0.5 + texBright * 0.5);
            FragColor = vec4(mix(color, t.rgb * color, texBright) * intensity, intensity);
        } else {
            FragColor = vec4(color * intensity, intensity);
        }
    } else if (beamMode) {
        // Aqua Beam: Gaussian falloff across width (V) + smooth taper along length (U)
        float vDist = abs(TexCoord.y - 0.5) * 2.0; // 0 at center, 1 at edges
        float falloff = exp(-vDist * vDist * 3.0);  // Gaussian: soft glow across width
        // Smooth taper at beam start (U=0) and end (U=1) — wider fade zone
        float uFade = smoothstep(0.0, 0.12, TexCoord.x) * smoothstep(1.0, 0.88, TexCoord.x);
        float intensity = falloff * uFade;
        FragColor = vec4(color * intensity, intensity);
    } else if (useTexture) {
        vec4 t = texture(ribbonTex, TexCoord);
        float brightness = dot(t.rgb, vec3(0.299, 0.587, 0.114));
        FragColor = vec4(t.rgb * color, brightness * alpha);
    } else {
        FragColor = vec4(color, alpha);
    }
}
