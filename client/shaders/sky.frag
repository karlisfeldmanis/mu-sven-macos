#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in float Alpha;
in vec3 FragPos;

uniform sampler2D skyTexture;
uniform vec3 fogColor;

// Edge fog: darken sky near terrain boundaries
// edgeMargin pushes full-opacity fog inward so map borders are fully hidden
float computeEdgeFog(vec3 worldPos) {
    float edgeWidth = 2500.0;
    float edgeMargin = 500.0;
    float terrainMax = 25600.0;
    float dEdge = min(min(worldPos.x, terrainMax - worldPos.x),
                      min(worldPos.z, terrainMax - worldPos.z));
    return smoothstep(edgeMargin, edgeMargin + edgeWidth, dEdge);
}

void main() {
    vec3 resultColor;
    float resultAlpha;

    if (Alpha > 1.5) {
        // Bottom cap: solid fog color to fill void below terrain
        resultColor = fogColor;
        resultAlpha = 1.0;
    } else {
        // Cylinder band: sky texture blended with fog, fading out at top
        vec4 texColor = texture(skyTexture, TexCoord);
        resultColor = mix(fogColor, texColor.rgb, 0.8);
        resultAlpha = Alpha;
    }

    // Edge fog: darken toward black at map boundaries
    float edgeFactor = computeEdgeFog(FragPos);
    resultColor = mix(vec3(0.0), resultColor, edgeFactor);

    FragColor = vec4(resultColor, resultAlpha);
}
