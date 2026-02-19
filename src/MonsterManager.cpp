#include "MonsterManager.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>

// Monster type → display name
static const std::unordered_map<uint16_t, std::string> s_monsterNames = {
    {0, "Bull Fighter"},
    {1, "Hound"},
    {2, "Budge Dragon"},
    {3, "Spider"},
    {4, "Elite Bull Fighter"},
    {6, "Lich"},
    {7, "Giant"},
    {14, "Skeleton Warrior"}};

glm::vec3
MonsterManager::sampleTerrainLightAt(const glm::vec3 &worldPos) const {
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

float MonsterManager::snapToTerrain(float worldX, float worldZ) {
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

int MonsterManager::loadMonsterModel(const std::string &bmdFile,
                                     const std::string &name, float scale,
                                     float radius, float height,
                                     float bodyOffset) {
  // Check if already loaded
  for (int i = 0; i < (int)m_models.size(); ++i) {
    if (m_models[i].name == name)
      return i;
  }

  std::string fullPath = m_monsterTexPath + bmdFile;
  auto bmd = BMDParser::Parse(fullPath);
  if (!bmd) {
    std::cerr << "[Monster] Failed to load BMD: " << fullPath << std::endl;
    return -1;
  }

  MonsterModel model;
  model.name = name;
  model.bmd = bmd.get();
  model.scale = scale;
  model.collisionRadius = radius;
  model.collisionHeight = height;
  model.bodyOffset = bodyOffset;

  // Find root bone (Parent == -1) for LockPositions handling
  for (int i = 0; i < (int)bmd->Bones.size(); ++i) {
    if (!bmd->Bones[i].Dummy && bmd->Bones[i].Parent == -1) {
      model.rootBone = i;
      break;
    }
  }

  m_ownedBmds.push_back(std::move(bmd));

  int idx = (int)m_models.size();
  m_models.push_back(std::move(model));
  auto *loadedBmd = m_models[idx].bmd;
  std::cout << "[Monster] Loaded model '" << name << "' ("
            << loadedBmd->Bones.size() << " bones, " << loadedBmd->Meshes.size()
            << " meshes, " << loadedBmd->Actions.size()
            << " actions, rootBone=" << m_models[idx].rootBone << ")"
            << std::endl;

  // Pre-upload mesh buffers using identity bones (for debris and shared use)
  auto identityBones = ComputeBoneMatrices(loadedBmd);
  AABB dummyAABB{};
  for (auto &mesh : loadedBmd->Meshes) {
    UploadMeshWithBones(mesh, m_monsterTexPath, identityBones,
                        m_models[idx].meshBuffers, dummyAABB, true);
  }

  // Log LockPositions for walk action (ACTION_WALK=2)
  if (ACTION_WALK < (int)loadedBmd->Actions.size()) {
    std::cout << "[Monster]   Walk action " << ACTION_WALK
              << ": keys=" << loadedBmd->Actions[ACTION_WALK].NumAnimationKeys
              << " LockPositions="
              << loadedBmd->Actions[ACTION_WALK].LockPositions << std::endl;
  }
  return idx;
}

void MonsterManager::InitModels(const std::string &dataPath) {
  if (m_modelsLoaded)
    return;

  m_monsterTexPath = dataPath + "/Monster/";

  // Create shaders (same as NPC — model.vert/frag, shadow.vert/frag)
  std::ifstream shaderTest("shaders/model.vert");
  m_shader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/model.vert" : "../shaders/model.vert",
      shaderTest.good() ? "shaders/model.frag" : "../shaders/model.frag");

  m_shadowShader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/shadow.vert" : "../shaders/shadow.vert",
      shaderTest.good() ? "shaders/shadow.frag" : "../shaders/shadow.frag");

  // Bull Fighter: server type 0, Monster01.bmd (CreateMonsterClient: scale 0.8)
  // BBox: (-60,-60,0) to (50,50,150) — default
  int bullIdx =
      loadMonsterModel("Monster01.bmd", "Bull Fighter", 0.8f, 80.0f, 150.0f);
  if (bullIdx >= 0) {
    auto &bull = m_models[bullIdx];
    bull.level = 6;       // Monster.txt: Level=6
    bull.defense = 6;     // Monster.txt: Defense=6
    bull.defenseRate = 6; // Monster.txt: DefRate=6
    bull.attackRate = 28; // Monster.txt: AtkRate=28
  }
  m_typeToModel[0] = bullIdx;

  // Hound: server type 1, Monster02.bmd (CreateMonsterClient: scale 0.85)
  // BBox: (-60,-60,0) to (50,50,150) — default
  int houndIdx =
      loadMonsterModel("Monster02.bmd", "Hound", 0.85f, 80.0f, 150.0f);
  if (houndIdx >= 0) {
    auto &hound = m_models[houndIdx];
    hound.level = 9;       // Monster.txt: Level=9
    hound.defense = 9;     // Monster.txt: Defense=9
    hound.defenseRate = 9; // Monster.txt: DefRate=9
    hound.attackRate = 39; // Monster.txt: AtkRate=39
  }
  m_typeToModel[1] = houndIdx;

  // Budge Dragon: server type 2, Monster03.bmd (CreateMonsterClient: scale 0.5)
  // BBox: (-60,-60,0) to (50,50,80) — flying type, NO bodyOffset (hover handles
  // height)
  int budgeIdx =
      loadMonsterModel("Monster03.bmd", "Budge Dragon", 0.5f, 70.0f, 80.0f);
  if (budgeIdx >= 0) {
    auto &budge = m_models[budgeIdx];
    budge.level = 4;       // Monster.txt: Level=4
    budge.defense = 3;     // Monster.txt: Defense=3
    budge.defenseRate = 3; // Monster.txt: DefRate=3
    budge.attackRate = 18; // Monster.txt: AtkRate=18
  }
  m_typeToModel[2] = budgeIdx;

  // Spider: server type 3, Monster10.bmd (CreateMonsterClient: scale 0.4,
  // OpenMonsterModel(9)) BBox: (-60,-60,0) to (50,50,80) — NO bodyOffset
  // (BodyHeight=0 in original)
  int spiderIdx =
      loadMonsterModel("Monster10.bmd", "Spider", 0.4f, 70.0f, 80.0f);
  if (spiderIdx >= 0) {
    auto &spider = m_models[spiderIdx];
    spider.level = 2;       // Monster.txt: Level=2
    spider.defense = 1;     // Monster.txt: Defense=1
    spider.defenseRate = 1; // Monster.txt: DefRate=1
    spider.attackRate = 8;  // Monster.txt: AtkRate=8
  }
  m_typeToModel[3] = spiderIdx;

  // Elite Bull Fighter: server type 4, Monster01.bmd (Scale 1.15)
  // Shared with Bull Fighter model but different scale
  m_typeToModel[4] = bullIdx; // Use the same loaded model index
  if (bullIdx >= 0) {
    // We need to handle per-type scale if we share models.
    // For now, let's just use the same model but I'll add a way to override
    // scale per instance if needed. Actually, Bull Fighter and Elite Bull
    // Fighter share the same BMD but have different server types. I will adjust
    // the spawning logic to use the correct scale.
  }

  // Lich: server type 6, Monster05.bmd (scale 0.85, ranged caster)
  int lichIdx = loadMonsterModel("Monster05.bmd", "Lich", 0.85f, 80.0f, 150.0f);
  if (lichIdx >= 0) {
    auto &lich = m_models[lichIdx];
    lich.level = 14;
    lich.defense = 18;
    lich.defenseRate = 18;
    lich.attackRate = 72;
  }
  m_typeToModel[6] = lichIdx;

  // Giant: server type 7, Monster06.bmd (scale 1.6, large and slow)
  int giantIdx =
      loadMonsterModel("Monster06.bmd", "Giant", 1.6f, 120.0f, 200.0f);
  if (giantIdx >= 0) {
    auto &giant = m_models[giantIdx];
    giant.level = 17;
    giant.defense = 25;
    giant.defenseRate = 25;
    giant.attackRate = 89;
  }
  m_typeToModel[7] = giantIdx;

  // Skeleton Warrior: server type 14, Monster48.bmd (from 5.2 reference) (scale
  // 0.95)
  int skelIdx = loadMonsterModel("Monster48.bmd", "Skeleton Warrior", 0.95f,
                                 80.0f, 150.0f);
  if (skelIdx >= 0) {
    auto &skel = m_models[skelIdx];
    skel.level = 19;
    skel.defense = 30;
    skel.defenseRate = 30;
    skel.attackRate = 105;
  }
  m_typeToModel[14] = skelIdx;

  // Load Debris models (not mapped to server types)
  std::string skillPath = dataPath + "/Skill/";
  m_boneModelIdx =
      loadMonsterModel("../Skill/Bone01.bmd", "Bone Debris", 0.5f, 0, 0);
  m_stoneModelIdx =
      loadMonsterModel("../Skill/BigStone01.bmd", "Stone Debris", 0.6f, 0, 0);

  m_modelsLoaded = true;
  std::cout << "[Monster] Models loaded: " << m_models.size() << " types"
            << std::endl;
}

void MonsterManager::AddMonster(uint16_t monsterType, uint8_t gridX,
                                uint8_t gridY, uint8_t dir,
                                uint16_t serverIndex) {
  auto it = m_typeToModel.find(monsterType);
  if (it == m_typeToModel.end()) {
    std::cerr << "[Monster] Unknown monster type " << monsterType << " at ("
              << (int)gridX << "," << (int)gridY << "), skipping" << std::endl;
    return;
  }
  int modelIdx = it->second;
  if (modelIdx < 0 || modelIdx >= (int)m_models.size())
    return;

  auto &mdl = m_models[modelIdx];
  MonsterInstance mon;
  mon.modelIdx = modelIdx;
  mon.scale = mdl.scale;
  mon.monsterType = monsterType;
  mon.serverIndex = serverIndex;

  // Name
  auto nameIt = s_monsterNames.find(monsterType);
  if (nameIt != s_monsterNames.end())
    mon.name = nameIt->second;

  // Grid to world: WorldX = gridY * 100, WorldZ = gridX * 100
  // Small random offset to prevent stacking
  float randX = ((float)(rand() % 60) - 30.0f);
  float randZ = ((float)(rand() % 60) - 30.0f);
  float worldX = (float)gridY * 100.0f + randX;
  float worldZ = (float)gridX * 100.0f + randZ;
  float worldY = snapToTerrain(worldX, worldZ) + mdl.bodyOffset;
  mon.position = glm::vec3(worldX, worldY, worldZ);
  mon.spawnPosition = mon.position;

  // Direction to facing angle (same as NPC: dir-1 * 45°)
  mon.facing = (float)(dir - 1) * (float)M_PI / 4.0f;

  // Random bob timer offset so monsters don't bob in sync
  mon.bobTimer = (float)(m_monsters.size() * 1.7f);

  // Random animation offset so monsters don't sync
  mon.animFrame = (float)(m_monsters.size() * 2.3f);

  // Compute initial bone matrices
  auto bones = ComputeBoneMatrices(mdl.bmd);

  // Upload meshes (single BMD, not multi-part)
  AABB aabb{};
  for (auto &mesh : mdl.bmd->Meshes) {
    UploadMeshWithBones(mesh, m_monsterTexPath, bones, mon.meshBuffers, aabb,
                        true);
  }

  // Create shadow mesh buffers — sized for triangle-expanded vertices
  for (int mi = 0;
       mi < (int)mdl.bmd->Meshes.size() && mi < (int)mon.meshBuffers.size();
       ++mi) {
    auto &mesh = mdl.bmd->Meshes[mi];
    MonsterInstance::ShadowMesh sm;
    // Count actual shadow vertices: 3 per tri, 6 per quad
    int shadowVertCount = 0;
    for (int t = 0; t < mesh.NumTriangles; ++t) {
      shadowVertCount += (mesh.Triangles[t].Polygon == 4) ? 6 : 3;
    }
    sm.vertexCount = shadowVertCount;
    if (sm.vertexCount == 0) {
      mon.shadowMeshes.push_back(sm);
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
    mon.shadowMeshes.push_back(sm);
  }

  m_monsters.push_back(std::move(mon));
  std::cout << "[Monster] Spawned type=" << monsterType << " at grid ("
            << (int)gridX << "," << (int)gridY << ")" << std::endl;
}

void MonsterManager::setAction(MonsterInstance &mon, int action) {
  if (mon.action == action)
    return;

  // Trigger blending for ALL animation changes
  mon.priorAction = mon.action;
  mon.priorAnimFrame = mon.animFrame;
  mon.isBlending = true;
  mon.blendAlpha = 0.0f;

  mon.action = action;
  mon.animFrame = 0.0f;
}

// Per-action animation speed with per-type overrides (ZzzOpenData.cpp
// OpenMonsterModel)
float MonsterManager::getAnimSpeed(uint16_t monsterType, int action) const {
  float speed;
  switch (action) {
  case ACTION_STOP1:
    speed = 0.25f;
    break;
  case ACTION_STOP2:
    speed = 0.20f;
    break;
  case ACTION_WALK:
    speed = 0.34f;
    break;
  case ACTION_ATTACK1:
  case ACTION_ATTACK2:
    speed = 0.33f;
    break;
  case ACTION_SHOCK:
    speed = 0.50f;
    break;
  case ACTION_DIE:
    speed = 0.55f;
    break;
  default:
    speed = 0.25f;
    break;
  }

  // Global per-type multipliers (ZzzOpenData.cpp:2370-2376)
  if (monsterType == 3) { // Spider
    speed *= 1.2f;
  } else if (monsterType == 5 ||
             monsterType == 25) { // Larva / Golem variations
    speed *= 0.7f;
  }

  // Specific action overrides
  if (action == ACTION_WALK) {
    if (monsterType == 2)
      speed = 0.7f; // Budge Dragon (flying)
    // Add other specific walk speeds if needed
  }

  return speed * 25.0f; // Scale to 25fps base
}

// Smooth facing interpolation matching original MU TurnAngle2:
// - If angular error >= 45° (pi/4): snap to target (large correction)
// - Otherwise: exponential decay at 0.5^(dt*25) rate (half remaining error per
// 25fps frame)
static float smoothFacing(float current, float target, float dt) {
  float diff = target - current;
  // Normalize to [-PI, PI]
  while (diff > (float)M_PI)
    diff -= 2.0f * (float)M_PI;
  while (diff < -(float)M_PI)
    diff += 2.0f * (float)M_PI;

  if (std::abs(diff) >= (float)M_PI / 4.0f) {
    return target; // Snap for large turns (original: >= 45°)
  }
  // Exponential decay: 0.5^(dt*25) matches original half-error-per-frame at
  // 25fps
  float factor = 1.0f - std::pow(0.5f, dt * 25.0f);
  float result = current + diff * factor;
  // Normalize result
  while (result > (float)M_PI)
    result -= 2.0f * (float)M_PI;
  while (result < -(float)M_PI)
    result += 2.0f * (float)M_PI;
  return result;
}

// Compute facing angle from movement direction (OpenGL coords)
static float facingFromDir(const glm::vec3 &dir) {
  return atan2f(dir.z, -dir.x);
}

void MonsterManager::updateStateMachine(MonsterInstance &mon, float dt) {
  auto &mdl = m_models[mon.modelIdx];

  // Original MU arrival threshold: 20 world units (0.2 grid cells)
  static constexpr float ARRIVAL_DIST = 20.0f;

  switch (mon.state) {
  case MonsterState::IDLE: {
    // If we just entered IDLE or finished an idle cycle, pick a new action and
    // duration
    if (mon.stateTimer <= 0.0f) {
      // 80% chance for STOP1, 20% for STOP2 (matches original MU feel)
      int nextIdle = (rand() % 100 < 80) ? ACTION_STOP1 : ACTION_STOP2;
      setAction(mon, nextIdle);
      // Stay in this idle action for 2-5 seconds
      mon.stateTimer = 2.0f + static_cast<float>(rand() % 3000) / 1000.0f;
    }

    float terrainY = snapToTerrain(mon.position.x, mon.position.z);
    mon.position.y = terrainY + mdl.bodyOffset;
    // Budge Dragon hover (ZzzCharacter.cpp:6224): -abs(sin(Timer))*70+70
    if (mon.monsterType == 2) {
      mon.bobTimer += dt * 3.75f;
      mon.position.y += (-std::abs(std::sin(mon.bobTimer)) * 30.0f + 30.0f);
    }
    mon.stateTimer -= dt;
    break;
  }

  case MonsterState::WALKING: {
    setAction(mon, ACTION_WALK);

    glm::vec3 dir = mon.wanderTarget - mon.position;
    dir.y = 0.0f;
    float dist = glm::length(dir);

    if (dist < ARRIVAL_DIST || mon.stateTimer <= 0.0f) {
      mon.state = MonsterState::IDLE;
      mon.stateTimer = 0.0f; // Reset to pick new idle action immediately
    } else {
      glm::vec3 moveDir = glm::normalize(dir);
      float step = WANDER_SPEED * dt;
      if (step > dist)
        step = dist;
      mon.position += moveDir * step;
      float tY = snapToTerrain(mon.position.x, mon.position.z);
      mon.position.y = tY + mdl.bodyOffset;
      mon.facing = smoothFacing(mon.facing, facingFromDir(moveDir), dt);
    }
    if (mon.monsterType == 2) {
      mon.bobTimer += dt * 3.75f;
      mon.position.y += (-std::abs(std::sin(mon.bobTimer)) * 30.0f + 30.0f);
    }
    mon.stateTimer -= dt;
    break;
  }

  case MonsterState::CHASING: {
    mon.serverPosAge += dt;

    if (mon.serverPosAge > 15.0f) {
      // Safety timeout — no server update in 15s, return to spawn
      mon.serverChasing = false;
      mon.state = MonsterState::WALKING;
      mon.wanderTarget = mon.spawnPosition;
      mon.stateTimer = 15.0f;
      setAction(mon, ACTION_WALK);
      break;
    }

    // Chase toward actual player position (smooth) instead of quantized grid
    // cell
    glm::vec3 chaseTarget = m_playerDead ? mon.serverTargetPos : m_playerPos;
    glm::vec3 toTarget = chaseTarget - mon.position;
    toTarget.y = 0.0f;
    float distToTarget = glm::length(toTarget);

    // Within melee range — stand and face player (idle), waiting for next
    // attack
    static constexpr float MELEE_IDLE_RANGE = 200.0f;
    if (distToTarget <= MELEE_IDLE_RANGE) {
      setAction(mon, ACTION_STOP1);
      if (!m_playerDead) {
        glm::vec3 toPlayer = m_playerPos - mon.position;
        toPlayer.y = 0.0f;
        if (glm::length(toPlayer) > 1.0f) {
          glm::vec3 fdir = glm::normalize(toPlayer);
          mon.facing = smoothFacing(mon.facing, facingFromDir(fdir), dt);
        }
      }
    } else if (distToTarget > ARRIVAL_DIST) {
      setAction(mon, ACTION_WALK);
      glm::vec3 moveDir = glm::normalize(toTarget);
      float step = CHASE_SPEED * dt;
      if (step > distToTarget)
        step = distToTarget;
      mon.position += moveDir * step;
      mon.facing = smoothFacing(mon.facing, facingFromDir(moveDir), dt);
    } else {
      setAction(mon, ACTION_STOP1);
      // At target — face the player
      if (!m_playerDead) {
        glm::vec3 toPlayer = m_playerPos - mon.position;
        toPlayer.y = 0.0f;
        if (glm::length(toPlayer) > 1.0f) {
          glm::vec3 fdir = glm::normalize(toPlayer);
          mon.facing = smoothFacing(mon.facing, facingFromDir(fdir), dt);
        }
      }
    }
    // Always snap Y to terrain (fixes hover accumulation bug for Budge Dragon)
    mon.position.y =
        snapToTerrain(mon.position.x, mon.position.z) + mdl.bodyOffset;
    if (mon.monsterType == 2) {
      mon.bobTimer += dt * 3.75f;
      mon.position.y += (-std::abs(std::sin(mon.bobTimer)) * 30.0f + 30.0f);
    }
    break;
  }

  case MonsterState::ATTACKING: {
    // Face the player during attack
    if (!m_playerDead) {
      glm::vec3 toPlayer = m_playerPos - mon.position;
      toPlayer.y = 0.0f;
      if (glm::length(toPlayer) > 1.0f) {
        glm::vec3 dir = glm::normalize(toPlayer);
        mon.facing = smoothFacing(mon.facing, facingFromDir(dir), dt);
      }
    }
    // Maintain Y position (terrain + bodyOffset + hover for flying types)
    mon.position.y =
        snapToTerrain(mon.position.x, mon.position.z) + mdl.bodyOffset;
    if (mon.monsterType == 2) {
      mon.bobTimer += dt * 3.75f;
      mon.position.y += (-std::abs(std::sin(mon.bobTimer)) * 30.0f + 30.0f);
    }
    mon.stateTimer -= dt;
    if (mon.stateTimer <= 0.0f) {
      if (mon.serverChasing) {
        mon.state = MonsterState::CHASING;
      } else {
        // Resume walking if we have a wander target, else idle
        float distToWander = glm::length(mon.wanderTarget - mon.position);
        if (distToWander > ARRIVAL_DIST) {
          mon.state = MonsterState::WALKING;
        } else {
          mon.state = MonsterState::IDLE;
        }
      }
    }
    break;
  }

  case MonsterState::HIT: {
    setAction(mon, ACTION_SHOCK);
    // Maintain Y position during hit stun
    mon.position.y =
        snapToTerrain(mon.position.x, mon.position.z) + mdl.bodyOffset;
    if (mon.monsterType == 2) {
      mon.bobTimer += dt * 3.75f;
      mon.position.y += (-std::abs(std::sin(mon.bobTimer)) * 30.0f + 30.0f);
    }
    mon.stateTimer -= dt;
    if (mon.stateTimer <= 0.0f) {
      if (mon.serverChasing) {
        mon.state = MonsterState::CHASING;
      } else {
        mon.state = MonsterState::IDLE;
      }
    }
    break;
  }

  case MonsterState::DYING: {
    setAction(mon, ACTION_DIE);
    // On death: snap to terrain + bodyOffset, no hover (ZzzCharacter.cpp:6285)
    mon.position.y =
        snapToTerrain(mon.position.x, mon.position.z) + mdl.bodyOffset;

    int numKeys = 1;
    if (ACTION_DIE < (int)mdl.bmd->Actions.size())
      numKeys = mdl.bmd->Actions[ACTION_DIE].NumAnimationKeys;
    if (mon.animFrame >= (float)(numKeys - 1)) {
      mon.animFrame = (float)(numKeys - 1);
      mon.state = MonsterState::DEAD;
      mon.stateTimer = 0.0f;
    }
    break;
  }

  case MonsterState::DEAD: {
    mon.corpseTimer += dt;
    if (mon.corpseTimer < CORPSE_FADE_TIME) {
      mon.corpseAlpha = 1.0f - (mon.corpseTimer / CORPSE_FADE_TIME);
    } else {
      mon.corpseAlpha = 0.0f;
    }
    break;
  }

  default:
    mon.state = MonsterState::IDLE;
    break;
  }
}

void MonsterManager::Update(float deltaTime) {
  int idx = 0;
  for (auto &mon : m_monsters) {
    // Safety: if HP is 0 but monster isn't dying/dead, force death
    // (catches missed 0x2A packets or race conditions)
    if (mon.hp <= 0 && mon.state != MonsterState::DYING &&
        mon.state != MonsterState::DEAD) {
      std::cout << "[Client] Mon " << idx << " (" << mon.name
                << "): HP=0 but state=" << (int)mon.state << ", forcing DYING"
                << std::endl;
      mon.state = MonsterState::DYING;
      mon.stateTimer = 0.0f;
      setAction(mon, ACTION_DIE);
    }

    // Save position before update for stuck/stutter detection
    glm::vec3 posBefore = mon.position;

    updateStateMachine(mon, deltaTime);

    // Stuck + stutter detection for WALKING/CHASING monsters
    if (mon.state == MonsterState::WALKING ||
        mon.state == MonsterState::CHASING) {
      glm::vec3 delta = mon.position - posBefore;
      delta.y = 0.0f; // Ignore vertical (terrain snap)
      float moveLen = glm::length(delta);

      // Stuck detection: walking but not moving
      if (moveLen < 0.01f) {
        mon.stutterScore += deltaTime * 2.0f; // Accumulate stuck time
      } else {
        // Stutter detection: direction reversals
        float prevLen = glm::length(mon.prevDelta);
        if (moveLen > 0.1f && prevLen > 0.1f) {
          float dot =
              glm::dot(glm::normalize(delta), glm::normalize(mon.prevDelta));
          if (dot < -0.5f) {
            mon.stutterScore += 1.0f;
          } else {
            mon.stutterScore =
                std::max(0.0f, mon.stutterScore - deltaTime * 2.0f);
          }
        }
      }

      mon.prevDelta = delta;
      mon.prevPosition = posBefore;

      // Log stuck/stutter warnings (throttled)
      mon.stutterLogTimer -= deltaTime;
      if (mon.stutterScore > 3.0f && mon.stutterLogTimer <= 0.0f) {
        mon.stutterLogTimer = 2.0f;
        std::cout << "[STUCK] Mon " << idx << " (" << mon.name
                  << "): score=" << mon.stutterScore
                  << " state=" << (int)mon.state << " hp=" << mon.hp << " pos=("
                  << mon.position.x << "," << mon.position.z << ")"
                  << " target=(" << mon.wanderTarget.x << ","
                  << mon.wanderTarget.z << ")"
                  << " serverAge=" << mon.serverPosAge << std::endl;
      }
    } else {
      mon.stutterScore = 0.0f;
      mon.prevDelta = glm::vec3(0.0f);
    }
    idx++;
  }
  updateDebris(deltaTime);
}

void MonsterManager::Render(const glm::mat4 &view, const glm::mat4 &proj,
                            const glm::vec3 &camPos, float deltaTime) {
  if (!m_shader || m_monsters.empty())
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

  // Disable face culling — spider legs are thin planar geometry visible from
  // both sides
  glDisable(GL_CULL_FACE);

  for (auto &mon : m_monsters) {
    // Skip fully faded corpses
    if (mon.state == MonsterState::DEAD && mon.corpseAlpha <= 0.01f)
      continue;

    auto &mdl = m_models[mon.modelIdx];

    // Advance animation
    int numKeys = 1;
    bool lockPos = false;
    if (mon.action >= 0 && mon.action < (int)mdl.bmd->Actions.size()) {
      numKeys = mdl.bmd->Actions[mon.action].NumAnimationKeys;
      lockPos = mdl.bmd->Actions[mon.action].LockPositions;
    }
    if (numKeys > 1) {
      float animSpeed = getAnimSpeed(mon.monsterType, mon.action);

      // Scale walk animation speed to match actual movement speed.
      // Original MU MoveSpeed=400 for all Lorencia monsters — animation was
      // tuned for that.
      // For spiders (type 3), use a lower reference speed (200) to keep legs
      // moving at a natural pace that matches stop animation.
      float refMoveSpeed = (mon.monsterType == 3) ? 200.0f : 400.0f;

      if (mon.action == ACTION_WALK) {
        if (mon.state == MonsterState::WALKING)
          animSpeed *= WANDER_SPEED / refMoveSpeed;
        else if (mon.state == MonsterState::CHASING)
          animSpeed *= CHASE_SPEED / refMoveSpeed;
      }

      mon.animFrame += animSpeed * deltaTime;

      // Die animation doesn't loop
      if (mon.state == MonsterState::DYING || mon.state == MonsterState::DEAD) {
        if (mon.animFrame >= (float)(numKeys - 1))
          mon.animFrame = (float)(numKeys - 1);
      } else {
        // LockPositions actions wrap at numKeys-1 (last frame == first frame)
        int wrapKeys = lockPos ? (numKeys - 1) : numKeys;
        if (wrapKeys < 1)
          wrapKeys = 1;
        if (mon.animFrame >= (float)wrapKeys)
          mon.animFrame = std::fmod(mon.animFrame, (float)wrapKeys);
      }
    }

    // Advance blending Alpha
    if (mon.isBlending) {
      mon.blendAlpha += deltaTime / mon.BLEND_DURATION;
      if (mon.blendAlpha >= 1.0f) {
        mon.blendAlpha = 1.0f;
        mon.isBlending = false;
      }
    }

    // Compute bone matrices with blending support
    std::vector<BoneWorldMatrix> bones;
    if (mon.isBlending && mon.priorAction != -1) {
      bones = ComputeBoneMatricesBlended(mdl.bmd, mon.priorAction,
                                         mon.priorAnimFrame, mon.action,
                                         mon.animFrame, mon.blendAlpha);
    } else {
      bones =
          ComputeBoneMatricesInterpolated(mdl.bmd, mon.action, mon.animFrame);
    }

    // LockPositions: cancel root bone X/Y displacement to prevent animation
    // from physically moving the model. In blending mode, we interpolate the
    // offset.
    if (mdl.rootBone >= 0) {
      int rb = mdl.rootBone;
      float dx = 0.0f, dy = 0.0f;

      if (mon.isBlending && mon.priorAction != -1) {
        bool lock1 = mon.priorAction < (int)mdl.bmd->Actions.size() &&
                     mdl.bmd->Actions[mon.priorAction].LockPositions;
        bool lock2 = mon.action < (int)mdl.bmd->Actions.size() &&
                     mdl.bmd->Actions[mon.action].LockPositions;

        float dx1 = 0.0f, dy1 = 0.0f, dx2 = 0.0f, dy2 = 0.0f;
        if (lock1) {
          auto &bm1 = mdl.bmd->Bones[rb].BoneMatrixes[mon.priorAction];
          if (!bm1.Position.empty()) {
            glm::vec3 p;
            glm::vec4 q;
            if (GetInterpolatedBoneData(mdl.bmd, mon.priorAction,
                                        mon.priorAnimFrame, rb, p, q)) {
              dx1 = p.x - bm1.Position[0].x;
              dy1 = p.y - bm1.Position[0].y;
            }
          }
        }
        if (lock2) {
          auto &bm2 = mdl.bmd->Bones[rb].BoneMatrixes[mon.action];
          if (!bm2.Position.empty()) {
            glm::vec3 p;
            glm::vec4 q;
            if (GetInterpolatedBoneData(mdl.bmd, mon.action, mon.animFrame, rb,
                                        p, q)) {
              dx2 = p.x - bm2.Position[0].x;
              dy2 = p.y - bm2.Position[0].y;
            }
          }
        }
        dx = dx1 * (1.0f - mon.blendAlpha) + dx2 * mon.blendAlpha;
        dy = dy1 * (1.0f - mon.blendAlpha) + dy2 * mon.blendAlpha;
      } else if (mon.action >= 0 && mon.action < (int)mdl.bmd->Actions.size() &&
                 mdl.bmd->Actions[mon.action].LockPositions) {
        auto &bm = mdl.bmd->Bones[rb].BoneMatrixes[mon.action];
        if (!bm.Position.empty()) {
          dx = bones[rb][0][3] - bm.Position[0].x;
          dy = bones[rb][1][3] - bm.Position[0].y;
        }
      }

      if (dx != 0.0f || dy != 0.0f) {
        for (int b = 0; b < (int)bones.size(); ++b) {
          bones[b][0][3] -= dx;
          bones[b][1][3] -= dy;
        }
      }
    }

    mon.cachedBones = bones;

    // Re-skin meshes
    for (int mi = 0;
         mi < (int)mon.meshBuffers.size() && mi < (int)mdl.bmd->Meshes.size();
         ++mi) {
      RetransformMeshWithBones(mdl.bmd->Meshes[mi], bones, mon.meshBuffers[mi]);
    }

    // Build model matrix
    glm::mat4 model = glm::translate(glm::mat4(1.0f), mon.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, mon.facing, glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(mon.scale));

    m_shader->setMat4("model", model);

    // Terrain lightmap at monster position
    glm::vec3 tLight = sampleTerrainLightAt(mon.position);
    m_shader->setVec3("terrainLight", tLight);

    // Corpse fade via objectAlpha
    m_shader->setFloat("objectAlpha", mon.corpseAlpha);

    // Draw all meshes
    for (auto &mb : mon.meshBuffers) {
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

  // Restore state
  glEnable(GL_CULL_FACE);
  m_shader->setFloat("objectAlpha", 1.0f);

  renderDebris(view, proj, camPos);
}

void MonsterManager::RenderShadows(const glm::mat4 &view,
                                   const glm::mat4 &proj) {
  if (!m_shadowShader || m_monsters.empty())
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
  glEnable(GL_STENCIL_TEST);

  const float sx = 2000.0f;
  const float sy = 4000.0f;

  for (auto &mon : m_monsters) {
    if (mon.cachedBones.empty())
      continue;
    // Skip faded corpses
    if (mon.state == MonsterState::DEAD && mon.corpseAlpha <= 0.01f)
      continue;

    auto &mdl = m_models[mon.modelIdx];

    // Shadow model matrix
    glm::mat4 model = glm::translate(glm::mat4(1.0f), mon.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(mon.scale));

    m_shadowShader->setMat4("model", model);

    // Clear stencil for this monster
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    float cosF = cosf(mon.facing);
    float sinF = sinf(mon.facing);

    for (int mi = 0;
         mi < (int)mdl.bmd->Meshes.size() && mi < (int)mon.shadowMeshes.size();
         ++mi) {
      auto &sm = mon.shadowMeshes[mi];
      if (sm.vertexCount == 0 || sm.vao == 0)
        continue;

      auto &mesh = mdl.bmd->Meshes[mi];
      std::vector<glm::vec3> shadowVerts;
      shadowVerts.reserve(sm.vertexCount);

      for (int i = 0; i < mesh.NumTriangles; ++i) {
        auto &tri = mesh.Triangles[i];
        int steps = (tri.Polygon == 3) ? 3 : 4;
        for (int v = 0; v < 3; ++v) {
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 pos = srcVert.Position;
          int boneIdx = srcVert.Node;
          if (boneIdx >= 0 && boneIdx < (int)mon.cachedBones.size()) {
            pos = MuMath::TransformPoint(
                (const float(*)[4])mon.cachedBones[boneIdx].data(), pos);
          }
          pos *= mon.scale;
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
        if (steps == 4) {
          int quadIndices[3] = {0, 2, 3};
          for (int v : quadIndices) {
            auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
            glm::vec3 pos = srcVert.Position;
            int boneIdx = srcVert.Node;
            if (boneIdx >= 0 && boneIdx < (int)mon.cachedBones.size()) {
              pos = MuMath::TransformPoint(
                  (const float(*)[4])mon.cachedBones[boneIdx].data(), pos);
            }
            pos *= mon.scale;
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

  glBindVertexArray(0);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
}

MonsterInfo MonsterManager::GetMonsterInfo(int index) const {
  MonsterInfo info{};
  if (index < 0 || index >= (int)m_monsters.size())
    return info;
  const auto &mon = m_monsters[index];
  const auto &mdl = m_models[mon.modelIdx];
  info.position = mon.position;
  info.radius = mdl.collisionRadius;
  info.height = mdl.collisionHeight;
  info.bodyOffset = mdl.bodyOffset;
  info.name = mon.name;
  info.type = mon.monsterType;
  info.level = mdl.level;
  info.hp = mon.hp;
  info.maxHp = mon.maxHp;
  info.defense = mdl.defense;
  info.defenseRate = mdl.defenseRate;
  info.state = mon.state;
  return info;
}

uint16_t MonsterManager::GetServerIndex(int index) const {
  if (index < 0 || index >= (int)m_monsters.size())
    return 0;
  return m_monsters[index].serverIndex;
}

int MonsterManager::FindByServerIndex(uint16_t serverIndex) const {
  for (int i = 0; i < (int)m_monsters.size(); i++) {
    if (m_monsters[i].serverIndex == serverIndex)
      return i;
  }
  return -1;
}

void MonsterManager::SetMonsterHP(int index, int hp, int maxHp) {
  if (index < 0 || index >= (int)m_monsters.size())
    return;
  std::cout << "[Client] Mon " << index << " (" << m_monsters[index].name
            << "): HP " << m_monsters[index].hp << " -> " << hp << "/" << maxHp
            << std::endl;
  m_monsters[index].hp = hp;
  m_monsters[index].maxHp = maxHp;
}

void MonsterManager::SetMonsterDying(int index) {
  if (index < 0 || index >= (int)m_monsters.size())
    return;
  auto &mon = m_monsters[index];
  if (mon.state != MonsterState::DYING && mon.state != MonsterState::DEAD) {
    std::cout << "[Client] Mon " << index << " (" << mon.name << "): DYING"
              << std::endl;
    mon.hp = 0;
    mon.state = MonsterState::DYING;
    mon.stateTimer = 0.0f;
    setAction(mon, ACTION_DIE);

    // Spawn death debris (Main 5.2 ZzzCharacter.cpp:1386, 1401, 1412)
    if (mon.monsterType == 14) { // Skeleton Warrior
      spawnDebris(m_boneModelIdx, mon.position + glm::vec3(0, 50, 0), 6);
    } else if (mon.monsterType == 7) { // Giant
      spawnDebris(m_stoneModelIdx, mon.position + glm::vec3(0, 80, 0), 8);
    }
  }
}

void MonsterManager::TriggerHitAnimation(int index) {
  if (index < 0 || index >= (int)m_monsters.size())
    return;
  auto &mon = m_monsters[index];
  if (mon.state == MonsterState::DYING || mon.state == MonsterState::DEAD)
    return;
  std::cout << "[Client] Mon " << index << " (" << mon.name << "): HIT (was "
            << (int)mon.state << ")" << std::endl;
  mon.state = MonsterState::HIT;
  mon.stateTimer = 0.5f;
}

void MonsterManager::TriggerAttackAnimation(int index) {
  if (index < 0 || index >= (int)m_monsters.size())
    return;
  auto &mon = m_monsters[index];
  if (mon.state == MonsterState::DYING || mon.state == MonsterState::DEAD)
    return;
  // Don't override HIT stun — let flinch animation play out before attacking
  // again
  // Don't override HIT stun — let flinch animation play out before attacking
  // again
  // if (mon.state == MonsterState::HIT)
  //   return;
  // Attack packet confirms monster is actively chasing — refresh timeout
  std::cout << "[Client] Mon " << index << " (" << mon.name
            << "): ATTACKING (was " << (int)mon.state << ")" << std::endl;
  mon.serverPosAge = 0.0f;
  mon.serverChasing = true;
  mon.state = MonsterState::ATTACKING;
  // Attack animation duration based on action keys / speed
  auto &mdl = m_models[mon.modelIdx];
  int atk = (rand() % 2 == 0) ? ACTION_ATTACK1 : ACTION_ATTACK2;
  mon.swordCount++;
  int numKeys = 1;
  if (atk < (int)mdl.bmd->Actions.size())
    numKeys = mdl.bmd->Actions[atk].NumAnimationKeys;
  float speed = getAnimSpeed(mon.monsterType, atk);
  mon.stateTimer =
      (numKeys > 1 && speed > 0.0f) ? (float)numKeys / speed : 1.0f;
  setAction(mon, atk);

  // Trigger Lich VFX (Monster Type 6)
  if (mon.monsterType == 6 && m_vfxManager) {
    glm::vec3 startPos = mon.position;
    // Try to get weapon bone position (41)
    if (41 < (int)mon.cachedBones.size()) {
      const auto &m = mon.cachedBones[41];
      startPos = glm::vec3(m[0][3], m[1][3],
                           m[2][3]); // Translation is in the 4th column
    } else {
      startPos.y += 150.0f; // Fallback to head-ish height
    }
    m_vfxManager->SpawnLightning(startPos, m_playerPos);
  }
}

void MonsterManager::RespawnMonster(int index, uint8_t gridX, uint8_t gridY,
                                    int hp) {
  if (index < 0 || index >= (int)m_monsters.size())
    return;
  auto &mon = m_monsters[index];
  std::cout << "[Client] Mon " << index << " (" << mon.name << "): RESPAWN at ("
            << (int)gridX << "," << (int)gridY << ") hp=" << hp << std::endl;
  auto &mdl = m_models[mon.modelIdx];

  float worldX = (float)gridY * 100.0f;
  float worldZ = (float)gridX * 100.0f;
  float worldY = snapToTerrain(worldX, worldZ) + mdl.bodyOffset;
  mon.position = glm::vec3(worldX, worldY, worldZ);
  mon.spawnPosition = mon.position;
  mon.hp = hp;
  mon.maxHp = hp;
  mon.corpseAlpha = 1.0f;
  mon.corpseTimer = 0.0f;
  mon.state = MonsterState::IDLE;
  mon.serverChasing = false;
  mon.serverPosAge = 999.0f;
  setAction(mon, ACTION_STOP1);
}

void MonsterManager::SetMonsterServerPosition(int index, float worldX,
                                              float worldZ, bool chasing) {
  if (index < 0 || index >= (int)m_monsters.size())
    return;
  auto &mon = m_monsters[index];
  if (mon.state == MonsterState::DYING || mon.state == MonsterState::DEAD)
    return;
  std::cout << "[Client] Mon " << index << " (" << mon.name
            << "): 0x35 move to (" << worldX << "," << worldZ
            << ") chasing=" << chasing << " state=" << (int)mon.state
            << std::endl;

  auto &mdl = m_models[mon.modelIdx];
  float terrainY =
      snapToTerrain(worldX + 50.0f, worldZ + 50.0f) + mdl.bodyOffset;
  mon.serverTargetPos = glm::vec3(worldX + 50.0f, terrainY, worldZ + 50.0f);
  mon.serverChasing = chasing;
  mon.serverPosAge = 0.0f;

  // Transition to CHASING if server says monster is chasing
  if (chasing && mon.state != MonsterState::ATTACKING &&
      mon.state != MonsterState::HIT) {
    mon.state = MonsterState::CHASING;
  }
  // If server says no longer chasing, walk to the given position then idle
  // Also update wanderTarget when already WALKING (prevents idle gaps between
  // wander moves)
  if (!chasing &&
      (mon.state == MonsterState::CHASING || mon.state == MonsterState::IDLE ||
       mon.state == MonsterState::WALKING)) {
    mon.state = MonsterState::WALKING;
    mon.wanderTarget = mon.serverTargetPos;
    // Compute a reasonable timeout based on distance to target
    float dx = mon.serverTargetPos.x - mon.position.x;
    float dz = mon.serverTargetPos.z - mon.position.z;
    float dist = std::sqrt(dx * dx + dz * dz);
    mon.stateTimer = std::max(2.0f, dist / WANDER_SPEED + 1.0f);
  }
}

int MonsterManager::CalcXPReward(int monsterIndex, int playerLevel) const {
  // CharacterCalcExperienceAlone (ObjectManager.cpp:813)
  if (monsterIndex < 0 || monsterIndex >= (int)m_monsters.size())
    return 0;
  const auto &mon = m_monsters[monsterIndex];
  const auto &mdl = m_models[mon.modelIdx];
  int monLvl = mdl.level;
  int lvlFactor = ((monLvl + 25) * monLvl) / 3;
  // Level penalty: monster 10+ levels below player
  if ((monLvl + 10) < playerLevel)
    lvlFactor = (lvlFactor * (monLvl + 10)) / std::max(1, playerLevel);
  int xp = lvlFactor + lvlFactor / 4; // * 1.25
  return std::max(1, xp);
}

void MonsterManager::spawnDebris(int modelIdx, const glm::vec3 &pos,
                                 int count) {
  if (modelIdx < 0 || modelIdx >= (int)m_models.size())
    return;

  for (int i = 0; i < count; ++i) {
    DebrisInstance d;
    d.modelIdx = modelIdx;
    d.position = pos;
    float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
    float speed = 80.0f + (float)(rand() % 100);
    d.velocity =
        glm::vec3(std::cos(angle) * speed, 150.0f + (float)(rand() % 100),
                  std::sin(angle) * speed);
    d.rotation = glm::vec3((float)(rand() % 360), (float)(rand() % 360),
                           (float)(rand() % 360));
    d.rotVelocity =
        glm::vec3((float)(rand() % 200 - 100), (float)(rand() % 200 - 100),
                  (float)(rand() % 200 - 100));
    d.scale = m_models[modelIdx].scale * (0.8f + (float)(rand() % 40) / 100.0f);
    d.lifetime = 2.0f + (float)(rand() % 2000) / 1000.0f;
    m_debris.push_back(d);
  }
}

void MonsterManager::updateDebris(float dt) {
  for (int i = (int)m_debris.size() - 1; i >= 0; --i) {
    auto &d = m_debris[i];
    d.lifetime -= dt;
    if (d.lifetime <= 0.0f) {
      m_debris[i] = m_debris.back();
      m_debris.pop_back();
      continue;
    }

    d.position += d.velocity * dt;
    d.rotation += d.rotVelocity * dt;

    float floorY = snapToTerrain(d.position.x, d.position.z);
    if (d.position.y < floorY) {
      d.position.y = floorY;
      d.velocity.y = -d.velocity.y * 0.4f; // Bounce
      d.velocity.x *= 0.6f;
      d.velocity.z *= 0.6f;
      d.rotVelocity *= 0.5f;
    } else {
      d.velocity.y -= 500.0f * dt; // Gravity
    }
  }
}

void MonsterManager::renderDebris(const glm::mat4 &view,
                                  const glm::mat4 &projection,
                                  const glm::vec3 &camPos) {
  if (m_debris.empty() || !m_shader)
    return;

  m_shader->use();
  m_shader->setMat4("view", view);
  m_shader->setMat4("projection", projection);
  m_shader->setFloat("luminosity", m_luminosity);
  m_shader->setFloat("blendMeshLight", 1.0f);
  m_shader->setInt("numPointLights", 0);
  m_shader->setBool("useFog", true);
  m_shader->setVec3("viewPos", camPos);
  // We need camPos if we want fog to work correctly, passing it via Render
  // For now let's assume viewPos is already set or passed.
  // I'll update Render() to pass camPos to renderDebris.

  for (const auto &d : m_debris) {
    auto &mdl = m_models[d.modelIdx];
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, d.position);
    model = glm::rotate(model, glm::radians(d.rotation.z), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(d.rotation.y), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(d.rotation.x), glm::vec3(1, 0, 0));
    model = glm::scale(model, glm::vec3(d.scale));
    m_shader->setMat4("model", model);

    // Get terrain light
    glm::vec3 light = sampleTerrainLightAt(d.position);
    m_shader->setVec3("terrainLight", light);

    // Debris fade out
    float alpha = std::min(1.0f, d.lifetime * 2.0f);
    m_shader->setFloat("objectAlpha", alpha);

    // Draw pre-uploaded mesh buffers
    for (size_t i = 0; i < mdl.meshBuffers.size(); ++i) {
      auto &mb = mdl.meshBuffers[i];
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }
  glBindVertexArray(0);
}

void MonsterManager::Cleanup() {
  for (auto &mon : m_monsters) {
    CleanupMeshBuffers(mon.meshBuffers);
    for (auto &sm : mon.shadowMeshes) {
      if (sm.vao)
        glDeleteVertexArrays(1, &sm.vao);
      if (sm.vbo)
        glDeleteBuffers(1, &sm.vbo);
    }
  }
  m_monsters.clear();
  m_models.clear();
  m_ownedBmds.clear();
  m_shader.reset();
  m_shadowShader.reset();
}
