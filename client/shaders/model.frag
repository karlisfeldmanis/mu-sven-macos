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
uniform vec3 uFogColor;
uniform float uFogNear;
uniform float uFogFar;
uniform float blendMeshLight;
uniform float objectAlpha; // Per-instance alpha for roof hiding (0=invisible, 1=opaque)
uniform vec3 terrainLight; // Lightmap color sampled at object world position
uniform float luminosity;  // Day/night cycle (0.45-0.75)

// Chrome environment mapping (Main 5.2 RENDER_CHROME/CHROME2/METAL)
// 0=off, 1=CHROME, 2=CHROME2, 3=METAL
uniform int chromeMode;
uniform float chromeTime;

// Item glow (Main 5.2 +7/+9/+11 additive pass)
uniform vec3 glowColor; // (0,0,0) = normal, else = self-illuminating glow

// Base color tint (Main 5.2 +3/+5 enhancement body tinting)
uniform vec3 baseTint; // (1,1,1) = no tint

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
    // Basic lighting (sun) — modulated by terrain lightmap (baked shadows)
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(abs(dot(norm, lightDir)), 0.5); // Two-sided lighting
    vec3 tLight = max(terrainLight, vec3(0.30));
    vec3 sunLit = diff * lightColor * tLight;

    // Point lights bypass terrain lightmap — local sources illuminate
    // regardless of baked terrain shadows. Min 0.25 ambient term ensures
    // overhead lights (lamps, torches) illuminate characters whose normals
    // are mostly horizontal.
    vec3 pointLit = vec3(0.0);
    for (int i = 0; i < numPointLights; ++i) {
        vec3 toLight = pointLightPos[i] - FragPos;
        float dist = length(toLight);
        float atten = max(1.0 - dist / pointLightRange[i], 0.0);
        atten *= atten; // Quadratic falloff
        vec3 lDir = normalize(toLight);
        float d = max(abs(dot(norm, lDir)), 0.25);
        pointLit += d * atten * pointLightColor[i];
    }

    vec3 lighting = sunLit + pointLit;

    // Chrome/Metal: replace texture coords with normal-derived UVs (Main 5.2)
    // Main 5.2 ZzzBMD.cpp RenderMesh: g_chrome UV from vertex normals
    // chromeTime = glfwGetTime() (seconds); Main 5.2 WorldTime = milliseconds
    // Wave = WorldTime * 0.0001 = seconds * 0.1
    vec2 finalUV = TexCoord;
    if (chromeMode == 1) {
        // RENDER_CHROME: g_chrome[j][0] = Normal[2]*0.5 + Wave
        //                g_chrome[j][1] = Normal[1]*0.5 + Wave*2
        float wave = chromeTime * 0.1;
        finalUV.x = norm.z * 0.5 + wave;
        finalUV.y = norm.y * 0.5 + wave * 2.0;
    } else if (chromeMode == 2) {
        // RENDER_CHROME2: g_chrome[j][0] = (Normal[2]+Normal[0])*0.8 + Wave2*2
        //                 g_chrome[j][1] = (Normal[1]+Normal[0])*1.0 + Wave2*3
        // Main 5.2: Wave2 = (int)WorldTime%5000 * 0.00024f - 0.4f (linear sawtooth)
        // Range: -0.4 to +0.8, period 5 seconds, sharp reset
        float wave2 = mod(chromeTime, 5.0) * 0.24 - 0.4;
        finalUV.x = (norm.z + norm.x) * 0.8 + wave2 * 2.0;
        finalUV.y = (norm.y + norm.x) * 1.0 + wave2 * 3.0;
    } else if (chromeMode == 3) {
        // RENDER_METAL: g_chrome[j][0] = Normal[2]*0.5 + 0.2
        //               g_chrome[j][1] = Normal[1]*0.5 + 0.5
        finalUV.x = norm.z * 0.5 + 0.2;
        finalUV.y = norm.y * 0.5 + 0.5;
    } else if (chromeMode == 4) {
        // RENDER_CHROME4 (+13): dynamic light vector L, most intense animation
        // Main 5.2 ZzzBMD.cpp:1076-1081
        float wave = chromeTime * 0.1;
        vec3 L = vec3(cos(chromeTime * 1.0), sin(chromeTime * 2.0), 1.0);
        finalUV.x = dot(norm, L);
        finalUV.y = 1.0 - dot(norm, L);
        finalUV.y -= norm.z * 0.5 + wave * 3.0;
        finalUV.x += norm.y * 0.5 + L.y * 3.0;
    }

    vec4 texColor = texture(texture_diffuse, finalUV);
    if (texColor.a < 0.1) discard;

    // Item glow: additive enhancement pass
    // Main 5.2: CHROME/METAL use PartObjectColor (fixed palette, self-illuminating)
    //           CHROME2/CHROME4 use PartObjectColor2 (modulates scene light)
    vec3 finalLight;
    if (glowColor.r + glowColor.g + glowColor.b > 0.001) {
        if (chromeMode == 2 || chromeMode == 4) {
            // CHROME2/CHROME4: color modulates scene light (Main 5.2 PartObjectColor2)
            finalLight = glowColor * lighting * luminosity;
        } else {
            // CHROME/METAL: fixed palette color bypasses scene lighting
            finalLight = glowColor;
        }
    } else {
        finalLight = lighting * blendMeshLight * luminosity * baseTint;
    }
    FragColor = vec4(finalLight, objectAlpha) * texColor;

    if (useFog) {
        float dist = length(FragPos - viewPos);
        float fogFactor = clamp((uFogFar - dist) / (uFogFar - uFogNear), 0.0, 1.0);
        FragColor.rgb = mix(uFogColor * luminosity, FragColor.rgb, fogFactor);

        // Edge fog: darken at map boundaries (semi-transparent, not pure black)
        float edgeFactor = computeEdgeFog(FragPos);
        float edgeBlend = mix(0.75, 1.0, edgeFactor); // fade to 75% brightness at edges
        FragColor.rgb *= edgeBlend;
    }
}
