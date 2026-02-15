#include "GrassRenderer.hpp"
#include "TextureLoader.hpp"
#include <cstdlib>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

// --- Embedded shaders ---

static const char *grassVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in float aWindWeight;
layout (location = 3) in float aGridX;
layout (location = 4) in vec3 aColor;
layout (location = 5) in float aTexLayer;

out vec2 TexCoord;
out vec3 VertColor;
out vec3 FragPos;
flat out int TexLayer;

uniform mat4 view;
uniform mat4 projection;
uniform float uTime;
uniform vec3 ballPos;
uniform float pushRadius;

void main() {
    vec3 pos = aPos;

    // Wind: reference WindSpeed = (WorldTime % 720000) * 0.002
    // uTime is seconds, so mod(uTime, 720) * 2.0 matches reference timing
    float windSpeed = mod(uTime, 720.0) * 2.0;
    float wind = sin(windSpeed + aGridX * 5.0) * 10.0 * aWindWeight;
    pos.x += wind;

    // Grass pushing: top vertices near ball get pushed away (original CheckGrass)
    if (aWindWeight > 0.0 && pushRadius > 0.0) {
        vec2 toBlade = pos.xz - ballPos.xz;
        float dist = length(toBlade);
        if (dist < pushRadius && dist > 0.001) {
            float pushStrength = (1.0 - dist / pushRadius);
            pushStrength *= pushStrength; // Quadratic falloff
            vec2 pushDir = normalize(toBlade);
            pos.xz += pushDir * pushStrength * pushRadius * 0.5;
            pos.y -= pushStrength * 30.0; // Slight downward bend
        }
    }

    gl_Position = projection * view * vec4(pos, 1.0);
    TexCoord = aTexCoord;
    VertColor = aColor;
    FragPos = pos;
    TexLayer = int(aTexLayer + 0.5);
}
)";

static const char *grassFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec3 VertColor;
in vec3 FragPos;
flat in int TexLayer;

uniform sampler2D grassTex0;
uniform sampler2D grassTex1;
uniform sampler2D grassTex2;
uniform vec3 viewPos;

void main() {
    vec4 color;
    if (TexLayer == 1) color = texture(grassTex1, TexCoord);
    else if (TexLayer == 2) color = texture(grassTex2, TexCoord);
    else color = texture(grassTex0, TexCoord);

    if (color.a < 0.25) discard; // Match original glAlphaFunc(GL_GREATER, 0.25)

    vec3 lit = color.rgb * VertColor;

    // Fog matching terrain shader parameters
    float dist = length(FragPos - viewPos);
    float fogFactor = clamp((3500.0 - dist) / (3500.0 - 1500.0), 0.0, 1.0);
    vec3 fogColor = vec3(0.117, 0.078, 0.039);
    lit = mix(fogColor, lit, fogFactor);

    // Edge fog (same as terrain)
    float edgeWidth = 2500.0;
    float edgeMargin = 500.0;
    float terrainMax = 25600.0;
    float dEdge = min(min(FragPos.x, terrainMax - FragPos.x),
                      min(FragPos.z, terrainMax - FragPos.z));
    float edgeFactor = smoothstep(edgeMargin, edgeMargin + edgeWidth, dEdge);
    lit = mix(vec3(0.0), lit, edgeFactor);

    FragColor = vec4(lit, color.a);
}
)";

// --- Constants ---

// Reference: Height = pBitmap->Height * 2.0f; TileGrass01/02 are 256x64, so 64*2=128
static constexpr float GRASS_HEIGHT = 128.0f;
// Reference: Width = 64.0f / 256.0f = 0.25 (four grass blade variants per texture)
static constexpr float UV_WIDTH = 64.0f / 256.0f;

// --- Implementation ---

void GrassRenderer::Init() { setupShader(); }

void GrassRenderer::setupShader() {
  auto compile = [](GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int success;
    char log[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
      glGetShaderInfoLog(shader, 512, nullptr, log);
      std::cerr << "[GrassRenderer] Shader error: " << log << std::endl;
    }
    return shader;
  };

  GLuint vert = compile(GL_VERTEX_SHADER, grassVertexShaderSource);
  GLuint frag = compile(GL_FRAGMENT_SHADER, grassFragmentShaderSource);

  shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vert);
  glAttachShader(shaderProgram, frag);
  glLinkProgram(shaderProgram);

  int success;
  char log[512];
  glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(shaderProgram, 512, nullptr, log);
    std::cerr << "[GrassRenderer] Link error: " << log << std::endl;
  }

  glDeleteShader(vert);
  glDeleteShader(frag);
}

void GrassRenderer::Load(const TerrainData &data, int worldID,
                          const std::string &dataPath) {
  const int SIZE = TerrainParser::TERRAIN_SIZE; // 256

  // Load grass textures (OZT for alpha, fall back to OZJ)
  std::string worldDir = dataPath + "/World" + std::to_string(worldID);
  const char *names[3] = {"TileGrass01", "TileGrass02", "TileGrass03"};
  for (int i = 0; i < 3; ++i) {
    std::string ozt = worldDir + "/" + names[i] + ".OZT";
    grassTextures[i] = TextureLoader::LoadOZT(ozt);
    if (grassTextures[i] == 0) {
      std::string ozj = worldDir + "/" + names[i] + ".OZJ";
      grassTextures[i] = TextureLoader::LoadOZJ(ozj);
    }
    if (grassTextures[i] != 0) {
      std::cout << "[GrassRenderer] Loaded " << names[i] << std::endl;
    } else {
      std::cerr << "[GrassRenderer] Failed to load " << names[i] << std::endl;
    }
  }

  // Generate grass billboard quads for grass terrain cells
  // Single billboard per cell (SW→NE diagonal) matching original engine
  std::vector<GrassVertex> vertices;
  std::vector<unsigned int> indices;

  // Seed RNG for per-row UV randomization (matches reference TerrainGrassTexture)
  srand(42);

  // Pre-compute per-row random UV offsets (reference: TerrainGrassTexture[yi])
  float rowUVOffsets[256];
  for (int z = 0; z < SIZE; ++z) {
    rowUVOffsets[z] = (float)(rand() % 4) / 4.0f;
  }

  for (int z = 0; z < SIZE - 1; ++z) {
    for (int x = 0; x < SIZE - 1; ++x) {
      int idx = z * SIZE + x;

      // Only grass tiles (layer1 index 0 or 1)
      uint8_t layer1 = data.mapping.layer1[idx];
      if (layer1 != 0 && layer1 != 1)
        continue;

      // Skip cells with alpha blending (overlay covers grass)
      if (data.mapping.alpha[idx] > 0.0f)
        continue;

      // Skip cells with NOGROUND attribute (under bridges/structures)
      if (idx < (int)data.mapping.attributes.size() &&
          (data.mapping.attributes[idx] & 0x08) != 0)
        continue;

      // Skip cells with large height discontinuity to neighbors
      // (fountain edges, cliff edges, structural boundaries)
      float hCenter = data.heightmap[idx];
      bool steep = false;
      const int neighbors[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
      for (auto &n : neighbors) {
        int nz = z + n[0], nx = x + n[1];
        if (nz >= 0 && nz < SIZE && nx >= 0 && nx < SIZE) {
          float hNeighbor = data.heightmap[nz * SIZE + nx];
          if (std::abs(hCenter - hNeighbor) > 40.0f) {
            steep = true;
            break;
          }
        }
      }
      if (steep)
        continue;

      // Get heightmap at SW and NE corners (diagonal billboard)
      float h_sw = data.heightmap[z * SIZE + x];
      float h_ne = data.heightmap[(z + 1) * SIZE + (x + 1)];

      // UV strip: reference uses su = xf * Width + TerrainGrassTexture[yi]
      // This cycles through 4 blade variants per column, plus row randomization
      float su = (float)x * UV_WIDTH + rowUVOffsets[z];
      // Wrap to [0, 1) range
      su = su - floorf(su);
      float uLeft = su;
      float uRight = su + UV_WIDTH;

      // Lightmap color at this cell
      glm::vec3 lightColor(1.0f);
      if (idx < (int)data.lightmap.size()) {
        lightColor = data.lightmap[idx];
      }

      float texLayer = (float)layer1;

      // Single billboard: SW → NE diagonal (matches reference VectorCopy)
      // Reference: v[3]=SW(ground), v[2]=NE(ground), v[0]=SW(top), v[1]=NE(top)
      // Top vertices: Z += Height, X -= 50, Y += wind
      unsigned int baseIdx = (unsigned int)vertices.size();

      glm::vec3 posSW((float)z * 100.0f, h_sw, (float)x * 100.0f);
      glm::vec3 posNE((float)(z + 1) * 100.0f, h_ne,
                      (float)(x + 1) * 100.0f);

      // Bottom-left (SW, anchored at terrain)
      GrassVertex bl;
      bl.position = posSW;
      bl.texCoord = glm::vec2(uLeft, 1.0f); // V=1 at base
      bl.windWeight = 0.0f;
      bl.gridX = (float)x;
      bl.color = lightColor;
      bl.texLayer = texLayer;
      vertices.push_back(bl);

      // Bottom-right (NE, anchored at terrain)
      GrassVertex br;
      br.position = posNE;
      br.texCoord = glm::vec2(uRight, 1.0f);
      br.windWeight = 0.0f;
      br.gridX = (float)(x + 1);
      br.color = lightColor;
      br.texLayer = texLayer;
      vertices.push_back(br);

      // Top-right (NE, elevated + wind)
      // Reference: MU_Z += Height, MU_X -= 50 → GL: Y += Height, Z -= 50
      GrassVertex tr;
      tr.position =
          glm::vec3(posNE.x, posNE.y + GRASS_HEIGHT, posNE.z - 50.0f);
      tr.texCoord = glm::vec2(uRight, 0.0f); // V=0 at tips
      tr.windWeight = 1.0f;
      tr.gridX = (float)(x + 1);
      tr.color = lightColor;
      tr.texLayer = texLayer;
      vertices.push_back(tr);

      // Top-left (SW, elevated + wind)
      GrassVertex tl;
      tl.position =
          glm::vec3(posSW.x, posSW.y + GRASS_HEIGHT, posSW.z - 50.0f);
      tl.texCoord = glm::vec2(uLeft, 0.0f);
      tl.windWeight = 1.0f;
      tl.gridX = (float)x;
      tl.color = lightColor;
      tl.texLayer = texLayer;
      vertices.push_back(tl);

      indices.push_back(baseIdx + 0);
      indices.push_back(baseIdx + 1);
      indices.push_back(baseIdx + 2);
      indices.push_back(baseIdx + 0);
      indices.push_back(baseIdx + 2);
      indices.push_back(baseIdx + 3);
    }
  }

  indexCount = (int)indices.size();
  std::cout << "[GrassRenderer] Generated " << (indexCount / 6)
            << " grass billboards (" << vertices.size() << " vertices)"
            << std::endl;

  if (indexCount == 0)
    return;

  // Upload to GPU
  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);
  glGenBuffers(1, &EBO);

  glBindVertexArray(VAO);

  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GrassVertex),
               vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  // position (vec3)
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GrassVertex),
                        (void *)offsetof(GrassVertex, position));
  glEnableVertexAttribArray(0);

  // texCoord (vec2)
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GrassVertex),
                        (void *)offsetof(GrassVertex, texCoord));
  glEnableVertexAttribArray(1);

  // windWeight (float)
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(GrassVertex),
                        (void *)offsetof(GrassVertex, windWeight));
  glEnableVertexAttribArray(2);

  // gridX (float)
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(GrassVertex),
                        (void *)offsetof(GrassVertex, gridX));
  glEnableVertexAttribArray(3);

  // color (vec3)
  glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(GrassVertex),
                        (void *)offsetof(GrassVertex, color));
  glEnableVertexAttribArray(4);

  // texLayer (float)
  glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(GrassVertex),
                        (void *)offsetof(GrassVertex, texLayer));
  glEnableVertexAttribArray(5);

  glBindVertexArray(0);
}

void GrassRenderer::Render(const glm::mat4 &view, const glm::mat4 &projection,
                           float time, const glm::vec3 &viewPos,
                           const glm::vec3 &ballPos) {
  if (indexCount == 0 || shaderProgram == 0)
    return;

  glUseProgram(shaderProgram);

  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE,
                     glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1,
                     GL_FALSE, glm::value_ptr(projection));
  glUniform1f(glGetUniformLocation(shaderProgram, "uTime"), time);
  glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"), 1,
               glm::value_ptr(viewPos));
  glUniform3fv(glGetUniformLocation(shaderProgram, "ballPos"), 1,
               glm::value_ptr(ballPos));
  glUniform1f(glGetUniformLocation(shaderProgram, "pushRadius"), 150.0f);

  // Bind grass textures
  for (int i = 0; i < 3; ++i) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, grassTextures[i] ? grassTextures[i] : 0);
  }
  glUniform1i(glGetUniformLocation(shaderProgram, "grassTex0"), 0);
  glUniform1i(glGetUniformLocation(shaderProgram, "grassTex1"), 1);
  glUniform1i(glGetUniformLocation(shaderProgram, "grassTex2"), 2);

  // Render state: alpha blend, no face culling (both sides visible)
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);

  glBindVertexArray(VAO);
  glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  glEnable(GL_CULL_FACE);
}

void GrassRenderer::Cleanup() {
  if (VAO)
    glDeleteVertexArrays(1, &VAO);
  if (VBO)
    glDeleteBuffers(1, &VBO);
  if (EBO)
    glDeleteBuffers(1, &EBO);
  for (int i = 0; i < 3; ++i) {
    if (grassTextures[i])
      glDeleteTextures(1, &grassTextures[i]);
  }
  if (shaderProgram)
    glDeleteProgram(shaderProgram);
  VAO = VBO = EBO = shaderProgram = 0;
  indexCount = 0;
}
