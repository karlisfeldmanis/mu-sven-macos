#include "ObjectRenderer.hpp"
#include "TextureLoader.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

// BlendMesh texture ID lookup — returns the BMD texture slot index
// that identifies the "window light" mesh for each object type.
// Returns -1 if the object type has no BlendMesh.
// Source: reference ZzzObject.cpp OpenObjectsEnc / CreateObject
static int GetBlendMeshTexId(int type) {
  switch (type) {
  case 117:
    return 4; // House03 — window glow (flicker)
  case 118:
    return 8; // House04 — window glow (flicker + UV scroll)
  case 119:
    return 2; // House05 — window glow (flicker + UV scroll)
  case 122:
    return 4; // HouseWall02 — window glow (flicker)
  case 52:
    return 1; // Bonfire01 — fire glow
  case 90:
    return 1; // StreetLight01 — lamp glow
  case 150:
    return 1; // Candle01 — candle glow
  case 98:
    return 2; // Carriage01 — lantern glow
  case 105:
    return 3; // Waterspout01 — water UV scroll
  default:
    return -1;
  }
}

// Type-to-filename mapping based on reference _enum.h + MapManager.cpp
// AccessModel(TYPE, dir, "BaseName", index):
//   index == -1 → BaseName.bmd
//   index < 10  → BaseName0X.bmd
//   else        → BaseNameX.bmd
std::string ObjectRenderer::GetObjectBMDFilename(int type) {
  struct TypeEntry {
    int baseType;
    const char *baseName;
    int startIndex; // 1-based index for first item, -1 for no index
  };

  // Ranges: type = baseType + offset → BaseName(startIndex + offset).bmd
  static const TypeEntry ranges[] = {
      {0, "Tree", 1},            // 0-19: Tree01-Tree20
      {20, "Grass", 1},          // 20-29: Grass01-Grass10
      {30, "Stone", 1},          // 30-39: Stone01-Stone10
      {40, "StoneStatue", 1},    // 40-42: StoneStatue01-03
      {43, "SteelStatue", 1},    // 43: SteelStatue01
      {44, "Tomb", 1},           // 44-46: Tomb01-03
      {50, "FireLight", 1},      // 50-51: FireLight01-02
      {52, "Bonfire", 1},        // 52: Bonfire01
      {55, "DoungeonGate", 1},   // 55: DoungeonGate01
      {56, "MerchantAnimal", 1}, // 56-57: MerchantAnimal01-02
      {58, "TreasureDrum", 1},   // 58: TreasureDrum01
      {59, "TreasureChest", 1},  // 59: TreasureChest01
      {60, "Ship", 1},           // 60: Ship01
      {65, "SteelWall", 1},      // 65-67: SteelWall01-03
      {68, "SteelDoor", 1},      // 68: SteelDoor01
      {69, "StoneWall", 1},      // 69-74: StoneWall01-06
      {75, "StoneMuWall", 1},    // 75-78: StoneMuWall01-04
      {80, "Bridge", 1},         // 80: Bridge01
      {81, "Fence", 1},          // 81-84: Fence01-04
      {85, "BridgeStone", 1},    // 85: BridgeStone01
      {90, "StreetLight", 1},    // 90: StreetLight01
      {91, "Cannon", 1},         // 91-93: Cannon01-03
      {95, "Curtain", 1},        // 95: Curtain01
      {96, "Sign", 1},           // 96-97: Sign01-02
      {98, "Carriage", 1},       // 98-101: Carriage01-04
      {102, "Straw", 1},         // 102-103: Straw01-02
      {105, "Waterspout", 1},    // 105: Waterspout01
      {106, "Well", 1},          // 106-109: Well01-04
      {110, "Hanging", 1},       // 110: Hanging01
      {111, "Stair", 1},         // 111: Stair01
      {115, "House", 1},         // 115-119: House01-05
      {120, "Tent", 1},          // 120: Tent01
      {121, "HouseWall", 1},     // 121-126: HouseWall01-06
      {127, "HouseEtc", 1},      // 127-129: HouseEtc01-03
      {130, "Light", 1},         // 130-132: Light01-03
      {133, "PoseBox", 1},       // 133: PoseBox01
      {140, "Furniture", 1},     // 140-146: Furniture01-07
      {150, "Candle", 1},        // 150: Candle01
      {151, "Beer", 1},          // 151-153: Beer01-03
  };

  static const int numRanges = sizeof(ranges) / sizeof(ranges[0]);

  // Find the range that contains this type (search backwards to find best
  // match)
  const TypeEntry *best = nullptr;
  for (int i = 0; i < numRanges; ++i) {
    if (type >= ranges[i].baseType) {
      best = &ranges[i];
    }
  }

  if (!best || type < best->baseType)
    return "";

  int offset = type - best->baseType;
  int index = best->startIndex + offset;

  // Format: BaseName0X.bmd (zero-padded for index < 10)
  char buf[64];
  if (index < 10)
    snprintf(buf, sizeof(buf), "%s0%d.bmd", best->baseName, index);
  else
    snprintf(buf, sizeof(buf), "%s%d.bmd", best->baseName, index);

  return buf;
}

void ObjectRenderer::Init() {
  // Try project root first (./build/MuRemaster), then build dir (cd build &&
  // ./MuRemaster)
  std::ifstream test("shaders/model.vert");
  if (test.good()) {
    shader =
        std::make_unique<Shader>("shaders/model.vert", "shaders/model.frag");
  } else {
    shader = std::make_unique<Shader>("../shaders/model.vert",
                                      "../shaders/model.frag");
  }
}

void ObjectRenderer::UploadMesh(const Mesh_t &mesh, const std::string &baseDir,
                                const std::vector<BoneWorldMatrix> &bones,
                                std::vector<MeshBuffers> &out, bool dynamic) {
  MeshBuffers mb;
  mb.texture = 0;

  struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 tex;
  };
  std::vector<Vertex> vertices;
  std::vector<unsigned int> indices;

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    int startIdx = vertices.size();
    for (int v = 0; v < 3; ++v) {
      Vertex vert;
      auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
      auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

      int boneIdx = srcVert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        const auto &bm = bones[boneIdx];
        vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                          srcVert.Position);
        vert.normal =
            MuMath::RotateVector((const float(*)[4])bm.data(), srcNorm.Normal);
      } else {
        vert.pos = srcVert.Position;
        vert.normal = srcNorm.Normal;
      }

      vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                           mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
      vertices.push_back(vert);
      indices.push_back(startIdx + v);
    }
    if (steps == 4) {
      int quadIndices[3] = {0, 2, 3};
      for (int v : quadIndices) {
        Vertex vert;
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

        int boneIdx = srcVert.Node;
        if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
          const auto &bm = bones[boneIdx];
          vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                            srcVert.Position);
          vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(),
                                             srcNorm.Normal);
        } else {
          vert.pos = srcVert.Position;
          vert.normal = srcNorm.Normal;
        }

        vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                             mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
        vertices.push_back(vert);
        indices.push_back(vertices.size() - 1);
      }
    }
  }

  mb.indexCount = indices.size();
  mb.vertexCount = vertices.size();
  mb.isDynamic = dynamic;
  if (mb.indexCount == 0) {
    out.push_back(mb);
    return;
  }

  GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;

  glGenVertexArrays(1, &mb.vao);
  glGenBuffers(1, &mb.vbo);
  glGenBuffers(1, &mb.ebo);

  glBindVertexArray(mb.vao);
  glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), usage);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mb.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)(sizeof(float) * 3));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)(sizeof(float) * 6));
  glEnableVertexAttribArray(2);

  auto texResult = TextureLoader::ResolveWithInfo(baseDir, mesh.TextureName);
  mb.texture = texResult.textureID;
  mb.hasAlpha = texResult.hasAlpha;
  mb.textureName = mesh.TextureName;

  auto scriptFlags = TextureLoader::ParseScriptFlags(mesh.TextureName);
  mb.noneBlend = scriptFlags.noneBlend;
  mb.hidden = scriptFlags.hidden;
  mb.bright = scriptFlags.bright;
  mb.bmdTextureId =
      mesh.Texture; // Store BMD texture slot for BlendMesh matching

  out.push_back(mb);
}

void ObjectRenderer::LoadObjects(const std::vector<ObjectData> &objects,
                                 const std::string &objectDir) {
  int skipped = 0;

  for (auto &obj : objects) {
    std::string filename = GetObjectBMDFilename(obj.type);
    if (filename.empty()) {
      ++skipped;
      continue;
    }

    // Load model into cache if not already loaded
    if (modelCache.find(obj.type) == modelCache.end()) {
      std::string fullPath = objectDir + "/" + filename;
      auto bmd = BMDParser::Parse(fullPath);
      if (!bmd) {
        // Mark as failed so we don't retry
        modelCache[obj.type] = ModelCache{};
        ++skipped;
        continue;
      }

      ModelCache cache;
      cache.boneMatrices = ComputeBoneMatrices(bmd.get());
      cache.blendMeshTexId = GetBlendMeshTexId(obj.type);

      // Detect animated models (>1 keyframe in first action)
      // Only enable CPU re-skinning for select types — trees (0-19) and
      // stone walls have too many instances for per-frame vertex transforms.
      auto shouldAnimate = [](int t) {
        // Cloth, signs, animals, mechanical, decorative — low instance count
        return t == 56 || t == 57 ||   // MerchantAnimal01-02
               t == 59 ||              // TreasureChest01
               t == 60 ||              // Ship01
               t == 90 ||              // StreetLight01
               t == 95 ||              // Curtain01
               t == 96 ||              // Sign01
               t == 98 ||              // Carriage01
               t == 105 ||             // Waterspout01
               t == 110 ||             // Hanging01
               t == 118 || t == 119 || // House04-05
               t == 120 ||             // Tent01
               t == 150;               // Candle01
      };
      if (!bmd->Actions.empty() && bmd->Actions[0].NumAnimationKeys > 1 &&
          shouldAnimate(obj.type)) {
        cache.isAnimated = true;
        cache.numAnimationKeys = bmd->Actions[0].NumAnimationKeys;
      }

      for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
        auto &mesh = bmd->Meshes[mi];
        UploadMesh(mesh, objectDir + "/", cache.boneMatrices, cache.meshBuffers,
                   cache.isAnimated);
      }

      // Mark BlendMesh meshes (window light / glow)
      if (cache.blendMeshTexId >= 0) {
        for (auto &mb : cache.meshBuffers) {
          if (mb.bmdTextureId == cache.blendMeshTexId) {
            mb.isWindowLight = true;
          }
        }
      }

      // Retain BMD data for animated types (needed for per-frame re-skinning)
      if (cache.isAnimated) {
        cache.bmdData = std::move(bmd);
        std::cout << "  [Animated] type " << obj.type
                  << " keys=" << cache.numAnimationKeys << std::endl;
      }

      modelCache[obj.type] = std::move(cache);
    }

    // Skip instances with empty model cache (failed to load)
    auto &cache = modelCache[obj.type];
    if (cache.meshBuffers.empty()) {
      ++skipped;
      continue;
    }

    // Build model matrix
    glm::mat4 model = glm::translate(glm::mat4(1.0f), obj.position);
    // MU Z-up → OpenGL Y-up coordinate conversion
    // Position maps as: GL_X=MU_Y, GL_Y=MU_Z, GL_Z=MU_X
    // Model geometry must match: Rz(-90)*Ry(-90) permutes axes correctly
    model =
        glm::rotate(model, glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    model =
        glm::rotate(model, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    // Apply MU rotation (stored as radians in obj.rotation, raw degrees in
    // mu_angle_raw)
    model = glm::rotate(model, obj.rotation.z,
                        glm::vec3(0.0f, 0.0f, 1.0f)); // MU Z rotation
    model = glm::rotate(model, obj.rotation.y,
                        glm::vec3(0.0f, 1.0f, 0.0f)); // MU Y rotation
    model = glm::rotate(model, obj.rotation.x,
                        glm::vec3(1.0f, 0.0f, 0.0f)); // MU X rotation
    model = glm::scale(model, glm::vec3(obj.scale));

    // Sample terrain lightmap at object's world position
    glm::vec3 worldPos = glm::vec3(model[3]);
    glm::vec3 tLight = SampleTerrainLight(worldPos);

    instances.push_back({obj.type, model, tLight});
  }

  std::cout << "[ObjectRenderer] Loaded " << instances.size()
            << " object instances, " << modelCache.size()
            << " unique models, skipped " << skipped << std::endl;
}

void ObjectRenderer::SetTerrainLightmap(
    const std::vector<glm::vec3> &lightmap) {
  terrainLightmap = lightmap;
}

glm::vec3 ObjectRenderer::SampleTerrainLight(const glm::vec3 &worldPos) const {
  const int SIZE = 256;
  if (terrainLightmap.size() < (size_t)(SIZE * SIZE))
    return glm::vec3(1.0f);

  // World → grid: WorldX maps to grid Z, WorldZ maps to grid X
  // (MU Y→WorldX, MU X→WorldZ; lightmap indexed as [z * SIZE + x])
  float gz = worldPos.x / 100.0f;
  float gx = worldPos.z / 100.0f;

  int xi = (int)gx;
  int zi = (int)gz;
  if (xi < 0 || zi < 0 || xi > SIZE - 2 || zi > SIZE - 2)
    return glm::vec3(0.5f);

  float xd = gx - (float)xi;
  float zd = gz - (float)zi;

  // 4 corners for bilinear interpolation
  const glm::vec3 &c00 = terrainLightmap[zi * SIZE + xi];
  const glm::vec3 &c10 = terrainLightmap[zi * SIZE + (xi + 1)];
  const glm::vec3 &c01 = terrainLightmap[(zi + 1) * SIZE + xi];
  const glm::vec3 &c11 = terrainLightmap[(zi + 1) * SIZE + (xi + 1)];

  glm::vec3 left = c00 + (c01 - c00) * zd;
  glm::vec3 right = c10 + (c11 - c10) * zd;
  return left + (right - left) * xd;
}

void ObjectRenderer::SetPointLights(const std::vector<glm::vec3> &positions,
                                    const std::vector<glm::vec3> &colors,
                                    const std::vector<float> &ranges) {
  plCount = std::min((int)positions.size(), 64);
  plPositions.assign(positions.begin(), positions.begin() + plCount);
  plColors.assign(colors.begin(), colors.begin() + plCount);
  plRanges.assign(ranges.begin(), ranges.begin() + plCount);
}

void ObjectRenderer::RetransformMesh(const Mesh_t &mesh,
                                     const std::vector<BoneWorldMatrix> &bones,
                                     MeshBuffers &mb) {
  if (!mb.isDynamic || mb.vertexCount == 0 || mb.vbo == 0)
    return;

  struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 tex;
  };
  std::vector<Vertex> vertices;
  vertices.reserve(mb.vertexCount);

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    for (int v = 0; v < 3; ++v) {
      Vertex vert;
      auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
      auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

      int boneIdx = srcVert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        const auto &bm = bones[boneIdx];
        vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                          srcVert.Position);
        vert.normal =
            MuMath::RotateVector((const float(*)[4])bm.data(), srcNorm.Normal);
      } else {
        vert.pos = srcVert.Position;
        vert.normal = srcNorm.Normal;
      }

      vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                           mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
      vertices.push_back(vert);
    }
    if (steps == 4) {
      int quadIndices[3] = {0, 2, 3};
      for (int v : quadIndices) {
        Vertex vert;
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

        int boneIdx = srcVert.Node;
        if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
          const auto &bm = bones[boneIdx];
          vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                            srcVert.Position);
          vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(),
                                             srcNorm.Normal);
        } else {
          vert.pos = srcVert.Position;
          vert.normal = srcNorm.Normal;
        }

        vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                             mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
        vertices.push_back(vert);
      }
    }
  }

  glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(Vertex),
                  vertices.data());
}

void ObjectRenderer::Render(const glm::mat4 &view, const glm::mat4 &projection,
                            const glm::vec3 &cameraPos, float currentTime) {
  if (instances.empty() || !shader)
    return;

  shader->use();
  glActiveTexture(GL_TEXTURE0);
  shader->setInt("texture_diffuse", 0);
  shader->setMat4("projection", projection);
  shader->setMat4("view", view);
  // High sun for even world-scale illumination
  shader->setVec3("lightPos", cameraPos + glm::vec3(0, 8000, 0));
  shader->setVec3("lightColor", 1.0f, 1.0f, 1.0f);
  shader->setVec3("viewPos", cameraPos);
  shader->setBool("useFog", true);
  shader->setFloat("blendMeshLight", 1.0f);
  shader->setVec2("texCoordOffset", glm::vec2(0.0f));

  // Set point light uniforms
  shader->setInt("numPointLights", plCount);
  for (int i = 0; i < plCount; ++i) {
    std::string idx = std::to_string(i);
    shader->setVec3("pointLightPos[" + idx + "]", plPositions[i]);
    shader->setVec3("pointLightColor[" + idx + "]", plColors[i]);
    shader->setFloat("pointLightRange[" + idx + "]", plRanges[i]);
  }

  // Advance skeletal animation for animated model types
  {
    const float ANIM_SPEED = 4.0f; // keyframes/sec (reference: 0.16 * 25fps)
    float dt = (lastAnimTime > 0.0f) ? (currentTime - lastAnimTime) : 0.0f;
    if (dt > 0.0f && dt < 1.0f) { // clamp to avoid huge jumps
      for (auto &[type, cache] : modelCache) {
        if (!cache.isAnimated || !cache.bmdData)
          continue;
        auto &state = animStates[type];
        state.frame += ANIM_SPEED * dt;
        if (state.frame >= (float)cache.numAnimationKeys)
          state.frame = std::fmod(state.frame, (float)cache.numAnimationKeys);

        auto bones = ComputeBoneMatricesInterpolated(cache.bmdData.get(), 0,
                                                     state.frame);
        for (int mi = 0; mi < (int)cache.meshBuffers.size() &&
                         mi < (int)cache.bmdData->Meshes.size();
             ++mi) {
          RetransformMesh(cache.bmdData->Meshes[mi], bones,
                          cache.meshBuffers[mi]);
        }
      }
    }
    lastAnimTime = currentTime;
  }

  // Compute BlendMesh animation state from time
  // Flicker: random-ish intensity between 0.4 and 0.7 (like original
  // rand()%4+4)
  float flickerBase = 0.55f + 0.15f * std::sin(currentTime * 7.3f) *
                                  std::sin(currentTime * 11.1f + 2.0f);
  // UV scroll: 1-second cycle for animated window meshes
  float uvScroll = -std::fmod(currentTime, 1.0f);

  for (auto &inst : instances) {
    // PoseBox (type 133) is an NPC interaction trigger, not a visible object
    // Light01-03 (types 130-132) are fire/smoke-only emitters (HiddenMesh=-2)
    if (inst.type == 133 || (inst.type >= 130 && inst.type <= 132))
      continue;

    auto it = modelCache.find(inst.type);
    if (it == modelCache.end())
      continue;

    shader->setMat4("model", inst.modelMatrix);
    shader->setVec3("terrainLight", inst.terrainLight);

    // Check if this model type has BlendMesh animation
    bool hasBlendMesh = it->second.blendMeshTexId >= 0;
    bool hasUVScroll =
        (inst.type == 118 || inst.type == 119 || inst.type == 105);

    for (auto &mb : it->second.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      if (mb.texture == 0)
        continue;

      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);

      if (mb.isWindowLight && hasBlendMesh) {
        // BlendMesh: additive blending with intensity flicker
        float intensity = flickerBase;
        // Bonfire has wider flicker range (0.4-0.9)
        if (inst.type == 52)
          intensity = 0.65f + 0.25f * std::sin(currentTime * 9.0f) *
                                  std::sin(currentTime * 13.7f);
        // Static glow for streetlights, candles, carriages
        if (inst.type == 90 || inst.type == 150 || inst.type == 98)
          intensity = 1.0f;

        shader->setFloat("blendMeshLight", intensity);
        if (hasUVScroll)
          shader->setVec2("texCoordOffset", glm::vec2(0.0f, uvScroll));
        else
          shader->setVec2("texCoordOffset", glm::vec2(0.0f));

        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Reset uniforms for next mesh
        shader->setFloat("blendMeshLight", 1.0f);
        shader->setVec2("texCoordOffset", glm::vec2(0.0f));
      } else if (mb.noneBlend) {
        glDisable(GL_BLEND);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        glEnable(GL_BLEND);
      } else if (mb.bright) {
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      } else {
        // Alpha meshes: disable face culling (paper-thin geometry like
        // fence bars needs both sides visible) and add depth bias to
        // avoid z-fighting with coplanar opaque meshes.
        // Matches original engine's EnableAlphaTest() → DisableCullFace().
        if (mb.hasAlpha) {
          glDisable(GL_CULL_FACE);
          glEnable(GL_POLYGON_OFFSET_FILL);
          glPolygonOffset(-1.0f, -1.0f);
          glDepthMask(GL_FALSE); // Shadows/Alpha shouldn't write depth
        }
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        if (mb.hasAlpha) {
          glDepthMask(GL_TRUE);
          glDisable(GL_POLYGON_OFFSET_FILL);
          glEnable(GL_CULL_FACE);
        }
      }
    }
  }
}

void ObjectRenderer::Cleanup() {
  for (auto &[type, cache] : modelCache) {
    for (auto &mb : cache.meshBuffers) {
      if (mb.vao)
        glDeleteVertexArrays(1, &mb.vao);
      if (mb.vbo)
        glDeleteBuffers(1, &mb.vbo);
      if (mb.ebo)
        glDeleteBuffers(1, &mb.ebo);
      if (mb.texture)
        glDeleteTextures(1, &mb.texture);
    }
  }
  modelCache.clear();
  instances.clear();
  shader.reset();
}
