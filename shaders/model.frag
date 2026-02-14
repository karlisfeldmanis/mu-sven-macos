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
uniform float blendMeshLight;
uniform vec3 terrainLight; // Lightmap color sampled at object world position

// Point lights
const int MAX_POINT_LIGHTS = 64;
uniform int numPointLights;
uniform vec3 pointLightPos[MAX_POINT_LIGHTS];
uniform vec3 pointLightColor[MAX_POINT_LIGHTS];
uniform float pointLightRange[MAX_POINT_LIGHTS];

// Edge fog: darken fragments near terrain boundaries
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
    // Basic lighting (sun)
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(abs(dot(norm, lightDir)), 0.5); // Two-sided lighting
    vec3 lighting = diff * lightColor;

    // Accumulate point lights
    for (int i = 0; i < numPointLights; ++i) {
        vec3 toLight = pointLightPos[i] - FragPos;
        float dist = length(toLight);
        float atten = max(1.0 - dist / pointLightRange[i], 0.0);
        atten *= atten; // Quadratic falloff
        vec3 lDir = normalize(toLight);
        float d = max(abs(dot(norm, lDir)), 0.0);
        lighting += d * atten * pointLightColor[i];
    }

    vec4 texColor = texture(texture_diffuse, TexCoord);
    if (texColor.a < 0.1) discard;

    FragColor = vec4(lighting * blendMeshLight * terrainLight, 1.0) * texColor;

    if (useFog) {
        float dist = length(FragPos - viewPos);
        float fogFactor = clamp((3500.0 - dist) / (3500.0 - 1500.0), 0.0, 1.0);
        vec3 fogColor = vec3(0.117, 0.078, 0.039); // Original MU: 30/256, 20/256, 10/256
        FragColor.rgb = mix(fogColor, FragColor.rgb, fogFactor);

        // Edge fog: darken toward black at map boundaries
        float edgeFactor = computeEdgeFog(FragPos);
        FragColor.rgb = mix(vec3(0.0), FragColor.rgb, edgeFactor);
    }
}
