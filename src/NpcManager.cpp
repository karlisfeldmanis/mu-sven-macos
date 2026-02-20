#include "NpcManager.hpp"
#include "HeroCharacter.hpp" // For PointLight struct
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

// NPC type → display name mapping (matches Database::SeedNpcSpawns)
static const std::unordered_map<uint16_t, std::string> s_npcNames = {
    {253, "Potion Girl Amy"},      {250, "Weapon Merchant"},
    {251, "Hanzo the Blacksmith"}, {254, "Pasi the Mage"},
    {255, "Lumen the Barmaid"},    {240, "Safety Guardian"}};

glm::vec3 NpcManager::sampleTerrainLightAt(const glm::vec3 &worldPos) const {
  const int SIZE = 256;
  if (m_terrainLightmap.size() < (size_t)(SIZE * SIZE))
    return glm::vec3(1.0f);

  float gz = worldPos.x / 100.0f;
  float gx = worldPos.z / 100.0f;
  int xi = (int)gx, zi = (int)gz;
  if (xi < 0 || zi < 0 || xi > SIZE - 2 || zi > SIZE - 2)
    return glm::vec3(0.5f);

  float xd = gx - (float)xi, zd = gz - (float)zi;
  const glm::vec3 &c00 = m_terrainLightmap[zi * SIZE + xi];
  const glm::vec3 &c10 = m_terrainLightmap[zi * SIZE + (xi + 1)];
  const glm::vec3 &c01 = m_terrainLightmap[(zi + 1) * SIZE + xi];
  const glm::vec3 &c11 = m_terrainLightmap[(zi + 1) * SIZE + (xi + 1)];
  glm::vec3 left = c00 + (c01 - c00) * zd;
  glm::vec3 right = c10 + (c11 - c10) * zd;
  return left + (right - left) * xd;
}

float NpcManager::snapToTerrain(float worldX, float worldZ) {
  if (!m_terrainData)
    return 0.0f;
  const int S = TerrainParser::TERRAIN_SIZE;
  float gz = worldX / 100.0f;
  float gx = worldZ / 100.0f;
  gz = std::clamp(gz, 0.0f, (float)(S - 2));
  gx = std::clamp(gx, 0.0f, (float)(S - 2));
  int xi = (int)gx, zi = (int)gz;
  float xd = gx - (float)xi, zd = gz - (float)zi;
  float h00 = m_terrainData->heightmap[zi * S + xi];
  float h10 = m_terrainData->heightmap[zi * S + (xi + 1)];
  float h01 = m_terrainData->heightmap[(zi + 1) * S + xi];
  float h11 = m_terrainData->heightmap[(zi + 1) * S + (xi + 1)];
  return h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) + h01 * (1 - xd) * zd +
         h11 * xd * zd;
}

int NpcManager::loadModel(const std::string &npcPath,
                          const std::string &skeletonFile,
                          const std::vector<std::string> &partFiles,
                          const std::string &modelName) {
  // Check if already loaded
  for (int i = 0; i < (int)m_models.size(); ++i) {
    if (m_models[i].name == modelName)
      return i;
  }

  auto skeleton = BMDParser::Parse(npcPath + skeletonFile);
  if (!skeleton) {
    std::cerr << "[NPC] Failed to load skeleton: " << skeletonFile << std::endl;
    return -1;
  }

  NpcModel model;
  model.name = modelName;
  model.skeleton = skeleton.get();
  m_ownedBmds.push_back(std::move(skeleton));

  // Load body part BMDs
  for (auto &partFile : partFiles) {
    auto part = BMDParser::Parse(npcPath + partFile);
    if (!part) {
      std::cerr << "[NPC] Failed to load part: " << partFile << std::endl;
      continue;
    }
    model.parts.push_back(part.get());
    m_ownedBmds.push_back(std::move(part));
  }

  int idx = (int)m_models.size();
  m_models.push_back(std::move(model));
  std::cout << "[NPC] Loaded model '" << modelName << "' ("
            << m_models[idx].skeleton->Bones.size() << " bones, "
            << m_models[idx].parts.size() << " parts)" << std::endl;
  return idx;
}

void NpcManager::addNpc(int modelIdx, int gridX, int gridY, int dir,
                        float scale) {
  if (modelIdx < 0 || modelIdx >= (int)m_models.size())
    return;

  NpcInstance npc;
  npc.modelIdx = modelIdx;
  npc.scale = scale;
  npc.action = 0;  // Idle
  npc.npcType = 0; // Set by AddNpcByType or Init caller

  // Grid to world: WorldX = gridY * 100, WorldZ = gridX * 100
  float worldX = (float)gridY * 100.0f;
  float worldZ = (float)gridX * 100.0f;
  float worldY = snapToTerrain(worldX, worldZ);
  npc.position = glm::vec3(worldX, worldY, worldZ);

  // Direction to facing angle from original MU source (WSclient.cpp:2564):
  //   Angle[2] = (Direction - 1) * 45.0f
  // dir: 0=-45°(SW), 1=0°(S), 2=45°(SE), 3=90°(E), 4=135°(NE), 5=180°(N),
  // 6=225°(NW), 7=270°(W)
  npc.facing = (float)(dir - 1) * (float)M_PI / 4.0f;

  // Random animation offset so NPCs don't all sync
  npc.animFrame = (float)(m_npcs.size() * 3.7f);

  auto &mdl = m_models[modelIdx];
  auto bones = ComputeBoneMatrices(mdl.skeleton);

  // Check if skeleton itself has renderable meshes
  bool skeletonHasMeshes = false;
  for (auto &mesh : mdl.skeleton->Meshes) {
    if (mesh.NumTriangles > 0) {
      skeletonHasMeshes = true;
      break;
    }
  }

  std::string npcTexPath = "";
  // Determine texture path from skeleton's first mesh
  if (!mdl.skeleton->Meshes.empty()) {
    // NPC textures are in Data/NPC/
    // The texture path is resolved by UploadMeshWithBones
  }

  // Find NPC texture directory
  // Skeleton file is in Data/NPC/, textures are there too
  std::string texDir = ""; // Will be set in Init()

  // Upload skeleton meshes (for single-model NPCs like Smith, Wizard, Storage)
  if (skeletonHasMeshes) {
    AABB aabb{};
    NpcInstance::BodyPart bp;
    bp.bmdIdx = -1; // skeleton
    for (auto &mesh : mdl.skeleton->Meshes) {
      UploadMeshWithBones(mesh, m_npcTexPath, bones, bp.meshBuffers, aabb,
                          true);
    }
    npc.bodyParts.push_back(std::move(bp));
  }

  // Upload body part meshes (for multi-part NPCs like Man, Girl, Female)
  for (int pi = 0; pi < (int)mdl.parts.size(); ++pi) {
    AABB aabb{};
    NpcInstance::BodyPart bp;
    bp.bmdIdx = pi;
    for (auto &mesh : mdl.parts[pi]->Meshes) {
      UploadMeshWithBones(mesh, m_npcTexPath, bones, bp.meshBuffers, aabb,
                          true);
    }
    npc.bodyParts.push_back(std::move(bp));
  }

  // Create shadow mesh buffers
  for (auto &bp : npc.bodyParts) {
    for (auto &mb : bp.meshBuffers) {
      NpcInstance::ShadowMesh sm;
      sm.vertexCount = mb.vertexCount;
      if (sm.vertexCount == 0) {
        npc.shadowMeshes.push_back(sm);
        continue;
      }
      glGenVertexArrays(1, &sm.vao);
      glGenBuffers(1, &sm.vbo);
      glBindVertexArray(sm.vao);
      glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
      glBufferData(GL_ARRAY_BUFFER, sm.vertexCount * sizeof(glm::vec3), nullptr,
                   GL_DYNAMIC_DRAW);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3),
                            (void *)0);
      glEnableVertexAttribArray(0);
      glBindVertexArray(0);
      npc.shadowMeshes.push_back(sm);
    }
  }

  m_npcs.push_back(std::move(npc));
}

void NpcManager::InitModels(const std::string &dataPath) {
  if (m_modelsLoaded)
    return;

  std::string npcPath = dataPath + "/NPC/";
  m_npcTexPath = npcPath;

  // Create shaders
  std::ifstream shaderTest("shaders/model.vert");
  m_shader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/model.vert" : "../shaders/model.vert",
      shaderTest.good() ? "shaders/model.frag" : "../shaders/model.frag");

  m_shadowShader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/shadow.vert" : "../shaders/shadow.vert",
      shaderTest.good() ? "shaders/shadow.frag" : "../shaders/shadow.frag");

  // Load NPC models for 0.97d Lorencia
  int smithIdx = loadModel(npcPath, "Smith01.bmd", {}, "Smith");
  int wizardIdx = loadModel(npcPath, "Wizard01.bmd", {}, "Wizard");
  int storageIdx = loadModel(npcPath, "Storage01.bmd", {}, "Storage");
  int manIdx = loadModel(
      npcPath, "Man01.bmd",
      {"ManHead01.bmd", "ManUpper01.bmd", "ManGloves01.bmd", "ManBoots01.bmd"},
      "MerchantMan");
  int girlIdx = loadModel(
      npcPath, "Girl01.bmd",
      {"GirlHead01.bmd", "GirlUpper01.bmd", "GirlLower01.bmd"}, "MerchantGirl");
  int femaleIdx = loadModel(npcPath, "Female01.bmd",
                            {"FemaleHead01.bmd", "FemaleUpper01.bmd",
                             "FemaleLower01.bmd", "FemaleBoots01.bmd"},
                            "MerchantFemale");

  // Map NPC type IDs to model indices
  m_typeToModel[251] = smithIdx;   // Hanzo the Blacksmith
  m_typeToModel[254] = wizardIdx;  // Pasi the Mage
  m_typeToModel[240] = storageIdx; // Safety Guardian (Vault)
  m_typeToModel[250] = manIdx;     // Weapon Merchant
  m_typeToModel[253] = girlIdx;    // Potion Girl Amy
  m_typeToModel[255] = femaleIdx;  // Lumen the Barmaid

  // Scale overrides
  m_typeScale[251] = 0.95f; // Blacksmith slightly smaller

  m_modelsLoaded = true;
  std::cout << "[NPC] Models loaded: " << m_models.size() << " types, "
            << m_typeToModel.size() << " type mappings" << std::endl;
}

void NpcManager::AddNpcByType(uint16_t npcType, uint8_t gridX, uint8_t gridY,
                              uint8_t dir) {
  auto it = m_typeToModel.find(npcType);
  if (it == m_typeToModel.end()) {
    std::cerr << "[NPC] Unknown NPC type " << npcType << " at (" << (int)gridX
              << "," << (int)gridY << "), skipping" << std::endl;
    return;
  }
  float scale = 1.0f;
  auto scaleIt = m_typeScale.find(npcType);
  if (scaleIt != m_typeScale.end())
    scale = scaleIt->second;

  addNpc(it->second, gridX, gridY, dir, scale);

  // Set type and name on the just-added NPC
  auto &added = m_npcs.back();
  added.npcType = npcType;
  auto nameIt = s_npcNames.find(npcType);
  if (nameIt != s_npcNames.end())
    added.name = nameIt->second;

  std::cout << "[NPC] Server-spawned NPC type=" << npcType << " at grid ("
            << (int)gridX << "," << (int)gridY << ") dir=" << (int)dir
            << std::endl;
}

void NpcManager::Init(const std::string &dataPath) {
  // Load models if not already loaded
  InitModels(dataPath);

  // Hardcoded fallback: place Lorencia NPCs from MonsterSetBase.txt
  struct HardcodedNpc {
    uint16_t type;
    int gx, gy, dir;
    float scale;
  };
  HardcodedNpc hardcoded[] = {
      {253, 127, 86, 2, 1.0f},   {250, 183, 137, 2, 1.0f},
      {251, 116, 141, 3, 0.95f}, {254, 118, 113, 3, 1.0f},
      {255, 123, 135, 1, 1.0f},  {240, 146, 110, 3, 1.0f},
      {240, 147, 145, 1, 1.0f}};
  for (auto &h : hardcoded) {
    addNpc(m_typeToModel[h.type], h.gx, h.gy, h.dir, h.scale);
    auto &added = m_npcs.back();
    added.npcType = h.type;
    auto nameIt = s_npcNames.find(h.type);
    if (nameIt != s_npcNames.end())
      added.name = nameIt->second;
  }

  std::cout << "[NPC] Initialized " << m_npcs.size()
            << " NPCs in Lorencia (hardcoded fallback)" << std::endl;
}

void NpcManager::Render(const glm::mat4 &view, const glm::mat4 &proj,
                        const glm::vec3 &camPos, float deltaTime) {
  if (!m_shader || m_npcs.empty())
    return;

  m_shader->use();
  m_shader->setMat4("projection", proj);
  m_shader->setMat4("view", view);

  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
  m_shader->setVec3("lightPos", eye + glm::vec3(0, 500, 0));
  m_shader->setVec3("lightColor", 1.0f, 1.0f, 1.0f);
  m_shader->setVec3("viewPos", eye);
  m_shader->setBool("useFog", true);
  m_shader->setFloat("blendMeshLight", 1.0f);
  m_shader->setFloat("objectAlpha", 1.0f);
  m_shader->setVec2("texCoordOffset", glm::vec2(0.0f));
  m_shader->setFloat("luminosity", m_luminosity);

  // Point lights
  int plCount = std::min((int)m_pointLights.size(), MAX_POINT_LIGHTS);
  m_shader->setInt("numPointLights", plCount);
  for (int i = 0; i < plCount; ++i) {
    std::string idx = std::to_string(i);
    m_shader->setVec3("pointLightPos[" + idx + "]", m_pointLights[i].position);
    m_shader->setVec3("pointLightColor[" + idx + "]", m_pointLights[i].color);
    m_shader->setFloat("pointLightRange[" + idx + "]", m_pointLights[i].range);
  }

  for (auto &npc : m_npcs) {
    auto &mdl = m_models[npc.modelIdx];

    // Advance idle animation
    int numKeys = 1;
    if (npc.action >= 0 && npc.action < (int)mdl.skeleton->Actions.size())
      numKeys = mdl.skeleton->Actions[npc.action].NumAnimationKeys;
    if (numKeys > 1) {
      npc.animFrame += ANIM_SPEED * deltaTime;
      if (npc.animFrame >= (float)numKeys)
        npc.animFrame = std::fmod(npc.animFrame, (float)numKeys);
    }

    // Compute bone matrices
    auto bones = ComputeBoneMatricesInterpolated(mdl.skeleton, npc.action,
                                                 npc.animFrame);
    npc.cachedBones = bones;

    // Re-skin meshes
    int partIdx = 0;
    for (auto &bp : npc.bodyParts) {
      BMDData *bmd = (bp.bmdIdx < 0) ? mdl.skeleton : mdl.parts[bp.bmdIdx];
      for (int mi = 0;
           mi < (int)bp.meshBuffers.size() && mi < (int)bmd->Meshes.size();
           ++mi) {
        RetransformMeshWithBones(bmd->Meshes[mi], bones, bp.meshBuffers[mi]);
      }
    }

    // Build model matrix
    glm::mat4 model = glm::translate(glm::mat4(1.0f), npc.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, npc.facing, glm::vec3(0, 0, 1));
    if (npc.scale != 1.0f)
      model = glm::scale(model, glm::vec3(npc.scale));

    m_shader->setMat4("model", model);

    // Terrain lightmap at NPC position
    glm::vec3 tLight = sampleTerrainLightAt(npc.position);
    m_shader->setVec3("terrainLight", tLight);

    // Draw all body part meshes
    for (auto &bp : npc.bodyParts) {
      for (auto &mb : bp.meshBuffers) {
        if (mb.indexCount == 0 || mb.hidden)
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
}

void NpcManager::RenderShadows(const glm::mat4 &view, const glm::mat4 &proj) {
  if (!m_shadowShader || m_npcs.empty())
    return;

  m_shadowShader->use();
  m_shadowShader->setMat4("projection", proj);
  m_shadowShader->setMat4("view", view);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.0f, -1.0f);
  glDisable(GL_CULL_FACE);

  // Stencil per NPC to avoid overlap
  glEnable(GL_STENCIL_TEST);

  const float sx = 2000.0f;
  const float sy = 4000.0f;

  for (auto &npc : m_npcs) {
    if (npc.cachedBones.empty())
      continue;

    auto &mdl = m_models[npc.modelIdx];

    // Shadow model matrix (facing baked into vertices)
    glm::mat4 model = glm::translate(glm::mat4(1.0f), npc.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    if (npc.scale != 1.0f)
      model = glm::scale(model, glm::vec3(npc.scale));

    m_shadowShader->setMat4("model", model);

    // Clear stencil for this NPC
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    float cosF = cosf(npc.facing);
    float sinF = sinf(npc.facing);

    int smIdx = 0;
    for (auto &bp : npc.bodyParts) {
      BMDData *bmd = (bp.bmdIdx < 0) ? mdl.skeleton : mdl.parts[bp.bmdIdx];
      for (int mi = 0;
           mi < (int)bmd->Meshes.size() && smIdx < (int)npc.shadowMeshes.size();
           ++mi, ++smIdx) {
        auto &sm = npc.shadowMeshes[smIdx];
        if (sm.vertexCount == 0 || sm.vao == 0)
          continue;

        auto &mesh = bmd->Meshes[mi];
        std::vector<glm::vec3> shadowVerts;
        shadowVerts.reserve(sm.vertexCount);

        for (int i = 0; i < mesh.NumTriangles; ++i) {
          auto &tri = mesh.Triangles[i];
          int steps = (tri.Polygon == 3) ? 3 : 4;
          for (int v = 0; v < 3; ++v) {
            auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
            glm::vec3 pos = srcVert.Position;
            int boneIdx = srcVert.Node;
            if (boneIdx >= 0 && boneIdx < (int)npc.cachedBones.size()) {
              pos = MuMath::TransformPoint(
                  (const float(*)[4])npc.cachedBones[boneIdx].data(), pos);
            }
            // Apply scale
            pos *= npc.scale;
            // Apply facing rotation in MU space
            float rx = pos.x * cosF - pos.y * sinF;
            float ry = pos.x * sinF + pos.y * cosF;
            pos.x = rx;
            pos.y = ry;
            // Shadow projection
            if (pos.z < sy) {
              float factor = 1.0f / (pos.z - sy);
              pos.x += pos.z * (pos.x + sx) * factor;
              pos.y += pos.z * (pos.y + sx) * factor;
            }
            pos.z = 5.0f;
            shadowVerts.push_back(pos);
          }
          if (steps == 4) {
            int quadIndices[3] = {0, 2, 3};
            for (int v : quadIndices) {
              auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
              glm::vec3 pos = srcVert.Position;
              int boneIdx = srcVert.Node;
              if (boneIdx >= 0 && boneIdx < (int)npc.cachedBones.size()) {
                pos = MuMath::TransformPoint(
                    (const float(*)[4])npc.cachedBones[boneIdx].data(), pos);
              }
              pos *= npc.scale;
              float rx = pos.x * cosF - pos.y * sinF;
              float ry = pos.x * sinF + pos.y * cosF;
              pos.x = rx;
              pos.y = ry;
              if (pos.z < sy) {
                float factor = 1.0f / (pos.z - sy);
                pos.x += pos.z * (pos.x + sx) * factor;
                pos.y += pos.z * (pos.y + sx) * factor;
              }
              pos.z = 5.0f;
              shadowVerts.push_back(pos);
            }
          }
        }

        glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        shadowVerts.size() * sizeof(glm::vec3),
                        shadowVerts.data());
        glBindVertexArray(sm.vao);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shadowVerts.size());
      }
    }
  }

  glBindVertexArray(0);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
}

NpcInfo NpcManager::GetNpcInfo(int index) const {
  NpcInfo info{};
  if (index < 0 || index >= (int)m_npcs.size())
    return info;
  const auto &npc = m_npcs[index];
  info.position = npc.position;
  info.radius = 80.0f;
  info.height = 200.0f;
  info.name = npc.name;
  info.type = npc.npcType;
  return info;
}

void NpcManager::RenderOutline(int npcIndex, const glm::mat4 &view,
                               const glm::mat4 &proj) {
  // Original MU selection outline (ZzzObject.cpp:301-328):
  // Two-pass scaled rendering with RENDER_BRIGHT (additive blend).
  // NPC colors: pass1 = (0.02, 0.1, 0), pass2 = (0.2, 0.2, 0)
  // BoneScale = 1.2 for both passes. LightEnable = false.

  if (!m_shader || npcIndex < 0 || npcIndex >= (int)m_npcs.size())
    return;

  auto &npc = m_npcs[npcIndex];

  m_shader->use();
  m_shader->setMat4("projection", proj);
  m_shader->setMat4("view", view);
  m_shader->setVec3("lightPos", glm::vec3(0, 10000, 0));
  m_shader->setVec3("lightColor", 1.0f, 1.0f, 1.0f); // Enabled for outline
  m_shader->setVec3("viewPos", glm::vec3(0));
  m_shader->setBool("useFog", false);
  m_shader->setVec2("texCoordOffset", glm::vec2(0.0f));
  m_shader->setInt("numPointLights", 0);
  m_shader->setFloat("luminosity", 1.0f);
  m_shader->setFloat("objectAlpha", 1.0f);

  // Additive blend (RENDER_BRIGHT), no depth write, disable depth test
  // so outline shows through other geometry (original behavior)
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  glDepthMask(GL_FALSE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  // Build model matrix at BoneScale = 1.2 (original: ZzzObject.cpp:312)
  float edgeScale = npc.scale * 1.2f;
  glm::mat4 model = glm::translate(glm::mat4(1.0f), npc.position);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, npc.facing, glm::vec3(0, 0, 1));
  model = glm::scale(model, glm::vec3(edgeScale));
  m_shader->setMat4("model", model);

  // Pass 1: NPC primary edge color (0.02, 0.1, 0) — green tint
  m_shader->setFloat("blendMeshLight", 1.0f);
  m_shader->setVec3("terrainLight", 0.02f, 0.1f, 0.0f);
  for (auto &bp : npc.bodyParts) {
    for (auto &mb : bp.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }

  // Pass 2: NPC secondary edge color (0.2, 0.2, 0) — brighter green-yellow
  // Original uses same BoneScale for NPC (no second scale change)
  m_shader->setVec3("terrainLight", 0.2f, 0.2f, 0.0f);
  for (auto &bp : npc.bodyParts) {
    for (auto &mb : bp.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }

  // Restore state
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindVertexArray(0);
}

void NpcManager::Cleanup() {
  for (auto &npc : m_npcs) {
    for (auto &bp : npc.bodyParts)
      CleanupMeshBuffers(bp.meshBuffers);
    for (auto &sm : npc.shadowMeshes) {
      if (sm.vao)
        glDeleteVertexArrays(1, &sm.vao);
      if (sm.vbo)
        glDeleteBuffers(1, &sm.vbo);
    }
  }
  m_npcs.clear();
  m_models.clear();
  m_ownedBmds.clear();
  m_shader.reset();
  m_shadowShader.reset();
}

void NpcManager::RenderLabels(ImDrawList *dl, const glm::mat4 &view,
                               const glm::mat4 &proj, int winW, int winH,
                               const glm::vec3 &camPos, int hoveredNpc) {
  const float padX = 4.0f, padY = 2.0f;

  for (int i = 0; i < GetNpcCount(); ++i) {
    NpcInfo info = GetNpcInfo(i);
    if (info.name.empty())
      continue;

    float dist = glm::distance(camPos, info.position);
    if (dist > 2000.0f)
      continue;

    glm::vec3 labelPos = info.position + glm::vec3(0, info.height + 30.0f, 0);
    glm::vec4 clip = proj * view * glm::vec4(labelPos, 1.0f);
    if (clip.w <= 0)
      continue;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    float sx = (ndc.x * 0.5f + 0.5f) * (float)winW;
    float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)winH;

    ImVec2 textSize = ImGui::CalcTextSize(info.name.c_str());
    float x0 = sx - textSize.x / 2 - padX;
    float y0 = sy - textSize.y / 2 - padY;
    float x1 = sx + textSize.x / 2 + padX;
    float y1 = sy + textSize.y / 2 + padY;

    bool hovered = (i == hoveredNpc);
    ImU32 bgCol =
        hovered ? IM_COL32(20, 40, 20, 200) : IM_COL32(10, 10, 10, 150);
    ImU32 borderCol =
        hovered ? IM_COL32(100, 255, 100, 200) : IM_COL32(80, 80, 80, 150);
    ImU32 textCol = hovered ? IM_COL32(150, 255, 150, 255)
                            : IM_COL32(200, 200, 200, 255);

    // Fill
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), bgCol, 2.0f);
    // Border
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), borderCol, 2.0f, 0, 1.0f);

    // Shadow
    dl->AddText(ImVec2(sx - textSize.x / 2 + 1, sy - textSize.y / 2 + 1),
                IM_COL32(0, 0, 0, 180), info.name.c_str());
    // Text
    dl->AddText(ImVec2(sx - textSize.x / 2, sy - textSize.y / 2),
                textCol, info.name.c_str());
  }
}

int NpcManager::PickLabel(float screenX, float screenY, const glm::mat4 &view,
                          const glm::mat4 &proj, int winW, int winH,
                          const glm::vec3 &camPos) const {
  const float padX = 4.0f, padY = 2.0f;

  for (int i = 0; i < GetNpcCount(); ++i) {
    NpcInfo info = GetNpcInfo(i);
    if (info.name.empty())
      continue;

    float dist = glm::distance(camPos, info.position);
    if (dist > 2000.0f)
      continue;

    glm::vec3 labelPos = info.position + glm::vec3(0, info.height + 30.0f, 0);
    glm::vec4 clip = proj * view * glm::vec4(labelPos, 1.0f);
    if (clip.w <= 0)
      continue;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    float sx = (ndc.x * 0.5f + 0.5f) * (float)winW;
    float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)winH;

    ImVec2 textSize = ImGui::CalcTextSize(info.name.c_str());
    float x0 = sx - textSize.x / 2 - padX;
    float y0 = sy - textSize.y / 2 - padY;
    float x1 = sx + textSize.x / 2 + padX;
    float y1 = sy + textSize.y / 2 + padY;

    if (screenX >= x0 && screenX <= x1 && screenY >= y0 && screenY <= y1)
      return i;
  }
  return -1;
}
