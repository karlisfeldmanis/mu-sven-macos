#include "ObjectRenderer.hpp"
#include "TextureLoader.hpp"
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

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
      {0, "Tree", 1},             // 0-19: Tree01-Tree20
      {20, "Grass", 1},           // 20-29: Grass01-Grass10
      {30, "Stone", 1},           // 30-39: Stone01-Stone10
      {40, "StoneStatue", 1},     // 40-42: StoneStatue01-03
      {43, "SteelStatue", 1},     // 43: SteelStatue01
      {44, "Tomb", 1},            // 44-46: Tomb01-03
      {50, "FireLight", 1},       // 50-51: FireLight01-02
      {52, "Bonfire", 1},         // 52: Bonfire01
      {55, "DoungeonGate", 1},    // 55: DoungeonGate01
      {56, "MerchantAnimal", 1},  // 56-57: MerchantAnimal01-02
      {58, "TreasureDrum", 1},    // 58: TreasureDrum01
      {59, "TreasureChest", 1},   // 59: TreasureChest01
      {60, "Ship", 1},            // 60: Ship01
      {65, "SteelWall", 1},       // 65-67: SteelWall01-03
      {68, "SteelDoor", 1},       // 68: SteelDoor01
      {69, "StoneWall", 1},       // 69-74: StoneWall01-06
      {75, "StoneMuWall", 1},     // 75-78: StoneMuWall01-04
      {80, "Bridge", 1},          // 80: Bridge01
      {81, "Fence", 1},           // 81-84: Fence01-04
      {85, "BridgeStone", 1},     // 85: BridgeStone01
      {90, "StreetLight", 1},     // 90: StreetLight01
      {91, "Cannon", 1},          // 91-93: Cannon01-03
      {95, "Curtain", 1},         // 95: Curtain01
      {96, "Sign", 1},            // 96-97: Sign01-02
      {98, "Carriage", 1},        // 98-101: Carriage01-04
      {102, "Straw", 1},          // 102-103: Straw01-02
      {105, "Waterspout", 1},     // 105: Waterspout01
      {106, "Well", 1},           // 106-109: Well01-04
      {110, "Hanging", 1},        // 110: Hanging01
      {111, "Stair", 1},          // 111: Stair01
      {115, "House", 1},          // 115-119: House01-05
      {120, "Tent", 1},           // 120: Tent01
      {121, "HouseWall", 1},      // 121-126: HouseWall01-06
      {127, "HouseEtc", 1},       // 127-129: HouseEtc01-03
      {130, "Light", 1},          // 130-132: Light01-03
      {133, "PoseBox", 1},        // 133: PoseBox01
      {140, "Furniture", 1},      // 140-146: Furniture01-07
      {150, "Candle", 1},         // 150: Candle01
      {151, "Beer", 1},           // 151-153: Beer01-03
  };

  static const int numRanges = sizeof(ranges) / sizeof(ranges[0]);

  // Find the range that contains this type (search backwards to find best match)
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
  shader = std::make_unique<Shader>("../shaders/model.vert",
                                    "../shaders/model.frag");
}

void ObjectRenderer::UploadMesh(const Mesh_t &mesh, const std::string &baseDir,
                                const std::vector<BoneWorldMatrix> &bones,
                                std::vector<MeshBuffers> &out) {
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
        vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(),
                                           srcNorm.Normal);
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
  if (mb.indexCount == 0) {
    out.push_back(mb);
    return;
  }

  glGenVertexArrays(1, &mb.vao);
  glGenBuffers(1, &mb.vbo);
  glGenBuffers(1, &mb.ebo);

  glBindVertexArray(mb.vao);
  glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mb.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               indices.size() * sizeof(unsigned int), indices.data(),
               GL_STATIC_DRAW);

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

      for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
        auto &mesh = bmd->Meshes[mi];
        UploadMesh(mesh, objectDir + "/", cache.boneMatrices,
                   cache.meshBuffers);
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

    instances.push_back({obj.type, model});
  }

  std::cout << "[ObjectRenderer] Loaded " << instances.size()
            << " object instances, " << modelCache.size()
            << " unique models, skipped " << skipped << std::endl;
}

void ObjectRenderer::Render(const glm::mat4 &view, const glm::mat4 &projection,
                            const glm::vec3 &cameraPos) {
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
  shader->setBool("useFog", false);

  for (auto &inst : instances) {
    // PoseBox (type 133) is an NPC interaction trigger, not a visible object
    if (inst.type == 133)
      continue;

    auto it = modelCache.find(inst.type);
    if (it == modelCache.end())
      continue;

    shader->setMat4("model", inst.modelMatrix);

    for (auto &mb : it->second.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      // Skip meshes with no texture — these are interaction/collision volumes
      // not meant to be rendered (gameplay triggers, collision boxes, etc.)
      if (mb.texture == 0)
        continue;

      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);

      if (mb.noneBlend) {
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
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
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
