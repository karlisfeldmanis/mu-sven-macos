#include "HeroCharacter.hpp"
#include "TextureLoader.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>

// ─── DK Stat Formulas (MuEmu-0.97k ObjectManager.cpp) ──────────────────

uint64_t HeroCharacter::CalcXPForLevel(int level) {
  // gObjSetExperienceTable: cubic curve, MaxLevel=400
  // scaleFactor = (UINT32_MAX * 0.95) / 400^3 ≈ 63.7
  static constexpr double kScale =
      ((double)0xFFFFFFFF * 0.95) / (400.0 * 400.0 * 400.0);
  return (uint64_t)(kScale * (double)level * (double)level * (double)level);
}

void HeroCharacter::RecalcStats() {
  // MaxHP = 110 + 2.0*(Level-1) + (VIT-25)*3.0
  m_maxHp = (int)(DK_BASE_HP + DK_LEVEL_LIFE * (m_level - 1) +
                  (m_vitality - DK_BASE_VIT) * DK_VIT_TO_LIFE);
  if (m_maxHp < 1)
    m_maxHp = 1;

  // MaxMana = 25 + (Level-1)*1.0 + (Energy-10)*1.5
  m_maxMana = (int)(25 + (m_level - 1) * 1.0f + (m_energy - 10) * 1.5f);
  if (m_maxMana < 1)
    m_maxMana = 1;

  // Damage = STR / 8 + weapon .. STR / 4 + weapon (original MU formula)
  m_damageMin = std::max(1, (int)m_strength / 8 + m_weaponDamageMin);
  m_damageMax = std::max(m_damageMin, (int)m_strength / 4 + m_weaponDamageMax);

  // Defense = DEX / 3 + equipped armor/shield defense
  m_defense = (int)m_dexterity / 3 + m_equipDefenseBonus;

  // AttackSuccessRate = Level*5 + (DEX*3)/2 + STR/4
  m_attackSuccessRate =
      m_level * 5 + ((int)m_dexterity * 3) / 2 + (int)m_strength / 4;

  // DefenseSuccessRate = DEX / 3
  m_defenseSuccessRate = (int)m_dexterity / 3;

  // XP threshold for next level
  m_nextExperience = CalcXPForLevel(m_level);
}

void HeroCharacter::GainExperience(uint64_t xp) {
  m_experience += xp;
  m_leveledUpThisFrame = false;

  while (m_experience >= m_nextExperience && m_level < 400) {
    m_level++;
    m_levelUpPoints += DK_POINTS_PER_LEVEL;
    m_leveledUpThisFrame = true;
    RecalcStats();
    m_hp = m_maxHp; // Full heal on level-up
    std::cout << "[Hero] Level up! Now level " << m_level << " (HP=" << m_maxHp
              << ", points=" << m_levelUpPoints
              << ", nextXP=" << m_nextExperience << ")" << std::endl;
  }
}

bool HeroCharacter::AddStatPoint(int stat) {
  if (m_levelUpPoints <= 0)
    return false;
  switch (stat) {
  case 0:
    m_strength++;
    break;
  case 1:
    m_dexterity++;
    break;
  case 2:
    m_vitality++;
    break;
  case 3:
    m_energy++;
    break;
  default:
    return false;
  }
  m_levelUpPoints--;
  int oldMaxHp = m_maxHp;
  RecalcStats();
  // If max HP increased, add the difference to current HP
  if (m_maxHp > oldMaxHp)
    m_hp += (m_maxHp - oldMaxHp);
  return true;
}

void HeroCharacter::LoadStats(int level, uint16_t str, uint16_t dex,
                              uint16_t vit, uint16_t ene, uint64_t experience,
                              int levelUpPoints, int currentHp,
                              int currentMana) {
  m_level = level;
  m_strength = str;
  m_dexterity = dex;
  m_vitality = vit;
  m_energy = ene;
  m_experience = experience;
  m_levelUpPoints = levelUpPoints;
  RecalcStats();
  // Restore current HP/Mana from server (clamped to new max values)
  m_hp = std::min(currentHp, m_maxHp);
  if (m_hp <= 0 && currentHp > 0)
    m_hp = m_maxHp; // Don't load as dead if server says alive
  m_mana = std::min(currentMana, m_maxMana);

  std::cout << "[Hero] Loaded stats from server: Lv" << m_level
            << " STR=" << m_strength << " DEX=" << m_dexterity
            << " VIT=" << m_vitality << " ENE=" << m_energy << " HP=" << m_hp
            << "/" << m_maxHp << " MP=" << m_mana << "/" << m_maxMana
            << " XP=" << m_experience << " pts=" << m_levelUpPoints
            << std::endl;
}

void HeroCharacter::Heal(int amount) {
  if (m_heroState != HeroState::ALIVE)
    return;
  m_hp = std::min(m_hp + amount, m_maxHp);
}

void HeroCharacter::SetWeaponBonus(int dmin, int dmax) {
  m_weaponDamageMin = dmin;
  m_weaponDamageMax = dmax;
  RecalcStats();
}

void HeroCharacter::SetDefenseBonus(int def) {
  m_equipDefenseBonus = def;
  RecalcStats();
}

DamageResult HeroCharacter::RollAttack(int targetDefense,
                                       int targetDefSuccessRate) const {
  // 1. Miss check (reference Attack.cpp)
  int atkRate = m_attackSuccessRate;
  int defRate = targetDefSuccessRate;
  if (atkRate < defRate) {
    // Attacker weaker: 95% miss
    if (rand() % 100 >= 5)
      return {0, DamageType::MISS};
  } else if (defRate > 0 && atkRate > 0) {
    // Normal: miss chance = defRate / atkRate
    if (rand() % atkRate < defRate)
      return {0, DamageType::MISS};
  }

  // 2. Critical check: rate = DEX / 5, cap 20%
  int critRate = std::min((int)m_dexterity / 5, 20);
  if (rand() % 100 < critRate) {
    int dmg = m_damageMax + m_level / 2; // Max + level bonus
    return {std::max(1, dmg - targetDefense), DamageType::CRITICAL};
  }

  // 3. Excellent check: rate = DEX / 10, cap 10%
  int excRate = std::min((int)m_dexterity / 10, 10);
  if (rand() % 100 < excRate) {
    int dmg = (m_damageMax * 120) / 100; // 120% of max
    return {std::max(1, dmg - targetDefense), DamageType::EXCELLENT};
  }

  // 4. Normal hit: random in [min, max]
  int dmg = m_damageMin;
  if (m_damageMax > m_damageMin)
    dmg += rand() % (m_damageMax - m_damageMin + 1);
  dmg -= targetDefense;
  return {std::max(1, dmg), DamageType::NORMAL};
}

glm::vec3 HeroCharacter::sampleTerrainLightAt(const glm::vec3 &worldPos) const {
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

void HeroCharacter::Init(const std::string &dataPath) {
  m_dataPath = dataPath;
  std::string playerPath = dataPath + "/Player/";

  // Load skeleton (Player.bmd — bones + actions, zero meshes)
  m_skeleton = BMDParser::Parse(playerPath + "player.bmd");
  if (!m_skeleton) {
    std::cerr << "[Hero] Failed to load Player.bmd skeleton" << std::endl;
    return;
  }
  std::cout << "[Hero] Player.bmd: " << m_skeleton->Bones.size() << " bones, "
            << m_skeleton->Actions.size() << " actions" << std::endl;

  // Load DK Naked body parts (Class02)
  const char *partFiles[] = {"HelmClass02.bmd", "ArmorClass02.bmd",
                             "PantClass02.bmd", "GloveClass02.bmd",
                             "BootClass02.bmd"};

  auto bones = ComputeBoneMatrices(m_skeleton.get());
  AABB totalAABB{};

  for (int p = 0; p < PART_COUNT; ++p) {
    std::string fullPath = playerPath + partFiles[p];
    auto bmd = BMDParser::Parse(fullPath);
    if (!bmd) {
      std::cerr << "[Hero] Failed to load: " << partFiles[p] << std::endl;
      continue;
    }

    for (auto &mesh : bmd->Meshes) {
      UploadMeshWithBones(mesh, playerPath, bones, m_parts[p].meshBuffers,
                          totalAABB, true);
    }
    m_parts[p].bmd = std::move(bmd);
    std::cout << "[Hero] Loaded " << partFiles[p] << std::endl;
  }

  // Create shader (same model.vert/frag as ObjectRenderer)
  std::ifstream shaderTest("shaders/model.vert");
  m_shader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/model.vert" : "../shaders/model.vert",
      shaderTest.good() ? "shaders/model.frag" : "../shaders/model.frag");

  // Cache root bone index and log walk animation info
  if (m_skeleton) {
    for (int i = 0; i < (int)m_skeleton->Bones.size(); ++i) {
      if (m_skeleton->Bones[i].Parent == -1 && !m_skeleton->Bones[i].Dummy) {
        m_rootBone = i;
        break;
      }
    }
    const int WALK_ACTION = 15;
    if (m_rootBone >= 0 && WALK_ACTION < (int)m_skeleton->Actions.size()) {
      int numKeys = m_skeleton->Actions[WALK_ACTION].NumAnimationKeys;
      auto &bm = m_skeleton->Bones[m_rootBone].BoneMatrixes[WALK_ACTION];
      if ((int)bm.Position.size() >= numKeys && numKeys > 1) {
        glm::vec3 p0 = bm.Position[0];
        glm::vec3 pN = bm.Position[numKeys - 1];
        float strideY = pN.y - p0.y;
        std::cout << "[Hero] Root bone " << m_rootBone
                  << ": walk stride=" << strideY << " MU-Y over " << numKeys
                  << " keys, LockPositions="
                  << m_skeleton->Actions[WALK_ACTION].LockPositions
                  << std::endl;
      }
    }
  }
  // Create shadow shader
  m_shadowShader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/shadow.vert" : "../shaders/shadow.vert",
      shaderTest.good() ? "shaders/shadow.frag" : "../shaders/shadow.frag");

  // Create shadow mesh buffers (position-only VBO for each body part mesh)
  for (int p = 0; p < PART_COUNT; ++p) {
    if (!m_parts[p].bmd)
      continue;
    for (auto &mb : m_parts[p].meshBuffers) {
      ShadowMesh sm;
      sm.vertexCount = mb.vertexCount;
      sm.indexCount = mb.vertexCount; // sequential indices = vertex count
      if (sm.vertexCount == 0) {
        m_shadowMeshes.push_back(sm);
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
      m_shadowMeshes.push_back(sm);
    }
  }

  // Compute initial stats from DK formulas
  RecalcStats();
  m_hp = m_maxHp;
  std::cout << "[Hero] DK Level " << m_level << " — HP=" << m_maxHp
            << " Dmg=" << m_damageMin << "-" << m_damageMax
            << " Def=" << m_defense << " AtkRate=" << m_attackSuccessRate
            << " NextXP=" << m_nextExperience << std::endl;
  std::cout << "[Hero] Character initialized (DK Naked)" << std::endl;
}

void HeroCharacter::Render(const glm::mat4 &view, const glm::mat4 &proj,
                           const glm::vec3 &camPos, float deltaTime) {
  if (!m_skeleton || !m_shader)
    return;

  // Advance animation
  int numKeys = 1;
  bool lockPos = false;
  if (m_action >= 0 && m_action < (int)m_skeleton->Actions.size()) {
    numKeys = m_skeleton->Actions[m_action].NumAnimationKeys;
    lockPos = m_skeleton->Actions[m_action].LockPositions;
  }
  if (numKeys > 1) {
    // Don't loop die animation — clamp to last frame when dying/dead
    bool clampAnim =
        (m_heroState == HeroState::DYING || m_heroState == HeroState::DEAD);
    m_animFrame += ANIM_SPEED * deltaTime;
    if (clampAnim) {
      if (m_animFrame >= (float)(numKeys - 1))
        m_animFrame = (float)(numKeys - 1);
    } else {
      int wrapKeys = lockPos ? (numKeys - 1) : numKeys;
      if (m_animFrame >= (float)wrapKeys)
        m_animFrame = std::fmod(m_animFrame, (float)wrapKeys);
    }
  }

  // Compute bones for current animation frame
  auto bones =
      ComputeBoneMatricesInterpolated(m_skeleton.get(), m_action, m_animFrame);

  // LockPositions: root bone X/Y locked to frame 0
  if (lockPos && m_rootBone >= 0) {
    int i = m_rootBone;
    auto &bm = m_skeleton->Bones[i].BoneMatrixes[m_action];
    if (!bm.Position.empty()) {
      float dx = bones[i][0][3] - bm.Position[0].x;
      float dy = bones[i][1][3] - bm.Position[0].y;
      for (int b = 0; b < (int)bones.size(); ++b) {
        bones[b][0][3] -= dx;
        bones[b][1][3] -= dy;
      }
    }
  }

  // Cache bones for shadow rendering
  m_cachedBones = bones;

  // Re-skin all body part meshes
  for (int p = 0; p < PART_COUNT; ++p) {
    if (!m_parts[p].bmd)
      continue;
    for (int mi = 0; mi < (int)m_parts[p].meshBuffers.size() &&
                     mi < (int)m_parts[p].bmd->Meshes.size();
         ++mi) {
      RetransformMeshWithBones(m_parts[p].bmd->Meshes[mi], bones,
                               m_parts[p].meshBuffers[mi]);
    }
  }

  // Build model matrix: translate -> MU->GL coord conversion -> facing rotation
  glm::mat4 model = glm::translate(glm::mat4(1.0f), m_pos);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, m_facing, glm::vec3(0, 0, 1));

  m_shader->use();
  m_shader->setMat4("projection", proj);
  m_shader->setMat4("view", view);
  m_shader->setMat4("model", model);

  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
  m_shader->setVec3("lightPos", eye + glm::vec3(0, 500, 0));
  m_shader->setVec3("lightColor", 1.0f, 1.0f, 1.0f);
  m_shader->setVec3("viewPos", eye);
  m_shader->setBool("useFog", true);
  m_shader->setFloat("blendMeshLight", 1.0f);
  m_shader->setFloat("objectAlpha", 1.0f);
  m_shader->setVec2("texCoordOffset", glm::vec2(0.0f));
  m_shader->setFloat("luminosity", m_luminosity);

  // Terrain lightmap at hero position
  glm::vec3 tLight = sampleTerrainLightAt(m_pos);
  m_shader->setVec3("terrainLight", tLight);

  // Point lights
  int plCount = std::min((int)m_pointLights.size(), MAX_POINT_LIGHTS);
  m_shader->setInt("numPointLights", plCount);
  for (int i = 0; i < plCount; ++i) {
    std::string idx = std::to_string(i);
    m_shader->setVec3("pointLightPos[" + idx + "]", m_pointLights[i].position);
    m_shader->setVec3("pointLightColor[" + idx + "]", m_pointLights[i].color);
    m_shader->setFloat("pointLightRange[" + idx + "]", m_pointLights[i].range);
  }

  // Draw all body part meshes
  for (int p = 0; p < PART_COUNT; ++p) {
    for (auto &mb : m_parts[p].meshBuffers) {
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

  // Draw weapon attached to hand bone (if equipped)
  // Original engine chain: CharBone[LinkBone] * WeaponBone[node] * vertex
  // Rotation/offset are identity — weapon BMD's own bone handles orientation
  auto &wCat = GetWeaponCategoryRender(m_weaponInfo.category);
  int attachBone = wCat.attachBone;
  if (m_weaponBmd && !m_weaponMeshBuffers.empty() &&
      attachBone < (int)bones.size()) {

    // Identity offset (no rotation, no translation)
    BoneWorldMatrix weaponOffsetMat =
        MuMath::BuildWeaponOffsetMatrix(glm::vec3(0, 0, 0), glm::vec3(0, 0, 0));

    // parentMat = CharBone[attachBone] * OffsetMatrix
    BoneWorldMatrix parentMat;
    MuMath::ConcatTransforms((const float(*)[4])bones[attachBone].data(),
                             (const float(*)[4])weaponOffsetMat.data(),
                             (float(*)[4])parentMat.data());

    // Compute weapon bone matrices with parentMat as root parent
    // Mirrors original Animation(Parent=true)
    auto wLocalBones = ComputeBoneMatrices(m_weaponBmd.get());
    std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
    for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
      MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                               (const float(*)[4])wLocalBones[bi].data(),
                               (float(*)[4])wFinalBones[bi].data());
    }

    // Re-skin weapon vertices using final bone matrices
    for (int mi = 0; mi < (int)m_weaponMeshBuffers.size() &&
                     mi < (int)m_weaponBmd->Meshes.size();
         ++mi) {
      auto &mesh = m_weaponBmd->Meshes[mi];
      auto &mb = m_weaponMeshBuffers[mi];
      if (mb.indexCount == 0)
        continue;

      std::vector<ViewerVertex> verts;
      verts.reserve(mesh.NumTriangles * 3);
      for (int ti = 0; ti < mesh.NumTriangles; ++ti) {
        auto &tri = mesh.Triangles[ti];
        for (int v = 0; v < 3; ++v) {
          ViewerVertex vv;
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 srcPos = srcVert.Position;
          glm::vec3 srcNorm = (tri.NormalIndex[v] < mesh.NumNormals)
                                  ? mesh.Normals[tri.NormalIndex[v]].Normal
                                  : glm::vec3(0, 0, 1);

          int boneIdx = srcVert.Node;
          if (boneIdx >= 0 && boneIdx < (int)wFinalBones.size()) {
            vv.pos = MuMath::TransformPoint(
                (const float(*)[4])wFinalBones[boneIdx].data(), srcPos);
            vv.normal = MuMath::RotateVector(
                (const float(*)[4])wFinalBones[boneIdx].data(), srcNorm);
          } else {
            vv.pos = MuMath::TransformPoint((const float(*)[4])parentMat.data(),
                                            srcPos);
            vv.normal = MuMath::RotateVector(
                (const float(*)[4])parentMat.data(), srcNorm);
          }
          vv.tex =
              (tri.TexCoordIndex[v] < mesh.NumTexCoords)
                  ? glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                              mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV)
                  : glm::vec2(0);
          verts.push_back(vv);
        }
      }

      // Upload to GPU via glBufferSubData
      glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
      glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(ViewerVertex),
                      verts.data());

      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }

  // --- Render shield on left hand (bone 42) ---
  auto &sCat = GetWeaponCategoryRender(6); // category 6 = shield
  int shieldBone = sCat.attachBone;        // bone 42 = left hand
  if (m_shieldBmd && !m_shieldMeshBuffers.empty() &&
      shieldBone < (int)bones.size()) {

    BoneWorldMatrix shieldOffsetMat =
        MuMath::BuildWeaponOffsetMatrix(glm::vec3(0, 0, 0), glm::vec3(0, 0, 0));

    BoneWorldMatrix shieldParentMat;
    MuMath::ConcatTransforms((const float(*)[4])bones[shieldBone].data(),
                             (const float(*)[4])shieldOffsetMat.data(),
                             (float(*)[4])shieldParentMat.data());

    auto sLocalBones = ComputeBoneMatrices(m_shieldBmd.get());
    std::vector<BoneWorldMatrix> sFinalBones(sLocalBones.size());
    for (int bi = 0; bi < (int)sLocalBones.size(); ++bi) {
      MuMath::ConcatTransforms((const float(*)[4])shieldParentMat.data(),
                               (const float(*)[4])sLocalBones[bi].data(),
                               (float(*)[4])sFinalBones[bi].data());
    }

    for (int mi = 0; mi < (int)m_shieldMeshBuffers.size() &&
                     mi < (int)m_shieldBmd->Meshes.size();
         ++mi) {
      auto &mesh = m_shieldBmd->Meshes[mi];
      auto &mb = m_shieldMeshBuffers[mi];
      if (mb.indexCount == 0)
        continue;

      std::vector<ViewerVertex> verts;
      verts.reserve(mesh.NumTriangles * 3);
      for (int ti = 0; ti < mesh.NumTriangles; ++ti) {
        auto &tri = mesh.Triangles[ti];
        for (int v = 0; v < 3; ++v) {
          ViewerVertex vv;
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 srcPos = srcVert.Position;
          glm::vec3 srcNorm = (tri.NormalIndex[v] < mesh.NumNormals)
                                  ? mesh.Normals[tri.NormalIndex[v]].Normal
                                  : glm::vec3(0, 0, 1);

          int boneIdx = srcVert.Node;
          if (boneIdx >= 0 && boneIdx < (int)sFinalBones.size()) {
            vv.pos = MuMath::TransformPoint(
                (const float(*)[4])sFinalBones[boneIdx].data(), srcPos);
            vv.normal = MuMath::RotateVector(
                (const float(*)[4])sFinalBones[boneIdx].data(), srcNorm);
          } else {
            vv.pos = MuMath::TransformPoint(
                (const float(*)[4])shieldParentMat.data(), srcPos);
            vv.normal = MuMath::RotateVector(
                (const float(*)[4])shieldParentMat.data(), srcNorm);
          }
          vv.tex =
              (tri.TexCoordIndex[v] < mesh.NumTexCoords)
                  ? glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                              mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV)
                  : glm::vec2(0);
          verts.push_back(vv);
        }
      }

      glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
      glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(ViewerVertex),
                      verts.data());

      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }
}

void HeroCharacter::RenderShadow(const glm::mat4 &view, const glm::mat4 &proj) {
  if (!m_skeleton || !m_shadowShader || m_cachedBones.empty() ||
      m_shadowMeshes.empty())
    return;

  // Shadow model matrix: NO facing rotation (facing is baked into vertices
  // before shadow projection so the shadow direction stays fixed in world
  // space)
  glm::mat4 model = glm::translate(glm::mat4(1.0f), m_pos);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));

  m_shadowShader->use();
  m_shadowShader->setMat4("projection", proj);
  m_shadowShader->setMat4("view", view);
  m_shadowShader->setMat4("model", model);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.0f, -1.0f);
  glDisable(GL_CULL_FACE);

  // Stencil: only draw each shadow pixel once (prevents overlap darkening)
  glEnable(GL_STENCIL_TEST);
  glClear(GL_STENCIL_BUFFER_BIT);
  glStencilFunc(GL_EQUAL, 0, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

  // Shadow projection constants (from ZzzBMD.cpp RenderBodyShadow)
  const float sx = 2000.0f;
  const float sy = 4000.0f;

  // Pre-compute facing rotation in MU-local space (around MU Z = height axis)
  float cosF = cosf(m_facing);
  float sinF = sinf(m_facing);

  int smIdx = 0;
  for (int p = 0; p < PART_COUNT; ++p) {
    if (!m_parts[p].bmd)
      continue;
    for (int mi = 0; mi < (int)m_parts[p].bmd->Meshes.size() &&
                     smIdx < (int)m_shadowMeshes.size();
         ++mi, ++smIdx) {
      auto &sm = m_shadowMeshes[smIdx];
      if (sm.vertexCount == 0 || sm.vao == 0)
        continue;

      auto &mesh = m_parts[p].bmd->Meshes[mi];
      std::vector<glm::vec3> shadowVerts;
      shadowVerts.reserve(sm.vertexCount);

      for (int i = 0; i < mesh.NumTriangles; ++i) {
        auto &tri = mesh.Triangles[i];
        int steps = (tri.Polygon == 3) ? 3 : 4;
        for (int v = 0; v < 3; ++v) {
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 pos = srcVert.Position;
          int boneIdx = srcVert.Node;
          if (boneIdx >= 0 && boneIdx < (int)m_cachedBones.size()) {
            pos = MuMath::TransformPoint(
                (const float(*)[4])m_cachedBones[boneIdx].data(), pos);
          }
          // Apply facing rotation in MU space (around Z/height axis)
          float rx = pos.x * cosF - pos.y * sinF;
          float ry = pos.x * sinF + pos.y * cosF;
          pos.x = rx;
          pos.y = ry;
          // Shadow projection in MU-local space
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
            if (boneIdx >= 0 && boneIdx < (int)m_cachedBones.size()) {
              pos = MuMath::TransformPoint(
                  (const float(*)[4])m_cachedBones[boneIdx].data(), pos);
            }
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

void HeroCharacter::ProcessMovement(float deltaTime) {
  if (!m_terrainData || !m_moving || IsDead())
    return;

  glm::vec3 dir = m_target - m_pos;
  dir.y = 0;
  float dist = glm::length(dir);

  if (dist < 10.0f) {
    StopMoving();
  } else {
    dir = glm::normalize(dir);
    glm::vec3 step = dir * m_speed * deltaTime;
    glm::vec3 newPos = m_pos + step;

    const int S = TerrainParser::TERRAIN_SIZE;
    int gz = (int)(newPos.x / 100.0f);
    int gx = (int)(newPos.z / 100.0f);
    bool walkable =
        (gx >= 0 && gz >= 0 && gx < S && gz < S) &&
        (m_terrainData->mapping.attributes[gz * S + gx] & 0x04) == 0;

    if (walkable) {
      m_pos.x = newPos.x;
      m_pos.z = newPos.z;
    } else {
      StopMoving();
    }
  }

  SnapToTerrain();
}

void HeroCharacter::MoveTo(const glm::vec3 &target) {
  if (IsDead())
    return;
  m_target = target;
  // Only reset walk animation if not already walking
  int walkAction =
      (!m_inSafeZone && m_weaponBmd)
          ? GetWeaponCategoryRender(m_weaponInfo.category).actionWalk
          : ACTION_WALK_MALE;
  if (!m_moving || m_action != walkAction) {
    m_action = walkAction;
    m_animFrame = 0.0f;
  }
  m_moving = true;
  // Compute facing angle
  float dx = target.x - m_pos.x;
  float dz = target.z - m_pos.z;
  m_facing = atan2f(dz, -dx);
}

void HeroCharacter::StopMoving() {
  m_moving = false;
  // Use weapon-specific idle action when outside SafeZone with weapon
  if (!m_inSafeZone && m_weaponBmd) {
    m_action = GetWeaponCategoryRender(m_weaponInfo.category).actionIdle;
  } else {
    m_action = ACTION_STOP_MALE;
  }
  m_animFrame = 0.0f;
}

void HeroCharacter::SetInSafeZone(bool safe) {
  if (m_inSafeZone == safe)
    return;
  m_inSafeZone = safe;
  // Original MU: weapon model is ALWAYS rendered when equipped.
  // SafeZone only changes animation stance (unarmed vs combat).

  // Switch animation to match new state
  if (m_moving) {
    m_action = (!safe && m_weaponBmd)
                   ? GetWeaponCategoryRender(m_weaponInfo.category).actionWalk
                   : ACTION_WALK_MALE;
  } else {
    m_action = (!safe && m_weaponBmd)
                   ? GetWeaponCategoryRender(m_weaponInfo.category).actionIdle
                   : ACTION_STOP_MALE;
  }
  m_animFrame = 0.0f;

  std::cout << "[Hero] " << (safe ? "Entered SafeZone" : "Left SafeZone")
            << ", action=" << m_action << std::endl;
}

void HeroCharacter::EquipWeapon(const WeaponEquipInfo &weapon) {
  if (weapon.category == 0xFF || weapon.modelFile.empty()) {
    std::cout << "[Hero] Unequipping weapon" << std::endl;
    // Clear existing weapon
    CleanupMeshBuffers(m_weaponMeshBuffers);
    m_weaponBmd.reset();
    m_weaponInfo = weapon;
    // Reset to unarmed animation if needed
    if (!m_inSafeZone) {
      m_action = m_moving ? ACTION_WALK_MALE : ACTION_STOP_MALE;
      m_animFrame = 0.0f;
    }
    return;
  }

  m_weaponInfo = weapon;

  // Load weapon BMD from Data/Item/ directory
  std::string weaponPath = m_dataPath + "/Item/" + weapon.modelFile;
  m_weaponBmd = BMDParser::Parse(weaponPath);
  if (!m_weaponBmd) {
    std::cerr << "[Hero] Failed to load weapon: " << weaponPath << std::endl;
    return;
  }

  auto &catRender = GetWeaponCategoryRender(weapon.category);
  std::cout << "[Hero] Loaded weapon " << weapon.modelFile << ": "
            << m_weaponBmd->Meshes.size() << " meshes, "
            << m_weaponBmd->Bones.size() << " bones"
            << " (bone=" << (int)catRender.attachBone
            << " idle=" << (int)catRender.actionIdle
            << " walk=" << (int)catRender.actionWalk << ")" << std::endl;

  // Upload weapon meshes with its own bone matrices (static reference pose)
  std::string texPath = m_dataPath + "/Item/";
  AABB weaponAABB{};

  std::vector<BoneWorldMatrix> weaponBones;
  if (!m_weaponBmd->Bones.empty()) {
    weaponBones = ComputeBoneMatrices(m_weaponBmd.get());
  } else {
    BoneWorldMatrix identity{};
    identity[0] = {1, 0, 0, 0};
    identity[1] = {0, 1, 0, 0};
    identity[2] = {0, 0, 1, 0};
    weaponBones.push_back(identity);
  }

  CleanupMeshBuffers(m_weaponMeshBuffers);
  for (auto &mesh : m_weaponBmd->Meshes) {
    UploadMeshWithBones(mesh, texPath, weaponBones, m_weaponMeshBuffers,
                        weaponAABB, true);
  }

  // Update animation to combat stance if outside SafeZone
  if (!m_inSafeZone) {
    m_action = m_moving ? catRender.actionWalk : catRender.actionIdle;
    m_animFrame = 0.0f;
  }

  std::cout << "[Hero] Weapon equipped: " << weapon.modelFile << " ("
            << m_weaponMeshBuffers.size() << " GPU meshes)" << std::endl;
}

void HeroCharacter::EquipShield(const WeaponEquipInfo &shield) {
  if (shield.category == 0xFF || shield.modelFile.empty()) {
    std::cout << "[Hero] Unequipping shield" << std::endl;
    CleanupMeshBuffers(m_shieldMeshBuffers);
    m_shieldBmd.reset();
    m_shieldInfo = shield;
    return;
  }

  m_shieldInfo = shield;

  std::string shieldPath = m_dataPath + "/Item/" + shield.modelFile;
  m_shieldBmd = BMDParser::Parse(shieldPath);
  if (!m_shieldBmd) {
    std::cerr << "[Hero] Failed to load shield: " << shieldPath << std::endl;
    return;
  }

  std::cout << "[Hero] Loaded shield " << shield.modelFile << ": "
            << m_shieldBmd->Meshes.size() << " meshes, "
            << m_shieldBmd->Bones.size() << " bones" << std::endl;

  std::string texPath = m_dataPath + "/Item/";
  AABB shieldAABB{};

  std::vector<BoneWorldMatrix> shieldBones;
  if (!m_shieldBmd->Bones.empty()) {
    shieldBones = ComputeBoneMatrices(m_shieldBmd.get());
  } else {
    BoneWorldMatrix identity{};
    identity[0] = {1, 0, 0, 0};
    identity[1] = {0, 1, 0, 0};
    identity[2] = {0, 0, 1, 0};
    shieldBones.push_back(identity);
  }

  CleanupMeshBuffers(m_shieldMeshBuffers);
  for (auto &mesh : m_shieldBmd->Meshes) {
    UploadMeshWithBones(mesh, texPath, shieldBones, m_shieldMeshBuffers,
                        shieldAABB, true);
  }

  std::cout << "[Hero] Shield equipped: " << shield.modelFile << " ("
            << m_shieldMeshBuffers.size() << " GPU meshes)" << std::endl;
}

void HeroCharacter::EquipBodyPart(int partIndex, const std::string &modelFile) {
  if (partIndex < 0 || partIndex >= PART_COUNT)
    return;

  // Default naked DK body parts (Class02 = DK starting gear)
  static const char *defaultParts[] = {"HelmClass02.bmd", "ArmorClass02.bmd",
                                       "PantClass02.bmd", "GloveClass02.bmd",
                                       "BootClass02.bmd"};

  std::string fileToLoad =
      modelFile.empty() ? defaultParts[partIndex] : modelFile;
  std::string fullPath = m_dataPath + "/Player/" + fileToLoad;

  auto bmd = BMDParser::Parse(fullPath);
  if (!bmd) {
    std::cerr << "[Hero] Failed to load body part: " << fullPath << std::endl;
    return;
  }

  // Cleanup old meshes
  CleanupMeshBuffers(m_parts[partIndex].meshBuffers);

  // Recompute bones from skeleton bind pose
  auto bones = ComputeBoneMatrices(m_skeleton.get());
  AABB partAABB{};

  for (auto &mesh : bmd->Meshes) {
    UploadMeshWithBones(mesh, m_dataPath + "/Player/", bones,
                        m_parts[partIndex].meshBuffers, partAABB, true);
  }
  m_parts[partIndex].bmd = std::move(bmd);

  std::cout << "[Hero] Equipped body part[" << partIndex << "]: " << fileToLoad
            << " (" << m_parts[partIndex].meshBuffers.size() << " GPU meshes)"
            << std::endl;
}

void HeroCharacter::AttackMonster(int monsterIndex,
                                  const glm::vec3 &monsterPos) {
  if (IsDead())
    return;

  // Already attacking same target — just update position, don't reset cycle
  if (monsterIndex == m_attackTargetMonster &&
      (m_attackState == AttackState::SWINGING ||
       m_attackState == AttackState::COOLDOWN)) {
    m_attackTargetPos = monsterPos;
    return;
  }

  m_attackTargetMonster = monsterIndex;
  m_attackTargetPos = monsterPos;

  // Check distance
  glm::vec3 dir = monsterPos - m_pos;
  dir.y = 0.0f;
  float dist = glm::length(dir);

  if (dist <= ATTACK_RANGE) {
    // In range — start swinging
    m_attackState = AttackState::SWINGING;
    m_attackAnimTimer = 0.0f;
    m_attackHitRegistered = false;
    m_moving = false;

    // Face the target
    m_facing = atan2f(dir.z, -dir.x);

    // Use fist attack when no weapon, sword swings when armed
    if (m_weaponBmd) {
      m_action = (m_swordSwingCount % 2 == 0) ? ACTION_ATTACK_SWORD_R1
                                              : ACTION_ATTACK_SWORD_R2;
    } else {
      m_action = ACTION_ATTACK_FIST;
    }
    m_animFrame = 0.0f;
    m_swordSwingCount++;
  } else {
    // Out of range — walk toward target
    m_attackState = AttackState::APPROACHING;
    MoveTo(monsterPos);
  }
}

void HeroCharacter::UpdateAttack(float deltaTime) {
  if (m_attackState == AttackState::NONE)
    return;

  switch (m_attackState) {
  case AttackState::APPROACHING: {
    // Check if we've arrived in range
    glm::vec3 dir = m_attackTargetPos - m_pos;
    dir.y = 0.0f;
    float dist = glm::length(dir);

    if (dist <= ATTACK_RANGE) {
      // Arrived — start swing
      m_moving = false;
      m_attackState = AttackState::SWINGING;
      m_attackAnimTimer = 0.0f;
      m_attackHitRegistered = false;

      // Face the target
      m_facing = atan2f(dir.z, -dir.x);

      if (m_weaponBmd) {
        m_action = (m_swordSwingCount % 2 == 0) ? ACTION_ATTACK_SWORD_R1
                                                : ACTION_ATTACK_SWORD_R2;
      } else {
        m_action = ACTION_ATTACK_FIST;
      }
      m_animFrame = 0.0f;
      m_swordSwingCount++;
    } else if (!m_moving) {
      // Stopped moving but not in range (blocked) — cancel
      CancelAttack();
    }
    break;
  }

  case AttackState::SWINGING: {
    // Check if swing animation is done
    int numKeys = 1;
    if (m_action >= 0 && m_action < (int)m_skeleton->Actions.size())
      numKeys = m_skeleton->Actions[m_action].NumAnimationKeys;

    float animDuration = (numKeys > 1) ? (float)numKeys / ANIM_SPEED : 0.5f;
    m_attackAnimTimer += deltaTime;

    if (m_attackAnimTimer >= animDuration) {
      // Swing finished — go to cooldown
      m_attackState = AttackState::COOLDOWN;
      m_attackCooldown = ATTACK_COOLDOWN_TIME;

      // Return to combat idle (weapon stance or unarmed)
      if (m_weaponBmd) {
        m_action = GetWeaponCategoryRender(m_weaponInfo.category).actionIdle;
      } else {
        m_action = ACTION_STOP_MALE;
      }
      m_animFrame = 0.0f;
    }
    break;
  }

  case AttackState::COOLDOWN: {
    m_attackCooldown -= deltaTime;
    if (m_attackCooldown <= 0.0f) {
      // Auto-attack: if target is still valid, swing again
      if (m_attackTargetMonster >= 0) {
        // Will be re-evaluated from main.cpp which checks if target alive
        m_attackState = AttackState::NONE;
      } else {
        CancelAttack();
      }
    }
    break;
  }

  case AttackState::NONE:
    break;
  }
}

bool HeroCharacter::CheckAttackHit() {
  if (m_attackState != AttackState::SWINGING || m_attackHitRegistered)
    return false;

  int numKeys = 1;
  if (m_action >= 0 && m_action < (int)m_skeleton->Actions.size())
    numKeys = m_skeleton->Actions[m_action].NumAnimationKeys;

  float animDuration = (numKeys > 1) ? (float)numKeys / ANIM_SPEED : 0.5f;
  float hitTime = animDuration * ATTACK_HIT_FRACTION;

  if (m_attackAnimTimer >= hitTime) {
    m_attackHitRegistered = true;
    return true;
  }
  return false;
}

void HeroCharacter::CancelAttack() {
  m_attackState = AttackState::NONE;
  m_attackTargetMonster = -1;
  m_swordSwingCount = 0;
  m_moving = false; // Stop any approach movement

  // Return to appropriate idle
  if (!m_inSafeZone && m_weaponBmd) {
    m_action = GetWeaponCategoryRender(m_weaponInfo.category).actionIdle;
  } else {
    m_action = ACTION_STOP_MALE;
  }
  m_animFrame = 0.0f;
}

void HeroCharacter::TakeDamage(int damage) {
  // Accept damage when ALIVE or HIT_STUN (so rapid hits can kill)
  if (m_heroState != HeroState::ALIVE && m_heroState != HeroState::HIT_STUN)
    return;

  m_hp -= damage;
  if (m_hp <= 0) {
    m_hp = 0;
    m_heroState = HeroState::DYING;
    m_stateTimer = 0.0f;
    CancelAttack();
    m_moving = false;
    m_action = ACTION_DIE1;
    m_animFrame = 0.0f;
    std::cout << "[Hero] Dying — action=" << ACTION_DIE1 << " numActions="
              << (m_skeleton ? (int)m_skeleton->Actions.size() : 0)
              << std::endl;
  } else {
    m_heroState = HeroState::HIT_STUN;
    m_stateTimer = HIT_STUN_TIME;
    // Brief shock animation — don't interrupt attack swing
    if (m_attackState != AttackState::SWINGING) {
      m_action = ACTION_SHOCK;
      m_animFrame = 0.0f;
    }
  }
}

void HeroCharacter::UpdateState(float deltaTime) {
  switch (m_heroState) {
  case HeroState::ALIVE:
    break; // Normal operation
  case HeroState::HIT_STUN:
    m_stateTimer -= deltaTime;
    if (m_stateTimer <= 0.0f) {
      m_heroState = HeroState::ALIVE;
      // Return to appropriate idle if not attacking/moving
      if (m_attackState == AttackState::NONE && !m_moving) {
        if (!m_inSafeZone && m_weaponBmd) {
          m_action = GetWeaponCategoryRender(m_weaponInfo.category).actionIdle;
        } else {
          m_action = ACTION_STOP_MALE;
        }
        m_animFrame = 0.0f;
      }
    }
    break;
  case HeroState::DYING: {
    // Play die animation to completion, then transition to DEAD
    m_stateTimer += deltaTime; // Count up as safety timeout
    int numKeys = 1;
    if (ACTION_DIE1 < (int)m_skeleton->Actions.size())
      numKeys = m_skeleton->Actions[ACTION_DIE1].NumAnimationKeys;
    if (m_animFrame >= (float)(numKeys - 1) || m_stateTimer > 3.0f) {
      m_animFrame = (float)(numKeys - 1); // Freeze on last frame
      m_heroState = HeroState::DEAD;
      m_stateTimer = DEAD_WAIT_TIME;
      std::cout << "[Hero] Now DEAD, respawn in " << DEAD_WAIT_TIME << "s"
                << std::endl;
    }
    break;
  }
  case HeroState::DEAD:
    m_stateTimer -= deltaTime;
    // Respawn is triggered externally by main.cpp after timer expires
    break;
  case HeroState::RESPAWNING:
    // Brief invuln after respawn — return to ALIVE after timer
    m_stateTimer -= deltaTime;
    if (m_stateTimer <= 0.0f)
      m_heroState = HeroState::ALIVE;
    break;
  }
}

void HeroCharacter::Respawn(const glm::vec3 &spawnPos) {
  m_pos = spawnPos;
  SnapToTerrain();
  m_hp = m_maxHp;
  m_heroState = HeroState::RESPAWNING;
  m_stateTimer = 2.0f; // 2 seconds invulnerability
  m_moving = false;
  m_attackState = AttackState::NONE;
  m_attackTargetMonster = -1;
  // Return to idle
  if (!m_inSafeZone && m_weaponBmd) {
    m_action = GetWeaponCategoryRender(m_weaponInfo.category).actionIdle;
  } else {
    m_action = ACTION_STOP_MALE;
  }
  m_animFrame = 0.0f;
}

void HeroCharacter::SnapToTerrain() {
  if (!m_terrainData)
    return;
  const int S = TerrainParser::TERRAIN_SIZE;
  float gz = m_pos.x / 100.0f;
  float gx = m_pos.z / 100.0f;
  gz = std::clamp(gz, 0.0f, (float)(S - 2));
  gx = std::clamp(gx, 0.0f, (float)(S - 2));
  int xi = (int)gx, zi = (int)gz;
  float xd = gx - (float)xi, zd = gz - (float)zi;
  float h00 = m_terrainData->heightmap[zi * S + xi];
  float h10 = m_terrainData->heightmap[zi * S + (xi + 1)];
  float h01 = m_terrainData->heightmap[(zi + 1) * S + xi];
  float h11 = m_terrainData->heightmap[(zi + 1) * S + (xi + 1)];
  m_pos.y = h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
            h01 * (1 - xd) * zd + h11 * xd * zd;
}

void HeroCharacter::Cleanup() {
  for (int p = 0; p < PART_COUNT; ++p)
    CleanupMeshBuffers(m_parts[p].meshBuffers);
  CleanupMeshBuffers(m_weaponMeshBuffers);
  m_weaponBmd.reset();
  CleanupMeshBuffers(m_shieldMeshBuffers);
  m_shieldBmd.reset();
  for (auto &sm : m_shadowMeshes) {
    if (sm.vao)
      glDeleteVertexArrays(1, &sm.vao);
    if (sm.vbo)
      glDeleteBuffers(1, &sm.vbo);
    if (sm.ebo)
      glDeleteBuffers(1, &sm.ebo);
  }
  m_shadowMeshes.clear();
  m_shader.reset();
  m_shadowShader.reset();
  m_skeleton.reset();
}
