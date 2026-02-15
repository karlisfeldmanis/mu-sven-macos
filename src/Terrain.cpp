#include "Terrain.hpp"
#include "TextureLoader.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <set>

const char *terrainVertexShaderSource = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aColor;

out vec2 TexCoord;
out vec3 VertColor;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    gl_Position = projection * view * worldPos;
    TexCoord = aTexCoord;
    VertColor = aColor;
    FragPos = worldPos.xyz;
}
)glsl";

const char *terrainFragmentShaderSource = R"glsl(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec3 VertColor;
in vec3 FragPos;

uniform vec3 viewPos;
uniform sampler2DArray tileTextures;
uniform sampler2D layer1Map;
uniform sampler2D layer2Map;
uniform sampler2D alphaMap;
uniform usampler2D attributeMap;
uniform usampler2D symmetryMap;
uniform sampler2D lightMap;
uniform float uTime;
uniform int debugMode;
uniform float luminosity;

vec2 applySymmetry(vec2 uv, uint symmetry) {
    uint rot = symmetry & 3u;
    bool flipX = (symmetry & 4u) != 0u;
    bool flipY = (symmetry & 8u) != 0u;
    vec2 res = uv;
    if (flipX) res.x = 1.0 - res.x;
    if (flipY) res.y = 1.0 - res.y;
    if (rot == 1u) res = vec2(1.0 - res.y, res.x);
    else if (rot == 2u) res = vec2(1.0 - res.x, 1.0 - res.y);
    else if (rot == 3u) res = vec2(res.y, 1.0 - res.x);
    return res;
}

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

vec4 sampleLayerSmooth(sampler2D layerMap, vec2 uv, vec2 uvBase) {
    vec2 size = vec2(256.0);
    vec2 texelSize = 1.0 / size;
    vec2 gridPos = uvBase - 0.5;
    vec2 f = fract(gridPos);
    vec2 i = floor(gridPos);

    // Center cell (already passed NOGROUND check in main())
    vec2 centerCoord = (floor(uvBase) + 0.5) * texelSize;
    float centerSrc = texture(layerMap, centerCoord).r * 255.0;
    uint centerSym = texture(symmetryMap, centerCoord).r;

    bool centerIsWater = (abs(centerSrc - 5.0) < 0.1);

    float src[4];
    uint sym[4];
    uint attr[4];
    for(int k=0; k<4; ++k) {
        vec2 off = vec2(float(k % 2), float(k / 2));
        vec2 coord = (i + off + 0.5) * texelSize;
        src[k] = texture(layerMap, coord).r * 255.0;
        sym[k] = texture(symmetryMap, coord).r;
        attr[k] = texture(attributeMap, coord).r;
    }

    // Check if any bilinear neighbor has TW_NOGROUND (bridge area)
    bool anyNoGround = false;
    for(int k=0; k<4; ++k) {
        if ((attr[k] & 8u) != 0u) anyNoGround = true;
    }

    vec4 c[4];
    for(int k=0; k<4; ++k) {
        bool isWater = (abs(src[k] - 5.0) < 0.1);

        // At bridge boundaries (TW_NOGROUND): snap to prevent water on road.
        // At normal water/land boundaries: allow bilinear for smooth transition.
        if (isWater != centerIsWater && anyNoGround) {
            vec2 cUV = fract(uv * 0.25);
            cUV = applySymmetry(cUV, centerSym);
            if(centerIsWater) {
                cUV.x += uTime * 0.1;
                cUV.y += sin(uTime + (uv.y * 0.25) * 10.0) * 0.05;
            }
            c[k] = texture(tileTextures, vec3(cUV, floor(centerSrc + 0.5)));
        } else {
            vec2 tileUV = fract(uv * 0.25);
            tileUV = applySymmetry(tileUV, sym[k]);
            if(isWater) {
                tileUV.x += uTime * 0.1;
                tileUV.y += sin(uTime + (uv.y * 0.25) * 10.0) * 0.05;
            }
            c[k] = texture(tileTextures, vec3(tileUV, floor(src[k] + 0.5)));
        }
    }

    // Bilinear mix of colors
    vec4 color = mix(mix(c[0], c[1], f.x), mix(c[2], c[3], f.x), f.y);
    // Mask out 255/invalid
    float m[4];
    for(int k=0; k<4; k++) m[k] = (src[k] < 254.5) ? 1.0 : 0.0;
    float mask = mix(mix(m[0], m[1], f.x), mix(m[2], m[3], f.x), f.y);
    
    return vec4(color.rgb, mask);
}

void main() {
    vec2 uvBase = TexCoord * 256.0;

    // Smooth alpha sampling
    float alpha = texture(alphaMap, (uvBase + 0.5) / 256.0).r;

    vec4 l1 = sampleLayerSmooth(layer1Map, uvBase, uvBase);
    vec4 l2 = sampleLayerSmooth(layer2Map, uvBase, uvBase);

    vec3 finalColor = mix(l1.rgb, l2.rgb, alpha * l2.a);

    // Apply lightmap and day/night luminosity
    vec3 lightColor = texture(lightMap, TexCoord).rgb;
    finalColor *= lightColor * luminosity;

    // Fog: match original engine (CameraViewFar=2000, dark brown fog color)
    float dist = length(FragPos - viewPos);
    float fogNear = 1500.0;
    float fogFar = 3500.0;
    float fogFactor = clamp((fogFar - dist) / (fogFar - fogNear), 0.0, 1.0);
    vec3 fogColor = vec3(0.117, 0.078, 0.039) * luminosity; // 30/256, 20/256, 10/256
    finalColor = mix(fogColor, finalColor, fogFactor);

    FragColor = vec4(finalColor, 1.0);
}
)glsl";

Terrain::Terrain() : VAO(0), VBO(0), EBO(0), shaderProgram(0), indexCount(0) {}

void Terrain::Init() { setupShader(); }

Terrain::~Terrain() {
  if (VAO)
    glDeleteVertexArrays(1, &VAO);
  if (VBO)
    glDeleteBuffers(1, &VBO);
  if (EBO)
    glDeleteBuffers(1, &EBO);
  if (tileTextureArray)
    glDeleteTextures(1, &tileTextureArray);
  if (layer1InfoMap)
    glDeleteTextures(1, &layer1InfoMap);
  if (layer2InfoMap)
    glDeleteTextures(1, &layer2InfoMap);
  if (alphaMap)
    glDeleteTextures(1, &alphaMap);
  if (attributeMap)
    glDeleteTextures(1, &attributeMap);
  if (symmetryMap)
    glDeleteTextures(1, &symmetryMap);
  if (lightmapTex)
    glDeleteTextures(1, &lightmapTex);
}

void Terrain::Load(const TerrainData &data, int worldID,
                   const std::string &data_path) {
  this->worldID = worldID;
  setupMesh(data.heightmap, data.lightmap);
  std::string worldDir = data_path + "/World" + std::to_string(worldID);
  setupTextures(data, worldDir);
}

void Terrain::SetPointLights(const std::vector<glm::vec3> &positions,
                             const std::vector<glm::vec3> &colors,
                             const std::vector<float> &ranges) {
  plCount = std::min((int)positions.size(), 64);
  plPositions.assign(positions.begin(), positions.begin() + plCount);
  plColors.assign(colors.begin(), colors.begin() + plCount);
  plRanges.assign(ranges.begin(), ranges.begin() + plCount);
}

void Terrain::Render(const glm::mat4 &view, const glm::mat4 &projection,
                     float time, const glm::vec3 &viewPos) {
  if (indexCount == 0)
    return;

  glUseProgram(shaderProgram);

  glm::mat4 model = glm::mat4(1.0f);
  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE,
                     glm::value_ptr(model));
  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE,
                     glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1,
                     GL_FALSE, glm::value_ptr(projection));
  glUniform1f(glGetUniformLocation(shaderProgram, "uTime"), time);
  glUniform1i(glGetUniformLocation(shaderProgram, "debugMode"), debugMode);
  glUniform1f(glGetUniformLocation(shaderProgram, "luminosity"), m_luminosity);
  glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"), 1,
               glm::value_ptr(viewPos));

  // Apply dynamic point lights to lightmap (CPU-side, matching original engine)
  applyDynamicLights();

  glBindVertexArray(VAO);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, tileTextureArray);
  glUniform1i(glGetUniformLocation(shaderProgram, "tileTextures"), 0);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, layer1InfoMap);
  glUniform1i(glGetUniformLocation(shaderProgram, "layer1Map"), 1);

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, layer2InfoMap);
  glUniform1i(glGetUniformLocation(shaderProgram, "layer2Map"), 2);

  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, alphaMap);
  glUniform1i(glGetUniformLocation(shaderProgram, "alphaMap"), 3);

  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, attributeMap);
  glUniform1i(glGetUniformLocation(shaderProgram, "attributeMap"), 4);

  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D, symmetryMap);
  glUniform1i(glGetUniformLocation(shaderProgram, "symmetryMap"), 5);

  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_2D, lightmapTex);
  glUniform1i(glGetUniformLocation(shaderProgram, "lightMap"), 6);

  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount),
                 GL_UNSIGNED_INT, 0);
}

void Terrain::setupMesh(const std::vector<float> &heightmap,
                        const std::vector<glm::vec3> &lightmap) {
  std::vector<Vertex> vertices;
  std::vector<unsigned int> indices;

  const int size = TerrainParser::TERRAIN_SIZE;
  for (int z = 0; z < size; ++z) {
    for (int x = 0; x < size; ++x) {
      Vertex v;
      float h = heightmap[z * size + x];
      // Final Approved Mapping (matches user feedback "it's correct now"):
      // MU_Y (z) maps to World X -> North is Forward (+X)
      // MU_X (x) maps to World Z -> West is Left (+Z is Right)
      v.position = glm::vec3(static_cast<float>(z) * 100.0f, h,
                             static_cast<float>(x) * 100.0f);
      v.texCoord =
          glm::vec2(static_cast<float>(x) / size, static_cast<float>(z) / size);

      // Use lightmap for vertex color
      if (z * size + x < lightmap.size()) {
        v.color = lightmap[z * size + x];
      } else {
        v.color = glm::vec3(1.0f, 1.0f, 1.0f);
      }

      vertices.push_back(v);
    }
  }

  // Original engine triangulation uses diagonal from (x,z) to (x+1,z+1).
  // Match that triangulation for consistent lightmap/height interpolation.
  for (int z = 0; z < size - 1; ++z) {
    for (int x = 0; x < size - 1; ++x) {
      int current = z * size + x; // (x, z)
      int next = current + size;  // (x, z+1)

      // Triangle 1: (x,z) -> (x+1,z) -> (x+1,z+1)
      indices.push_back(current);
      indices.push_back(current + 1);
      indices.push_back(next + 1);

      // Triangle 2: (x,z) -> (x+1,z+1) -> (x,z+1)
      indices.push_back(current);
      indices.push_back(next + 1);
      indices.push_back(next);
    }
  }

  indexCount = indices.size();

  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);
  glGenBuffers(1, &EBO);

  glBindVertexArray(VAO);

  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  // Position
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)0);
  glEnableVertexAttribArray(0);
  // TexCoords
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, texCoord));
  glEnableVertexAttribArray(1);
  // Color
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, color));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);
}

void Terrain::setupTextures(const TerrainData &data,
                            const std::string &base_path) {
  const int size = TerrainParser::TERRAIN_SIZE;

  // Layer 1 index map (NEAREST - one tile index per cell)
  glGenTextures(1, &layer1InfoMap);
  glBindTexture(GL_TEXTURE_2D, layer1InfoMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size, size, 0, GL_RED, GL_UNSIGNED_BYTE,
               data.mapping.layer1.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Layer 2 index map
  glGenTextures(1, &layer2InfoMap);
  glBindTexture(GL_TEXTURE_2D, layer2InfoMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size, size, 0, GL_RED, GL_UNSIGNED_BYTE,
               data.mapping.layer2.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Alpha blend map (LINEAR for smooth transitions between tiles)
  glGenTextures(1, &alphaMap);
  glBindTexture(GL_TEXTURE_2D, alphaMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, size, size, 0, GL_RED, GL_FLOAT,
               data.mapping.alpha.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Attribute map (collision, water flags etc.)
  glGenTextures(1, &attributeMap);
  glBindTexture(GL_TEXTURE_2D, attributeMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, size, size, 0, GL_RED_INTEGER,
               GL_UNSIGNED_BYTE, data.mapping.attributes.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // Symmetry map (ATT high byte: per-tile flip/rotate flags)
  glGenTextures(1, &symmetryMap);
  glBindTexture(GL_TEXTURE_2D, symmetryMap);
  if (data.mapping.symmetry.empty()) {
    std::vector<uint8_t> zeroSym(size * size, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, size, size, 0, GL_RED_INTEGER,
                 GL_UNSIGNED_BYTE, zeroSym.data());
  } else {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, size, size, 0, GL_RED_INTEGER,
                 GL_UNSIGNED_BYTE, data.mapping.symmetry.data());
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Lightmap texture (bilinear interpolation, no triangle diagonal kinks)
  glGenTextures(1, &lightmapTex);
  glBindTexture(GL_TEXTURE_2D, lightmapTex);
  // Convert vec3 lightmap to float RGB for upload
  std::vector<float> lightRGB(size * size * 3);
  for (int i = 0; i < size * size; ++i) {
    if (i < (int)data.lightmap.size()) {
      lightRGB[i * 3 + 0] = data.lightmap[i].r;
      lightRGB[i * 3 + 1] = data.lightmap[i].g;
      lightRGB[i * 3 + 2] = data.lightmap[i].b;
    } else {
      lightRGB[i * 3 + 0] = 1.0f;
      lightRGB[i * 3 + 1] = 1.0f;
      lightRGB[i * 3 + 2] = 1.0f;
    }
  }
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, size, size, 0, GL_RGB, GL_FLOAT,
               lightRGB.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Save baseline lightmap for per-frame dynamic light application
  m_baselineLightRGB = lightRGB;

  // Tile texture array - native client loads up to 30 tiles per world
  // (14 base + 16 ExtTile overlays)
  const int tile_res = 256;
  const int max_tiles = 32;

  glGenTextures(1, &tileTextureArray);
  glBindTexture(GL_TEXTURE_2D_ARRAY, tileTextureArray);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, tile_res, tile_res, max_tiles,
               0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  // Standard bitmap loading parameters: GL_NEAREST + GL_REPEAT
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

  // Fill all slots with neutral dark brown so unloaded tiles blend with terrain
  std::vector<unsigned char> neutral(tile_res * tile_res * 3);
  for (int p = 0; p < tile_res * tile_res; ++p) {
    neutral[p * 3 + 0] = 80; // R
    neutral[p * 3 + 1] = 70; // G
    neutral[p * 3 + 2] = 55; // B
  }
  for (int slot = 0; slot < max_tiles; ++slot) {
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, slot, tile_res, tile_res, 1,
                    GL_RGB, GL_UNSIGNED_BYTE, neutral.data());
  }

  // Lorencia tile set - matches relative tile loading order:
  // Indices 0-13: base tiles, 14-29: ExtTile01-16 (road/detail overlays)
  std::vector<std::string> tile_names;
  if (worldID == 1) {
    tile_names = {
        "TileGrass01",  // 0
        "TileGrass02",  // 1
        "TileGround01", // 2
        "TileGround02", // 3
        "TileGround03", // 4
        "TileWater01",  // 5
        "TileWood01",   // 6
        "TileRock01",   // 7
        "TileRock02",   // 8
        "TileRock03",   // 9
        "TileRock04",   // 10
        "TileRock05",   // 11
        "TileRock06",   // 12
        "TileRock07",   // 13
        "ExtTile01",    // 14
        "ExtTile02",    // 15
        "ExtTile03",    // 16
        "ExtTile04",    // 17
        "ExtTile05",    // 18
        "ExtTile06",    // 19
        "ExtTile07",    // 20
        "ExtTile08",    // 21
        "ExtTile09",    // 22
        "ExtTile10",    // 23
        "ExtTile11",    // 24
        "ExtTile12",    // 25
        "ExtTile13",    // 26
        "ExtTile14",    // 27
        "ExtTile15",    // 28
        "ExtTile16",    // 29
    };
  }

  // Scan which tile indices are actually referenced by the mapping data
  std::set<int> usedTileIndices;
  const size_t cells =
      TerrainParser::TERRAIN_SIZE * TerrainParser::TERRAIN_SIZE;
  for (size_t i = 0; i < cells && i < data.mapping.layer1.size(); ++i) {
    usedTileIndices.insert(data.mapping.layer1[i]);
    if (data.mapping.alpha[i] > 0.0f)
      usedTileIndices.insert(data.mapping.layer2[i]);
  }

  // Log which tile indices are used (detect out-of-range indices)
  for (int idx : usedTileIndices) {
    if (idx >= (int)tile_names.size()) {
      std::cerr << "[Terrain] WARNING: Terrain uses tile index " << idx
                << " which has no tile name (max=" << tile_names.size() - 1
                << ")" << std::endl;
    }
  }

  // Cache first successfully loaded texture data as fallback for missing tiles
  std::vector<unsigned char> fallback_data;
  int fallback_w = 0, fallback_h = 0;

  for (int i = 0; i < (int)tile_names.size(); ++i) {
    // Skip tiles not referenced by the terrain mapping
    if (usedTileIndices.find(i) == usedTileIndices.end()) {
      continue;
    }

    int w = 0, h = 0;
    std::vector<unsigned char> raw_data;

    // Try OZJ first, then jpg (native uses .jpg, our data has .OZJ)
    std::vector<std::string> extensions = {".OZJ", ".jpg", ".OZT"};
    for (const auto &ext : extensions) {
      std::string path = base_path + "/" + tile_names[i] + ext;
      if (ext == ".OZT") {
        // OZT needs raw loader too - skip for now, OZJ/jpg cover Lorencia
        continue;
      }
      raw_data = TextureLoader::LoadOZJRaw(path, w, h);
      if (!raw_data.empty())
        break;
    }

    if (raw_data.empty()) {
      // Use fallback texture for missing tiles
      if (!fallback_data.empty()) {
        w = fallback_w;
        h = fallback_h;
        raw_data = fallback_data;
        std::cerr << "[Terrain] Tile " << i << " (" << tile_names[i]
                  << ") missing, using fallback" << std::endl;
      } else {
        std::cerr << "[Terrain] Tile " << i << " (" << tile_names[i]
                  << ") missing, no fallback available" << std::endl;
        continue;
      }
    } else {
      std::cout << "[Terrain] Loaded tile " << i << ": " << tile_names[i]
                << " (" << w << "x" << h << ")" << std::endl;
      // Cache first successful load as fallback
      if (fallback_data.empty()) {
        fallback_data = raw_data;
        fallback_w = w;
        fallback_h = h;
      }
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, tileTextureArray);
    GLenum format = (raw_data.size() == (size_t)w * h * 4) ? GL_RGBA : GL_RGB;

    if (w == tile_res && h == tile_res) {
      glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, w, h, 1, format,
                      GL_UNSIGNED_BYTE, raw_data.data());
    } else if (w == 128 && h == 128) {
      // 128x128 tiles: simple 2x2 copy to fill 256x256 array slot.
      int bpp = (raw_data.size() == (size_t)w * h * 4) ? 4 : 3;
      std::vector<unsigned char> tiled(256 * 256 * bpp);
      for (int y = 0; y < 256; ++y) {
        int sy = y % 128;
        for (int x = 0; x < 256; ++x) {
          int sx = x % 128;
          memcpy(&tiled[(y * 256 + x) * bpp], &raw_data[(sy * 128 + sx) * bpp],
                 bpp);
        }
      }
      glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, 256, 256, 1, format,
                      GL_UNSIGNED_BYTE, tiled.data());
    } else if (w > 0 && h > 0) {
      // Non-standard size: upload what we have (top-left corner)
      int upload_w = std::min(w, tile_res);
      int upload_h = std::min(h, tile_res);
      glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, upload_w, upload_h, 1,
                      format, GL_UNSIGNED_BYTE, raw_data.data());
    }
  }
}

void Terrain::applyDynamicLights() {
  if (m_baselineLightRGB.empty())
    return;

  const int S = TerrainParser::TERRAIN_SIZE; // 256

  // Reset working lightmap from baseline
  m_workingLightRGB = m_baselineLightRGB;

  // Add each point light to the grid (matching AddTerrainLight from
  // ZzzLodTerrain.cpp) Original: linear falloff, cell range 1-3, additive
  // blending
  for (int li = 0; li < plCount; ++li) {
    // Convert world position to grid coordinates
    // WorldX → MU_Y → grid z (outer), WorldZ → MU_X → grid x (inner)
    float gx = plPositions[li].z / 100.0f;
    float gz = plPositions[li].x / 100.0f;

    const int cellRange = 3; // Original uses range 3 for most light types
    float rf = (float)cellRange;

    // Scale colors to match original intensity (original uses ~0.3-0.7 range)
    glm::vec3 color = plColors[li] * 0.35f;

    int gxi = (int)gx;
    int gzi = (int)gz;

    for (int sz = gzi - cellRange; sz <= gzi + cellRange; ++sz) {
      if (sz < 0 || sz >= S)
        continue;
      for (int sx = gxi - cellRange; sx <= gxi + cellRange; ++sx) {
        if (sx < 0 || sx >= S)
          continue;

        float xd = gx - (float)sx;
        float zd = gz - (float)sz;
        float dist = sqrtf(xd * xd + zd * zd);
        float lf = (rf - dist) / rf; // Linear falloff (matches original)
        if (lf <= 0.0f)
          continue;

        int idx = sz * S + sx;
        m_workingLightRGB[idx * 3 + 0] += color.r * lf;
        m_workingLightRGB[idx * 3 + 1] += color.g * lf;
        m_workingLightRGB[idx * 3 + 2] += color.b * lf;
      }
    }
  }

  // Re-upload modified lightmap texture
  glBindTexture(GL_TEXTURE_2D, lightmapTex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, S, S, GL_RGB, GL_FLOAT,
                  m_workingLightRGB.data());
}

void Terrain::setupShader() {
  auto compileShader = [](GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
      glGetShaderInfoLog(shader, 512, nullptr, infoLog);
      std::cerr << "Shader Compilation Error: " << infoLog << std::endl;
    }
    return shader;
  };

  GLuint vertex = compileShader(GL_VERTEX_SHADER, terrainVertexShaderSource);
  GLuint fragment =
      compileShader(GL_FRAGMENT_SHADER, terrainFragmentShaderSource);

  shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vertex);
  glAttachShader(shaderProgram, fragment);
  glLinkProgram(shaderProgram);

  int success;
  char infoLog[512];
  glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
    std::cerr << "Shader Linking Error: " << infoLog << std::endl;
  }

  glDeleteShader(vertex);
  glDeleteShader(fragment);
}
