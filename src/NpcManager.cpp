#include "NpcManager.hpp"
#include "HeroCharacter.hpp" // For PointLight struct
#include "TextureLoader.hpp"
#include "VFXManager.hpp"
#include "ViewerCommon.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

// NPC type → display name mapping (matches Database::SeedNpcSpawns)
static const std::unordered_map<uint16_t, std::string> s_npcNames = {
    {253, "Potion Girl Amy"},
    {250, "Weapon Merchant"},
    {251, "Hanzo the Blacksmith"},
    {254, "Pasi the Mage"},
    {255, "Lumen the Barmaid"},
    {240, "Safety Guardian"},
    {247, "Guard"},
    {249, "Guard"}};

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
  npc.action = m_models[modelIdx].defaultAction; // 0 for NPCs, 4/12 for guards
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

  // Determine texture directory: guards use Data/Player/, NPCs use Data/NPC/
  bool isGuard = (mdl.weaponAttachBone >= 0);
  std::string texDir = isGuard ? (m_dataPath + "/Player/") : m_npcTexPath;

  // Upload skeleton meshes (for single-model NPCs like Smith, Wizard, Storage)
  if (skeletonHasMeshes) {
    AABB aabb{};
    NpcInstance::BodyPart bp;
    bp.bmdIdx = -1; // skeleton
    for (auto &mesh : mdl.skeleton->Meshes) {
      UploadMeshWithBones(mesh, texDir, bones, bp.meshBuffers, aabb, true);
    }
    npc.bodyParts.push_back(std::move(bp));
  }

  // Upload body part meshes (for multi-part NPCs like Man, Girl, Female,
  // Guards)
  for (int pi = 0; pi < (int)mdl.parts.size(); ++pi) {
    AABB aabb{};
    NpcInstance::BodyPart bp;
    bp.bmdIdx = pi;
    for (auto &mesh : mdl.parts[pi]->Meshes) {
      UploadMeshWithBones(mesh, texDir, bones, bp.meshBuffers, aabb, true);
    }
    npc.bodyParts.push_back(std::move(bp));
  }

  // Upload weapon meshes (guards only)
  if (mdl.weaponBmd) {
    std::string weaponTexDir = m_dataPath + "/Item/";
    auto wBones = ComputeBoneMatrices(mdl.weaponBmd);
    AABB wAabb{};
    for (auto &mesh : mdl.weaponBmd->Meshes) {
      UploadMeshWithBones(mesh, weaponTexDir, wBones, npc.weaponMeshBuffers,
                          wAabb, true);
    }
    // Create weapon shadow mesh buffers
    for (auto &mb : npc.weaponMeshBuffers) {
      NpcInstance::ShadowMesh sm;
      sm.vertexCount = mb.vertexCount;
      if (sm.vertexCount == 0) {
        npc.weaponShadowMeshes.push_back(sm);
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
      npc.weaponShadowMeshes.push_back(sm);
    }
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
  m_dataPath = dataPath;

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

  // ── Guard NPCs (Main 5.2: ZzzCharacter.cpp:13859-13890) ──
  // Guards use Player.bmd skeleton + armor set 9 (heavy plate)
  std::string playerPath = dataPath + "/Player/";
  std::string itemPath = dataPath + "/Item/";

  // Type 249: Berdysh Guard (spear, right hand bone 33)
  int berdyshIdx =
      loadModel(playerPath, "player.bmd",
                {"HelmMale09.bmd", "ArmorMale09.bmd", "PantMale09.bmd",
                 "GloveMale09.bmd", "BootMale09.bmd"},
                "BerdyshGuard");
  if (berdyshIdx >= 0) {
    auto spearBmd = BMDParser::Parse(itemPath + "Spear07.bmd");
    if (spearBmd) {
      m_models[berdyshIdx].weaponBmd = spearBmd.get();
      m_models[berdyshIdx].weaponAttachBone = 33; // Right hand
      m_models[berdyshIdx].defaultAction = 4;     // PLAYER_STOP_SWORD
      m_ownedBmds.push_back(std::move(spearBmd));
    }
    m_typeToModel[249] = berdyshIdx;
  }

  // Type 247: Crossbow Guard (bow, left hand bone 42)
  int crossbowIdx =
      loadModel(playerPath, "player.bmd",
                {"HelmMale09.bmd", "ArmorMale09.bmd", "PantMale09.bmd",
                 "GloveMale09.bmd", "BootMale09.bmd"},
                "CrossbowGuard");
  if (crossbowIdx >= 0) {
    auto bowBmd = BMDParser::Parse(itemPath + "Bow07.bmd");
    if (bowBmd) {
      m_models[crossbowIdx].weaponBmd = bowBmd.get();
      m_models[crossbowIdx].weaponAttachBone = 42; // Left hand
      m_models[crossbowIdx].defaultAction = 12;    // PLAYER_STOP_CROSSBOW
      m_ownedBmds.push_back(std::move(bowBmd));
    }
    m_typeToModel[247] = crossbowIdx;
  }

  m_modelsLoaded = true;
  std::cout << "[NPC] Models loaded: " << m_models.size() << " types, "
            << m_typeToModel.size() << " type mappings" << std::endl;
}

void NpcManager::AddNpcByType(uint16_t npcType, uint8_t gridX, uint8_t gridY,
                              uint8_t dir, uint16_t serverIndex) {
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

  // Set type, name, and server index on the just-added NPC
  auto &added = m_npcs.back();
  added.npcType = npcType;
  added.serverIndex = serverIndex;
  auto nameIt = s_npcNames.find(npcType);
  if (nameIt != s_npcNames.end())
    added.name = nameIt->second;

  // Guard walk actions (Player.bmd action indices from _enum.h)
  // Type 249 (Berdysh): idle=4 (PLAYER_STOP_SWORD), walk=5 (PLAYER_WALK_SWORD)
  // Type 247 (Crossbow): idle=12 (PLAYER_STOP_CROSSBOW), walk=13
  // (PLAYER_WALK_CROSSBOW)
  if (npcType == 249)
    added.walkAction = 5;
  else if (npcType == 247)
    added.walkAction = 13;

  std::cout << "[NPC] Server-spawned NPC type=" << npcType
            << " idx=" << serverIndex << " at grid (" << (int)gridX << ","
            << (int)gridY << ") dir=" << (int)dir << std::endl;
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
      // Guards use Player.bmd which has faster PlaySpeed (0.30*25=7.5 fps)
      float speed = (npc.walkAction > 0) ? 7.5f : ANIM_SPEED;
      npc.animFrame += speed * deltaTime;
      if (npc.animFrame >= (float)numKeys) {
        npc.animFrame = std::fmod(npc.animFrame, (float)numKeys);
        // NPC action switching (Main 5.2: ZzzCharacter.cpp:3352-3358)
        // Blacksmith: 75% action 0 (hammering), 25% action 1-2
        if (npc.npcType == 251) {
          int numActions = (int)mdl.skeleton->Actions.size();
          if (rand() % 4 == 0 && numActions > 1) {
            npc.action = 1 + rand() % std::min(2, numActions - 1);
          } else {
            npc.action = 0;
          }
          npc.animFrame = 0.0f;
        }
      }
    }

    // Guard patrol movement: interpolate toward move target
    if (npc.isMoving) {
      glm::vec3 diff = npc.moveTarget - npc.position;
      diff.y = 0.0f; // Only move in XZ plane
      float dist = glm::length(diff);
      // Server uses GUARD_WANDER_SPEED = 150 units/sec
      float step = 150.0f * deltaTime;
      if (dist <= step || dist < 1.0f) {
        // Arrived at target
        npc.position.x = npc.moveTarget.x;
        npc.position.z = npc.moveTarget.z;
        npc.position.y = snapToTerrain(npc.position.x, npc.position.z);
        npc.isMoving = false;
        // Switch back to idle action
        npc.action = mdl.defaultAction;
        npc.animFrame = 0.0f;
      } else {
        glm::vec3 dir = diff / dist;
        float nextX = npc.position.x + dir.x * step;
        float nextZ = npc.position.z + dir.z * step;

        bool blocked = false;
        if (m_terrainData) {
          int gy = (int)(nextX / 100.0f);
          int gx = (int)(nextZ / 100.0f);
          const int S = TerrainParser::TERRAIN_SIZE;
          if (gx >= 0 && gy >= 0 && gx < S && gy < S) {
            uint8_t attr = m_terrainData->mapping.attributes[gy * S + gx];
            if ((attr & 0x04) != 0) {
              blocked = true;
            }
          }
        }

        if (blocked) {
          npc.isMoving = false;
          npc.action = mdl.defaultAction;
          npc.animFrame = 0.0f;
        } else {
          npc.position.x = nextX;
          npc.position.z = nextZ;
          npc.position.y = snapToTerrain(npc.position.x, npc.position.z);
          // Update facing toward movement direction
          npc.facing = std::atan2(dir.x, dir.z);
        }
      }
    }

    // Compute bone matrices
    auto bones = ComputeBoneMatricesInterpolated(mdl.skeleton, npc.action,
                                                 npc.animFrame);
    npc.cachedBones = bones;

    // ── Blacksmith VFX (Main 5.2: ZzzCharacter.cpp:5917-5939) ──
    // MODEL_SMITH (NPC type 251): sparks from bone 17 during hammer frames 5-6
    if (npc.npcType == 251 && m_vfxManager && npc.action == 0 &&
        npc.animFrame >= 5.0f && npc.animFrame <= 6.0f) {
      // Get bone 17 (hammer hand) position in model-local space
      const int HAMMER_BONE = 17;
      if (HAMMER_BONE < (int)bones.size()) {
        // Bone position is the translation column of the 3x4 matrix
        glm::vec3 boneLocal(bones[HAMMER_BONE][0][3], bones[HAMMER_BONE][1][3],
                            bones[HAMMER_BONE][2][3]);

        // Transform from BMD-local to world space:
        // Apply model rotation: rotate(-90°Z) * rotate(-90°Y) * rotate(facing)
        float c1 = std::cos(glm::radians(-90.0f));
        float s1 = std::sin(glm::radians(-90.0f));
        // rotate(-90°Z): (x,y,z) -> (y, -x, z)
        glm::vec3 r1(boneLocal.y, -boneLocal.x, boneLocal.z);
        // rotate(-90°Y): (x,y,z) -> (z, y, -x)
        glm::vec3 r2(r1.z, r1.y, -r1.x);

        // Fix: Adjust facing for guards (-90 degrees to align correctly)
        float adjustedFacing = npc.facing - glm::radians(90.0f);
        float cf = std::cos(adjustedFacing);
        float sf = std::sin(adjustedFacing);
        glm::vec3 r3(r2.x * cf - r2.y * sf, r2.x * sf + r2.y * cf, r2.z);
        // Apply scale and translate to world
        glm::vec3 sparkPos = npc.position + r3 * npc.scale;

        // Spawn 4 spark particles (Main 5.2: CreateJoint + CreateParticle x4)
        m_vfxManager->SpawnBurst(ParticleType::HIT_SPARK, sparkPos, 4);
      }
    }

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
    // Fix: Rotate -90 degrees around Z to align guards correctly
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, npc.facing - glm::radians(90.0f),
                        glm::vec3(0, 0, 1));
    if (npc.scale != 1.0f)
      model = glm::scale(model, glm::vec3(npc.scale));

    m_shader->setMat4("model", model);

    // Terrain lightmap at NPC position
    glm::vec3 tLight = sampleTerrainLightAt(npc.position);
    m_shader->setVec3("terrainLight", tLight);

    // Blacksmith forge glow: BlendMesh=4, Luminosity=0.8 constant
    // Main 5.2: o->BlendMesh = 4; o->BlendMeshLight = Luminosity;
    // Luminosity for Lorencia is a constant 0.8f (ZzzCharacter.cpp:5500)
    bool isBlacksmith = (npc.npcType == 251);
    float forgeLum = 1.0f;
    if (isBlacksmith) {
      forgeLum = 0.8f;
      m_shader->setFloat("blendMeshLight", forgeLum);
    }

    // Draw all body part meshes
    int meshDrawIdx = 0;
    for (auto &bp : npc.bodyParts) {
      for (auto &mb : bp.meshBuffers) {
        if (mb.indexCount == 0 || mb.hidden) {
          meshDrawIdx++;
          continue;
        }
        glBindTexture(GL_TEXTURE_2D, mb.texture);
        glBindVertexArray(mb.vao);

        // Blacksmith mesh 4 = forge glow (additive blend)
        bool forgeGlow = isBlacksmith && (mb.bmdTextureId == 4);

        if (forgeGlow || mb.bright) {
          glBlendFunc(GL_ONE, GL_ONE);
          glDepthMask(GL_FALSE);
          glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
          glDepthMask(GL_TRUE);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else if (mb.noneBlend) {
          glDisable(GL_BLEND);
          glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
          glEnable(GL_BLEND);
        } else {
          glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        }
        meshDrawIdx++;
      }
    }

    // Reset blendMeshLight after blacksmith
    if (isBlacksmith) {
      m_shader->setFloat("blendMeshLight", 1.0f);
    }

    // ── Guard weapon rendering (Main 5.2: ZzzCharacter.cpp:13859-13890) ──
    if (mdl.weaponBmd && mdl.weaponAttachBone >= 0 &&
        !npc.weaponMeshBuffers.empty() &&
        mdl.weaponAttachBone < (int)bones.size()) {
      // Guard weapon attachment: same as HeroCharacter — parentMat = bone *
      // identity offset. The correct idle animation (PLAYER_STOP_SWORD /
      // PLAYER_STOP_CROSSBOW) positions the hands for weapon hold.
      BoneWorldMatrix offsetMat = MuMath::BuildWeaponOffsetMatrix(
          glm::vec3(0.0f), glm::vec3(0.0f));

      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms(
          (const float(*)[4])bones[mdl.weaponAttachBone].data(),
          (const float(*)[4])offsetMat.data(), (float(*)[4])parentMat.data());

      // Compute weapon bone matrices with parentMat as root
      auto wLocalBones = ComputeBoneMatrices(mdl.weaponBmd);
      std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
      for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
        MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                 (const float(*)[4])wLocalBones[bi].data(),
                                 (float(*)[4])wFinalBones[bi].data());
      }

      // Re-skin weapon meshes
      for (int mi = 0; mi < (int)npc.weaponMeshBuffers.size() &&
                       mi < (int)mdl.weaponBmd->Meshes.size();
           ++mi) {
        RetransformMeshWithBones(mdl.weaponBmd->Meshes[mi], wFinalBones,
                                 npc.weaponMeshBuffers[mi]);
      }

      // Draw weapon meshes
      for (auto &mb : npc.weaponMeshBuffers) {
        if (mb.indexCount == 0 || mb.hidden)
          continue;
        glBindTexture(GL_TEXTURE_2D, mb.texture);
        glBindVertexArray(mb.vao);
        if (mb.bright) {
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

void NpcManager::SetNpcMoveTarget(uint16_t serverIndex, float worldX,
                                  float worldZ) {
  for (auto &npc : m_npcs) {
    if (npc.serverIndex == serverIndex) {
      float worldY = snapToTerrain(worldX, worldZ);
      npc.moveTarget = glm::vec3(worldX, worldY, worldZ);
      npc.isMoving = true;
      // Switch to walk action
      if (npc.walkAction > 0)
        npc.action = npc.walkAction;
      return;
    }
  }
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
    // Weapon meshes (guards)
    CleanupMeshBuffers(npc.weaponMeshBuffers);
    for (auto &sm : npc.weaponShadowMeshes) {
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
    ImU32 textCol =
        hovered ? IM_COL32(150, 255, 150, 255) : IM_COL32(200, 200, 200, 255);

    // Fill
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), bgCol, 2.0f);
    // Border
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), borderCol, 2.0f, 0, 1.0f);

    // Shadow
    dl->AddText(ImVec2(sx - textSize.x / 2 + 1, sy - textSize.y / 2 + 1),
                IM_COL32(0, 0, 0, 180), info.name.c_str());
    // Text
    dl->AddText(ImVec2(sx - textSize.x / 2, sy - textSize.y / 2), textCol,
                info.name.c_str());
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
