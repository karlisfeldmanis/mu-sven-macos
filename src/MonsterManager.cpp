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
    {3, "Spider"}};

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
  return h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
         h01 * (1 - xd) * zd + h11 * xd * zd;
}

int MonsterManager::loadMonsterModel(const std::string &bmdFile,
                                     const std::string &name, float scale,
                                     float radius, float height) {
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
  m_ownedBmds.push_back(std::move(bmd));

  int idx = (int)m_models.size();
  m_models.push_back(std::move(model));
  std::cout << "[Monster] Loaded model '" << name << "' ("
            << m_models[idx].bmd->Bones.size() << " bones, "
            << m_models[idx].bmd->Meshes.size() << " meshes, "
            << m_models[idx].bmd->Actions.size() << " actions)" << std::endl;
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

  // Spider: server type 3, model enum 9 → Monster10.bmd (Type+1 naming)
  int spiderIdx =
      loadMonsterModel("Monster10.bmd", "Spider", 0.4f, 60.0f, 80.0f);
  m_typeToModel[3] = spiderIdx;

  m_modelsLoaded = true;
  std::cout << "[Monster] Models loaded: " << m_models.size() << " types"
            << std::endl;
}

void MonsterManager::AddMonster(uint16_t monsterType, uint8_t gridX,
                                uint8_t gridY, uint8_t dir) {
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
  float worldY = snapToTerrain(worldX, worldZ);
  mon.position = glm::vec3(worldX, worldY, worldZ);
  mon.spawnPosition = mon.position;

  // Direction to facing angle (same as NPC: dir-1 * 45°)
  mon.facing = (float)(dir - 1) * (float)M_PI / 4.0f;

  // Random animation offset so monsters don't sync
  mon.animFrame = (float)(m_monsters.size() * 2.3f);

  // Random initial idle timer
  mon.idleWanderTimer = 2.0f + (float)(rand() % 3000) / 1000.0f;

  // Compute initial bone matrices
  auto bones = ComputeBoneMatrices(mdl.bmd);

  // Upload meshes (single BMD, not multi-part)
  AABB aabb{};
  for (auto &mesh : mdl.bmd->Meshes) {
    UploadMeshWithBones(mesh, m_monsterTexPath, bones, mon.meshBuffers, aabb,
                        true);
  }

  // Create shadow mesh buffers
  for (auto &mb : mon.meshBuffers) {
    MonsterInstance::ShadowMesh sm;
    sm.vertexCount = mb.vertexCount;
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
  mon.action = action;
  mon.animFrame = 0.0f;
}

void MonsterManager::updateStateMachine(MonsterInstance &mon, float dt) {
  auto &mdl = m_models[mon.modelIdx];

  switch (mon.state) {
  case MonsterState::IDLE: {
    setAction(mon, ACTION_STOP1);
    mon.idleWanderTimer -= dt;
    if (mon.idleWanderTimer <= 0.0f) {
      // Pick random wander target within WANDER_RADIUS of spawn
      float angle = (float)(rand() % 628) / 100.0f; // 0 to ~2*PI
      float dist = (float)(rand() % (int)WANDER_RADIUS);
      mon.wanderTarget = mon.spawnPosition;
      mon.wanderTarget.x += cosf(angle) * dist;
      mon.wanderTarget.z += sinf(angle) * dist;
      mon.wanderTarget.y = snapToTerrain(mon.wanderTarget.x, mon.wanderTarget.z);
      mon.state = MonsterState::WALKING;
      mon.stateTimer = 5.0f; // Max walk time
    }
    break;
  }

  case MonsterState::WALKING: {
    setAction(mon, ACTION_WALK);
    glm::vec3 dir = mon.wanderTarget - mon.position;
    dir.y = 0.0f;
    float dist = glm::length(dir);

    if (dist < 10.0f || mon.stateTimer <= 0.0f) {
      // Arrived or timed out
      mon.state = MonsterState::IDLE;
      mon.idleWanderTimer = 2.0f + (float)(rand() % 3000) / 1000.0f;
    } else {
      // Move toward target
      glm::vec3 moveDir = glm::normalize(dir);
      float step = WANDER_SPEED * dt;
      if (step > dist)
        step = dist;
      mon.position += moveDir * step;
      mon.position.y = snapToTerrain(mon.position.x, mon.position.z);

      // Face movement direction (atan2 in MU space)
      mon.facing = atan2f(moveDir.x, moveDir.z);
    }
    mon.stateTimer -= dt;
    break;
  }

  case MonsterState::HIT: {
    setAction(mon, ACTION_SHOCK);
    mon.stateTimer -= dt;
    if (mon.stateTimer <= 0.0f) {
      mon.state = MonsterState::IDLE;
      mon.idleWanderTimer = 0.5f; // Short pause after hit
    }
    break;
  }

  case MonsterState::DYING: {
    setAction(mon, ACTION_DIE);
    // Check if die animation finished — freeze on last frame
    int numKeys = 1;
    if (ACTION_DIE < (int)mdl.bmd->Actions.size())
      numKeys = mdl.bmd->Actions[ACTION_DIE].NumAnimationKeys;
    if (mon.animFrame >= (float)(numKeys - 1)) {
      mon.animFrame = (float)(numKeys - 1); // Freeze
      mon.state = MonsterState::DEAD;
      mon.stateTimer = 0.0f;
    }
    break;
  }

  case MonsterState::DEAD: {
    // Fade corpse
    mon.corpseTimer += dt;
    if (mon.corpseTimer < CORPSE_FADE_TIME) {
      mon.corpseAlpha = 1.0f - (mon.corpseTimer / CORPSE_FADE_TIME);
    } else {
      mon.corpseAlpha = 0.0f;
    }

    // Respawn after RESPAWN_TIME
    if (mon.corpseTimer >= RESPAWN_TIME) {
      mon.position = mon.spawnPosition;
      mon.position.y = snapToTerrain(mon.position.x, mon.position.z);
      mon.hp = mon.maxHp;
      mon.corpseAlpha = 1.0f;
      mon.corpseTimer = 0.0f;
      mon.state = MonsterState::IDLE;
      mon.idleWanderTimer = 1.0f + (float)(rand() % 2000) / 1000.0f;
      setAction(mon, ACTION_STOP1);
    }
    break;
  }
  }
}

void MonsterManager::Update(float deltaTime) {
  for (auto &mon : m_monsters) {
    updateStateMachine(mon, deltaTime);
  }
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

  for (auto &mon : m_monsters) {
    // Skip fully faded corpses
    if (mon.state == MonsterState::DEAD && mon.corpseAlpha <= 0.01f)
      continue;

    auto &mdl = m_models[mon.modelIdx];

    // Advance animation
    int numKeys = 1;
    if (mon.action >= 0 && mon.action < (int)mdl.bmd->Actions.size())
      numKeys = mdl.bmd->Actions[mon.action].NumAnimationKeys;
    if (numKeys > 1) {
      // Spider walk speed multiplier: 1.2x
      float speedMul = (mon.action == ACTION_WALK) ? 1.2f : 1.0f;
      mon.animFrame += ANIM_SPEED * speedMul * deltaTime;

      // Die animation doesn't loop
      if (mon.state == MonsterState::DYING ||
          mon.state == MonsterState::DEAD) {
        if (mon.animFrame >= (float)(numKeys - 1))
          mon.animFrame = (float)(numKeys - 1);
      } else {
        if (mon.animFrame >= (float)numKeys)
          mon.animFrame = std::fmod(mon.animFrame, (float)numKeys);
      }
    }

    // Compute bone matrices with slerp
    auto bones =
        ComputeBoneMatricesInterpolated(mdl.bmd, mon.action, mon.animFrame);
    mon.cachedBones = bones;

    // Re-skin meshes
    for (int mi = 0; mi < (int)mon.meshBuffers.size() &&
                      mi < (int)mdl.bmd->Meshes.size();
         ++mi) {
      RetransformMeshWithBones(mdl.bmd->Meshes[mi], bones,
                               mon.meshBuffers[mi]);
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

  // Reset objectAlpha for subsequent renders
  m_shader->setFloat("objectAlpha", 1.0f);
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

    for (int mi = 0; mi < (int)mdl.bmd->Meshes.size() &&
                      mi < (int)mon.shadowMeshes.size();
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

void MonsterManager::RenderOutline(int monsterIndex, const glm::mat4 &view,
                                   const glm::mat4 &proj) {
  if (!m_shader || monsterIndex < 0 ||
      monsterIndex >= (int)m_monsters.size())
    return;

  auto &mon = m_monsters[monsterIndex];
  // Don't outline dead/dying monsters
  if (mon.state == MonsterState::DEAD || mon.state == MonsterState::DYING)
    return;

  m_shader->use();
  m_shader->setMat4("projection", proj);
  m_shader->setMat4("view", view);
  m_shader->setVec3("lightPos", glm::vec3(0, 10000, 0));
  m_shader->setVec3("lightColor", 0.0f, 0.0f, 0.0f);
  m_shader->setVec3("viewPos", glm::vec3(0));
  m_shader->setBool("useFog", false);
  m_shader->setVec2("texCoordOffset", glm::vec2(0.0f));
  m_shader->setInt("numPointLights", 0);
  m_shader->setFloat("luminosity", 1.0f);
  m_shader->setFloat("objectAlpha", 1.0f);

  // Additive blend, no depth write/test
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  glDepthMask(GL_FALSE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  // Scaled model matrix (BoneScale = 1.2)
  float edgeScale = mon.scale * 1.2f;
  glm::mat4 model = glm::translate(glm::mat4(1.0f), mon.position);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, mon.facing, glm::vec3(0, 0, 1));
  model = glm::scale(model, glm::vec3(edgeScale));
  m_shader->setMat4("model", model);

  // Pass 1: Monster primary edge color — red tint
  m_shader->setFloat("blendMeshLight", 1.0f);
  m_shader->setVec3("terrainLight", 0.1f, 0.02f, 0.0f);
  for (auto &mb : mon.meshBuffers) {
    if (mb.indexCount == 0 || mb.hidden)
      continue;
    glBindTexture(GL_TEXTURE_2D, mb.texture);
    glBindVertexArray(mb.vao);
    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
  }

  // Pass 2: Brighter red
  m_shader->setVec3("terrainLight", 0.2f, 0.05f, 0.0f);
  for (auto &mb : mon.meshBuffers) {
    if (mb.indexCount == 0 || mb.hidden)
      continue;
    glBindTexture(GL_TEXTURE_2D, mb.texture);
    glBindVertexArray(mb.vao);
    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
  }

  // Restore state
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindVertexArray(0);
}

bool MonsterManager::DealDamage(int monsterIndex, int damage) {
  if (monsterIndex < 0 || monsterIndex >= (int)m_monsters.size())
    return false;

  auto &mon = m_monsters[monsterIndex];
  if (mon.state == MonsterState::DYING || mon.state == MonsterState::DEAD)
    return false;

  mon.hp -= damage;
  if (mon.hp <= 0) {
    mon.hp = 0;
    mon.state = MonsterState::DYING;
    mon.stateTimer = 0.0f;
    setAction(mon, ACTION_DIE);
    return true; // Killed
  } else {
    mon.state = MonsterState::HIT;
    mon.stateTimer = 0.5f;
    return false;
  }
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
  info.name = mon.name;
  info.type = mon.monsterType;
  info.hp = mon.hp;
  info.maxHp = mon.maxHp;
  info.state = mon.state;
  return info;
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
