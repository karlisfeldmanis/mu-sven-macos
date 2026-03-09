#include "ObjectRenderer.hpp"
#include "TextureLoader.hpp"
#include <cmath>
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
  case 41:
    return 1; // DungeonGate02 torch — fire glow mesh
  case 42:
    return 1; // DungeonGate03 torch — fire glow mesh
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

void ObjectRenderer::UploadMeshGPUSkinned(const Mesh_t &mesh,
                                           const std::string &baseDir,
                                           std::vector<MeshBuffers> &out) {
  MeshBuffers mb;
  mb.texture = 0;

  struct SkinnedVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 tex;
    float boneIndex;
  };
  std::vector<SkinnedVertex> vertices;
  std::vector<unsigned int> indices;

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    int startIdx = vertices.size();
    for (int v = 0; v < 3; ++v) {
      SkinnedVertex vert;
      auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
      auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];
      vert.pos = srcVert.Position;          // RAW position (no bone transform)
      vert.normal = srcNorm.Normal;          // RAW normal
      vert.boneIndex = (float)srcVert.Node;  // Bone index for GPU skinning
      vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                           mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
      vertices.push_back(vert);
      indices.push_back(startIdx + v);
    }
    if (steps == 4) {
      int quadIndices[3] = {0, 2, 3};
      for (int v : quadIndices) {
        SkinnedVertex vert;
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];
        vert.pos = srcVert.Position;
        vert.normal = srcNorm.Normal;
        vert.boneIndex = (float)srcVert.Node;
        vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                             mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
        vertices.push_back(vert);
        indices.push_back(vertices.size() - 1);
      }
    }
  }

  mb.indexCount = indices.size();
  mb.vertexCount = vertices.size();
  mb.isDynamic = false; // Static VBO — animation via shader uniforms
  if (mb.indexCount == 0) {
    out.push_back(mb);
    return;
  }

  glGenVertexArrays(1, &mb.vao);
  glGenBuffers(1, &mb.vbo);
  glGenBuffers(1, &mb.ebo);

  glBindVertexArray(mb.vao);
  glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(SkinnedVertex),
               vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mb.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  // pos
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex), (void *)0);
  glEnableVertexAttribArray(0);
  // normal
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                        (void *)(sizeof(float) * 3));
  glEnableVertexAttribArray(1);
  // tex
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                        (void *)(sizeof(float) * 6));
  glEnableVertexAttribArray(2);
  // boneIndex
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                        (void *)(sizeof(float) * 8));
  glEnableVertexAttribArray(3);

  auto texResult = TextureLoader::ResolveWithInfo(baseDir, mesh.TextureName);
  mb.texture = texResult.textureID;
  mb.hasAlpha = texResult.hasAlpha;
  mb.textureName = mesh.TextureName;

  auto scriptFlags = TextureLoader::ParseScriptFlags(mesh.TextureName);
  mb.noneBlend = scriptFlags.noneBlend;
  mb.hidden = scriptFlags.hidden;
  mb.bright = scriptFlags.bright;
  mb.bmdTextureId = mesh.Texture;

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
      auto shouldCPUAnimate = [](int t) {
        // CPU re-skinning: low instance count types only
        return t == 56 || t == 57 ||   // MerchantAnimal01-02
               t == 60 ||              // Ship01
               t == 72 || t == 74 ||   // StoneWall04/06 (flag banners)
               t == 90 ||              // StreetLight01
               t == 95 ||              // Curtain01
               t == 96 || t == 97 ||   // Sign01-02
               t == 98 ||              // Carriage01
               t == 105 ||             // Waterspout01
               t == 110 ||             // Hanging01
               t == 118 || t == 119 || // House04-05
               t == 120 ||             // Tent01
               t == 150;               // Candle01
      };
      // GPU skinning: high instance count animated types (trees)
      bool isTree = (obj.type >= 0 && obj.type <= 19);
      bool hasAnim = !bmd->Actions.empty() && bmd->Actions[0].NumAnimationKeys > 1;

      if (hasAnim && isTree) {
        // GPU-skinned path: store raw vertices + bone indices
        cache.isGPUAnimated = true;
        cache.numAnimationKeys = bmd->Actions[0].NumAnimationKeys;
        for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
          UploadMeshGPUSkinned(bmd->Meshes[mi], objectDir + "/",
                               cache.meshBuffers);
        }
      } else {
        if (hasAnim && shouldCPUAnimate(obj.type)) {
          cache.isAnimated = true;
          cache.numAnimationKeys = bmd->Actions[0].NumAnimationKeys;
        }
        for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
          UploadMesh(bmd->Meshes[mi], objectDir + "/", cache.boneMatrices,
                     cache.meshBuffers, cache.isAnimated);
        }
      }

      // Mark BlendMesh meshes (window light / glow)
      if (cache.blendMeshTexId >= 0) {
        for (auto &mb : cache.meshBuffers) {
          if (mb.bmdTextureId == cache.blendMeshTexId) {
            mb.isWindowLight = true;
          }
        }
      }

      // Retain BMD data for animated types (CPU re-skinning or GPU bone compute)
      if (cache.isAnimated || cache.isGPUAnimated) {
        cache.bmdData = std::move(bmd);
        std::cout << "  [" << (cache.isGPUAnimated ? "GPU-Anim" : "CPU-Anim")
                  << "] type " << obj.type
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

    // Skip grass objects (types 20-29) on non-grass terrain tiles.
    // Original engine only renders grass BMDs on grass terrain (layer1 0 or 1).
    if (obj.type >= 20 && obj.type <= 29 && terrainMapping) {
      const int S = 256;
      int gz = (int)(obj.position.x / 100.0f);
      int gx = (int)(obj.position.z / 100.0f);
      if (gz >= 0 && gx >= 0 && gz < S && gx < S) {
        uint8_t tile = terrainMapping->layer1[gz * S + gx];
        if (tile != 0 && tile != 1) {
          ++skipped;
          continue;
        }
      }
    }

    // Snap grass objects (types 20-29) to terrain heightmap to prevent floating
    glm::vec3 objPos = obj.position;
    if (obj.type >= 20 && obj.type <= 29 &&
        terrainHeightmap.size() >= 256 * 256) {
      const int S = 256;
      float gz = objPos.x / 100.0f;
      float gx = objPos.z / 100.0f;
      gz = std::clamp(gz, 0.0f, (float)(S - 2));
      gx = std::clamp(gx, 0.0f, (float)(S - 2));
      int xi = (int)gx, zi = (int)gz;
      float xd = gx - (float)xi, zd = gz - (float)zi;
      float h00 = terrainHeightmap[zi * S + xi];
      float h10 = terrainHeightmap[zi * S + (xi + 1)];
      float h01 = terrainHeightmap[(zi + 1) * S + xi];
      float h11 = terrainHeightmap[(zi + 1) * S + (xi + 1)];
      objPos.y = h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
                 h01 * (1 - xd) * zd + h11 * xd * zd;
    }

    // Fix floating candle — model origin is above base, lower onto table surface
    if (obj.type == 150)
      objPos.y -= 50.0f;

    // Build model matrix
    glm::mat4 model = glm::translate(glm::mat4(1.0f), objPos);
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

    // Collect interactive objects for sit/pose system (Main 5.2 OPERATE)
    // Lorencia: type 6=Tree07 (sit), 133=PoseBox (pose), 145=Furniture06 (sit),
    // 146=Furniture07 (sit)
    InteractType iact = InteractType::SIT;
    bool isInteractive = false;
    bool alignAngle = false;
    float pickRadius = 30.0f;
    float pickHeight = 100.0f;
    switch (obj.type) {
    case 6:   // MODEL_TREE01+6 — sit, no angle
      iact = InteractType::SIT;
      isInteractive = true;
      pickRadius = 40.0f;
      pickHeight = 120.0f;
      break;
    case 133: // MODEL_POSE_BOX — pose, with angle
      iact = InteractType::POSE;
      isInteractive = true;
      alignAngle = true;
      pickRadius = 20.0f;
      pickHeight = 100.0f;
      break;
    case 145: // MODEL_FURNITURE01+5 — sit, with angle
      iact = InteractType::SIT;
      isInteractive = true;
      break;
    case 146: // MODEL_FURNITURE01+6 — sit, no angle
      iact = InteractType::SIT;
      isInteractive = true;
      break;
    default:
      break;
    }
    if (isInteractive) {
      InteractiveObject io;
      io.type = obj.type;
      io.worldPos = worldPos;
      io.facingAngle = obj.mu_angle_raw.z; // MU Z rotation in degrees
      io.alignToObject = alignAngle;
      io.action = iact;
      io.radius = pickRadius;
      io.height = pickHeight;
      m_interactiveObjects.push_back(io);
    }
  }

  std::cout << "[ObjectRenderer] Loaded " << instances.size()
            << " object instances, " << modelCache.size()
            << " unique models, " << m_interactiveObjects.size()
            << " interactive objects, skipped " << skipped << std::endl;

}

void ObjectRenderer::LoadObjectsGeneric(
    const std::vector<ObjectData> &objects, const std::string &objectDir,
    const std::string &fallbackDir) {
  int skipped = 0;
  int fromFallback = 0;

  for (auto &obj : objects) {
    // Load model into cache if not already loaded
    if (modelCache.find(obj.type) == modelCache.end()) {
      // Try 1: Generic naming from objectDir (ObjectXX.bmd)
      int idx = obj.type + 1;
      char buf[64];
      if (idx < 10)
        snprintf(buf, sizeof(buf), "Object0%d.bmd", idx);
      else
        snprintf(buf, sizeof(buf), "Object%d.bmd", idx);

      std::string fullPath = objectDir + "/" + buf;
      std::string texDir = objectDir + "/";
      auto bmd = BMDParser::Parse(fullPath);

      // Try 2: Fallback to Lorencia naming from fallbackDir
      if (!bmd && !fallbackDir.empty()) {
        std::string lorName = GetObjectBMDFilename(obj.type);
        if (!lorName.empty()) {
          fullPath = fallbackDir + "/" + lorName;
          texDir = fallbackDir + "/";
          bmd = BMDParser::Parse(fullPath);
          if (bmd)
            ++fromFallback;
        }
      }

      if (!bmd) {
        modelCache[obj.type] = ModelCache{};
        ++skipped;
        continue;
      }

      ModelCache cache;
      cache.boneMatrices = ComputeBoneMatrices(bmd.get());
      cache.blendMeshTexId = GetBlendMeshTexId(obj.type);

      // Enable animation for models with multiple keyframes (low instance count)
      if (!bmd->Actions.empty() && bmd->Actions[0].NumAnimationKeys > 1) {
        int instanceCount = 0;
        for (auto &o : objects)
          if (o.type == obj.type)
            instanceCount++;
        if (instanceCount <= 20) {
          cache.isAnimated = true;
          cache.numAnimationKeys = bmd->Actions[0].NumAnimationKeys;
        }
      }

      for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
        UploadMesh(bmd->Meshes[mi], texDir, cache.boneMatrices,
                   cache.meshBuffers, cache.isAnimated);
      }

      // Mark BlendMesh meshes (fire glow / torch light)
      if (cache.blendMeshTexId >= 0) {
        for (auto &mb : cache.meshBuffers) {
          if (mb.bmdTextureId == cache.blendMeshTexId) {
            mb.isWindowLight = true;
          }
        }
      }

      if (cache.isAnimated) {
        cache.bmdData = std::move(bmd);
      }

      modelCache[obj.type] = std::move(cache);
    }

    auto &cache = modelCache[obj.type];
    if (cache.meshBuffers.empty()) {
      ++skipped;
      continue;
    }

    // Build model matrix (same transform as LoadObjects)
    glm::vec3 objPos = obj.position;
    glm::mat4 model = glm::translate(glm::mat4(1.0f), objPos);
    model =
        glm::rotate(model, glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    model =
        glm::rotate(model, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, obj.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, obj.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, obj.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::scale(model, glm::vec3(obj.scale));

    glm::vec3 worldPos = glm::vec3(model[3]);
    glm::vec3 tLight = SampleTerrainLight(worldPos);

    instances.push_back({obj.type, model, tLight});
  }

  std::cout << "[ObjectRenderer] Generic: Loaded " << instances.size()
            << " instances, " << modelCache.size() << " unique models ("
            << fromFallback << " from fallback), skipped " << skipped
            << std::endl;
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

void ObjectRenderer::SetTypeAlpha(
    const std::unordered_map<int, float> &alphaMap) {
  typeAlphaMap = alphaMap;
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

  // Simple buffer update — matches ModelViewer's working approach
  glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(Vertex),
                  vertices.data());
}


void ObjectRenderer::Render(const glm::mat4 &view, const glm::mat4 &projection,
                            const glm::vec3 &cameraPos, float currentTime) {
  if (instances.empty() || !shader)
    return;

  // Extract frustum planes from VP matrix for culling
  glm::mat4 vp = projection * view;
  glm::vec4 frustum[6];
  frustum[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0],
                          vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // Left
  frustum[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0],
                          vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // Right
  frustum[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1],
                          vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // Bottom
  frustum[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1],
                          vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // Top
  frustum[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2],
                          vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]); // Near
  frustum[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2],
                          vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // Far
  for (int i = 0; i < 6; ++i)
    frustum[i] /= glm::length(glm::vec3(frustum[i]));

  shader->use();
  glActiveTexture(GL_TEXTURE0);
  shader->setInt("texture_diffuse", 0);
  shader->setMat4("projection", projection);
  shader->setMat4("view", view);
  // High sun for even world-scale illumination
  shader->setVec3("lightPos", cameraPos + glm::vec3(0, 8000, 0));
  shader->setVec3("lightColor", 1.0f, 1.0f, 1.0f);
  shader->setVec3("viewPos", cameraPos);
  shader->setBool("useFog", m_fogEnabled);
  shader->setVec3("uFogColor", m_fogColor);
  shader->setFloat("uFogNear", m_fogNear);
  shader->setFloat("uFogFar", m_fogFar);
  shader->setFloat("blendMeshLight", 1.0f);
  shader->setFloat("objectAlpha", 1.0f);
  shader->setVec2("texCoordOffset", glm::vec2(0.0f));
  shader->setFloat("luminosity", m_luminosity);
  shader->setInt("chromeMode", 0);
  shader->setFloat("chromeTime", 0.0f);
  shader->setVec3("baseTint", glm::vec3(1.0f));
  shader->setVec3("glowColor", glm::vec3(0.0f));

  // Set point light uniforms (pre-cached locations)
  shader->uploadPointLights(plCount, plPositions.data(), plColors.data(),
                            plRanges.data());

  // Advance skeletal animation for animated model types
  shader->setBool("useSkinning", false); // Default off
  {
    const float ANIM_SPEED = 4.0f; // keyframes/sec (reference: 0.16 * 25fps)
    const float TREE_ANIM_SPEED = 8.0f; // Trees: Main 5.2 Velocity=0.4 * 25fps
    float dt = (lastAnimTime > 0.0f) ? (currentTime - lastAnimTime) : 0.0f;
    if (dt > 0.0f && dt < 1.0f) { // clamp to avoid huge jumps
      for (auto &[type, cache] : modelCache) {
        if (!cache.bmdData)
          continue;

        // GPU-skinned animation (trees): compute bone matrices for shader upload
        if (cache.isGPUAnimated) {
          auto &state = animStates[type];
          state.frame += TREE_ANIM_SPEED * dt;
          if (state.frame >= (float)cache.numAnimationKeys)
            state.frame = std::fmod(state.frame, (float)cache.numAnimationKeys);

          auto bones = ComputeBoneMatricesInterpolated(cache.bmdData.get(), 0,
                                                       state.frame);
          // Convert BoneWorldMatrix (3x4 row-major) to glm::mat4 (column-major)
          cache.gpuBoneMatrices.resize(bones.size());
          for (size_t i = 0; i < bones.size(); ++i) {
            auto &bm = bones[i];
            cache.gpuBoneMatrices[i][0] = glm::vec4(bm[0][0], bm[1][0], bm[2][0], 0.0f);
            cache.gpuBoneMatrices[i][1] = glm::vec4(bm[0][1], bm[1][1], bm[2][1], 0.0f);
            cache.gpuBoneMatrices[i][2] = glm::vec4(bm[0][2], bm[1][2], bm[2][2], 0.0f);
            cache.gpuBoneMatrices[i][3] = glm::vec4(bm[0][3], bm[1][3], bm[2][3], 1.0f);
          }
          continue;
        }

        // CPU-skinned animation: retransform mesh vertices
        if (!cache.isAnimated)
          continue;

        auto &state = animStates[type];
        state.frame += ANIM_SPEED * dt;
        if (state.frame >= (float)cache.numAnimationKeys)
          state.frame = std::fmod(state.frame, (float)cache.numAnimationKeys);

        auto bones = ComputeBoneMatricesInterpolated(cache.bmdData.get(), 0,
                                                     state.frame);

        // StoneWall04/06 flags: reduce bone animation amplitude to prevent
        // clipping through adjacent walls. Blend animated bones with rest
        // pose (30% animation, 70% rest). Only affects the animated chain
        // bones (2-5), not the static root/helpers.
        bool isWallFlag = (type == 72 || type == 74);
        if (isWallFlag) {
          const float blend = 0.3f;
          auto &rest = cache.boneMatrices;
          for (int bi = 0; bi < (int)bones.size() && bi < (int)rest.size();
               ++bi) {
            for (int r = 0; r < 3; ++r)
              for (int c = 0; c < 4; ++c)
                bones[bi][r][c] =
                    rest[bi][r][c] +
                    blend * (bones[bi][r][c] - rest[bi][r][c]);
          }
        }

        for (int mi = 0; mi < (int)cache.meshBuffers.size() &&
                         mi < (int)cache.bmdData->Meshes.size();
             ++mi) {
          // StoneWall04/06: only animate flag meshes (badge_*), skip
          // static wall base (tile_01.jpg) to prevent clipping
          if (isWallFlag &&
              cache.meshBuffers[mi].textureName.find("badge") ==
                  std::string::npos) {
            continue;
          }
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
  // Layered sines (additive, not multiplicative) — no beat-frequency strobing
  float flickerBase = 0.72f + 0.09f * std::sin(currentTime * 4.7f)
                            + 0.07f * std::sin(currentTime * 11.3f + 1.3f)
                            + 0.04f * std::sin(currentTime * 21.7f + 3.7f);
  // UV scroll: 1-second cycle for animated window meshes
  float uvScroll = -std::fmod(currentTime, 1.0f);

  // Dungeon UV scroll: types 22-24 StreamMesh=1, V = -(WorldTime%1000)*0.001
  // Main 5.2 ZzzObject.cpp:3964 — 1-second cycle, V-axis only
  float dungeonWaterScroll = 0.0f;
  if (m_mapId == 1) {
    int wt = (int)(currentTime * 1000.0f) % 1000;
    dungeonWaterScroll = -(float)wt * 0.001f;
  }

  for (auto &inst : instances) {
    // PoseBox (type 133) is an NPC interaction trigger, not a visible object
    // Light01-03 (types 130-132) are fire/smoke-only emitters (HiddenMesh=-2)
    if (inst.type == 133 || (inst.type >= 130 && inst.type <= 132))
      continue;

    // Dungeon hidden objects (Main 5.2 ZzzObject.cpp:3955 — HiddenMesh=-2)
    // Types 39=lance trap, 40=blade trap, 51=fire trap (VFX-only)
    // Type 52: debris spawner (particles only, no mesh)
    // Type 60: gate trigger (invisible, interaction-only)
    if (m_mapId == 1 &&
        (inst.type == 39 || inst.type == 40 || inst.type == 51 ||
         inst.type == 52 || inst.type == 60))
      continue;

    // Type filter: if set, only render specified types
    if (!m_typeFilter.empty()) {
      bool allowed = false;
      for (int t : m_typeFilter)
        if (inst.type == t) { allowed = true; break; }
      if (!allowed) continue;
    }

    // Frustum culling: skip objects outside view frustum
    {
      glm::vec3 objPos = glm::vec3(inst.modelMatrix[3]);
      float cullRadius = 500.0f; // Generous radius for buildings/trees
      bool outside = false;
      for (int p = 0; p < 6; ++p) {
        if (frustum[p].x * objPos.x + frustum[p].y * objPos.y +
                frustum[p].z * objPos.z + frustum[p].w <
            -cullRadius) {
          outside = true;
          break;
        }
      }
      if (outside)
        continue;
    }

    auto it = modelCache.find(inst.type);
    if (it == modelCache.end())
      continue;

    // Per-type alpha for roof hiding (types 125/126 fade when inside buildings)
    float instAlpha = 1.0f;
    auto alphaIt = typeAlphaMap.find(inst.type);
    if (alphaIt != typeAlphaMap.end()) {
      instAlpha = alphaIt->second;
      if (instAlpha < 0.01f)
        continue; // Skip fully invisible objects
    }
    shader->setFloat("objectAlpha", instAlpha);

    shader->setMat4("model", inst.modelMatrix);
    shader->setVec3("terrainLight", inst.terrainLight);

    // GPU skinning: upload bone matrices for GPU-animated types (trees)
    if (it->second.isGPUAnimated && !it->second.gpuBoneMatrices.empty()) {
      int count = std::min((int)it->second.gpuBoneMatrices.size(), 48);
      glUniformMatrix4fv(shader->loc("boneMatrices"), count, GL_FALSE,
                         glm::value_ptr(it->second.gpuBoneMatrices[0]));
      shader->setBool("useSkinning", true);
    } else {
      shader->setBool("useSkinning", false);
    }

    // Check if this model type has BlendMesh animation
    bool hasBlendMesh = it->second.blendMeshTexId >= 0;
    bool hasUVScroll =
        (inst.type == 118 || inst.type == 119 || inst.type == 105);
    // Dungeon water objects: types 22-24 with StreamMesh=1 (mesh idx 1 scrolls)
    bool isDungeonWater =
        (m_mapId == 1 && inst.type >= 22 && inst.type <= 24);

    // Dungeon coffins/sarcophagi (types 44-46), organic/squid objects (11, 22-24, 53):
    // disable face culling — thin/double-sided geometry needs both sides visible
    bool disableCullForObj = (m_mapId == 1 &&
        ((inst.type >= 44 && inst.type <= 46) ||
         (inst.type >= 22 && inst.type <= 24) ||
         inst.type == 11 || inst.type == 53));
    if (disableCullForObj)
      glDisable(GL_CULL_FACE);

    int meshIdx = 0;
    for (auto &mb : it->second.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden) {
        ++meshIdx;
        continue;
      }
      if (mb.texture == 0) {
        ++meshIdx;
        continue;
      }

      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);

      // Dungeon water: StreamMesh=1 — only mesh index 1 gets V-axis UV scroll
      // Main 5.2: BlendMeshTexCoordV = -(WorldTime%1000)*0.001f
      if (isDungeonWater && meshIdx == 1) {
        shader->setVec2("texCoordOffset", glm::vec2(0.0f, dungeonWaterScroll));
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        shader->setVec2("texCoordOffset", glm::vec2(0.0f));
        ++meshIdx;
        continue;
      }

      if (mb.isWindowLight && hasBlendMesh) {
        // BlendMesh: additive blending with intensity flicker
        float intensity = flickerBase;
        // Bonfire has wider flicker range (0.5-0.95)
        if (inst.type == 52)
          intensity = 0.70f + 0.12f * std::sin(currentTime * 6.3f)
                            + 0.08f * std::sin(currentTime * 14.1f + 0.7f)
                            + 0.05f * std::sin(currentTime * 27.3f + 2.1f);
        // Street light: subtle flicker 0.6-0.8 (Main 5.2: rand()%2+6 * 0.1)
        if (inst.type == 90)
          intensity = 0.7f + 0.1f * std::sin(currentTime * 5.3f);
        // Static glow for candles, carriages, waterspout
        if (inst.type == 150 || inst.type == 98 || inst.type == 105)
          intensity = 1.0f;
        // Dungeon torches: warm steady glow with gentle organic flicker
        if (inst.type == 41 || inst.type == 42) {
          float phase = inst.modelMatrix[3][0] * 0.013f; // per-torch offset
          intensity = 0.78f + 0.10f * std::sin(currentTime * 3.8f + phase)
                            + 0.06f * std::sin(currentTime * 9.5f + phase * 2.1f);
        }

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
        // Depth writes stay ON — shader discards transparent fragments
        // (texColor.a < 0.1), so opaque parts write depth correctly.
        if (mb.hasAlpha) {
          glDisable(GL_CULL_FACE);
          glEnable(GL_POLYGON_OFFSET_FILL);
          glPolygonOffset(-1.0f, -1.0f);
        }
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        if (mb.hasAlpha) {
          glDisable(GL_POLYGON_OFFSET_FILL);
          glEnable(GL_CULL_FACE);
        }
      }
      ++meshIdx;
    }

    if (disableCullForObj)
      glEnable(GL_CULL_FACE);
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
  if (m_chromeTexture) {
    glDeleteTextures(1, &m_chromeTexture);
    m_chromeTexture = 0;
  }
  shader.reset();
}
