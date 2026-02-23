#include "HeroCharacter.hpp"
#include "TextureLoader.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>

// ─── DK Stat Formulas (MuEmu-0.97k ObjectManager.cpp) ──────────────────

uint64_t HeroCharacter::CalcXPForLevel(int level) {
  if (level <= 1)
    return 0;
  // gObjSetExperienceTable: cubic curve, MaxLevel=400
  // scaleFactor = (UINT32_MAX * 0.95) / 400^3 ≈ 63.7
  static constexpr double kScale =
      ((double)0xFFFFFFFF * 0.95) / (400.0 * 400.0 * 400.0);
  double lv = (double)level - 1.0;
  return (uint64_t)(kScale * lv * lv * lv);
}

void HeroCharacter::RecalcStats() {
  // MaxHP = 110 + 2.0*(Level-1) + (VIT-25)*3.0
  m_maxHp = (int)(DK_BASE_HP + DK_LEVEL_LIFE * (m_level - 1) +
                  (m_vitality - DK_BASE_VIT) * DK_VIT_TO_LIFE);
  if (m_maxHp < 1)
    m_maxHp = 1;

  // DK uses AG (Ability Gauge): ENE*1.0 + VIT*0.3 + DEX*0.2 + STR*0.15
  // Other classes use Mana: 20 + (Level-1)*0.5 + (Energy-10)*1
  if (m_class == 16) { // CLASS_DK
    m_maxMana = (int)(m_energy * 1.0f + m_vitality * 0.3f + m_dexterity * 0.2f +
                      m_strength * 0.15f);
  } else {
    m_maxMana = (int)(20 + (m_level - 1) * 0.5f + (m_energy - 10) * 1);
  }
  if (m_maxMana < 1)
    m_maxMana = 1;

  // Damage = STR / 6 + weapon .. STR / 4 + weapon (OpenMU DK formula)
  m_damageMin = std::max(1, (int)m_strength / 6 + m_weaponDamageMin);
  m_damageMax = std::max(m_damageMin, (int)m_strength / 4 + m_weaponDamageMax);

  // Defense = DEX / 3 + equipped armor/shield defense
  m_defense = (int)m_dexterity / 3 + m_equipDefenseBonus;

  // AttackSuccessRate = Level*5 + (DEX*3)/2 + STR/4
  m_attackSuccessRate =
      m_level * 5 + ((int)m_dexterity * 3) / 2 + (int)m_strength / 4;

  // DefenseSuccessRate = DEX / 3
  m_defenseSuccessRate = (int)m_dexterity / 3;

  // XP threshold for next level
  m_nextExperience = CalcXPForLevel(m_level + 1);
}

void HeroCharacter::GainExperience(uint64_t xp) {
  m_experience += xp;
  m_leveledUpThisFrame = false;

  while (m_experience >= m_nextExperience && m_level < 400) {
    m_level++;
    m_levelUpPoints += DK_POINTS_PER_LEVEL;
    m_leveledUpThisFrame = true;
    RecalcStats();
    m_hp = m_maxHp; // Full refill on level-up
    m_mana = m_maxMana;
    m_ag = m_maxAg;
    std::cout << "[Hero] Level up! Now level " << m_level << " (HP=" << m_maxHp
              << ", MP=" << m_maxMana << ", AG=" << m_maxAg
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
                              int levelUpPoints, int currentHp, int maxHp,
                              int currentMana, int maxMana, int currentAg,
                              int maxAg, uint8_t charClass) {
  m_level = level;
  m_class = charClass;
  m_strength = str;
  m_dexterity = dex;
  m_vitality = vit;
  m_energy = ene;
  m_experience = experience;
  m_levelUpPoints = levelUpPoints;
  RecalcStats();

  // Override with server authoritative maximums
  m_maxHp = maxHp > 0 ? maxHp : m_maxHp;
  m_maxMana = maxMana > 0 ? maxMana : m_maxMana;
  m_maxAg = maxAg > 0 ? maxAg : m_maxAg;

  // Restore current HP/Mana/AG from server (clamped to new max values)
  m_hp = std::min(currentHp, m_maxHp);
  if (m_hp <= 0 && currentHp > 0)
    m_hp = m_maxHp; // Don't load as dead if server says alive
  m_mana = std::min(currentMana, m_maxMana);
  m_ag = std::min(currentAg, m_maxAg);

  std::cout << "[Hero] Loaded stats from server: Lv" << m_level
            << " STR=" << m_strength << " DEX=" << m_dexterity
            << " VIT=" << m_vitality << " ENE=" << m_energy << " HP=" << m_hp
            << "/" << m_maxHp << " MP=" << m_mana << "/" << m_maxMana
            << " AG=" << m_ag << "/" << m_maxAg << " XP=" << m_experience
            << " pts=" << m_levelUpPoints << std::endl;
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
  // 1. Miss check — OpenMU formula (matches server)
  int atkRate = m_attackSuccessRate;
  int defRate = targetDefSuccessRate;
  int hitChance;
  if (atkRate > 0 && defRate < atkRate) {
    hitChance = 100 - (defRate * 100) / atkRate;
  } else {
    hitChance = 5;
  }
  if (hitChance < 5)
    hitChance = 5;
  if (rand() % 100 >= hitChance)
    return {0, DamageType::MISS};

  // 2. Excellent check: 1% chance, 1.2x max damage (matches server)
  int critRoll = rand() % 100;
  if (critRoll < 1) {
    int dmg = (m_damageMax * 120) / 100;
    return {std::max(1, dmg - targetDefense), DamageType::EXCELLENT};
  }

  // 3. Critical check: 5% chance, max damage (matches server)
  if (critRoll < 6) {
    int dmg = m_damageMax;
    return {std::max(1, dmg - targetDefense), DamageType::CRITICAL};
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

// Helper for smooth rotation (MU DK style interpolation)
static float smoothFacing(float current, float target, float dt) {
  float diff = target - current;
  while (diff > (float)M_PI)
    diff -= 2.0f * (float)M_PI;
  while (diff < -(float)M_PI)
    diff += 2.0f * (float)M_PI;

  if (std::abs(diff) >= (float)M_PI / 4.0f) {
    return target; // Snap for large turns (> 45°) to feel responsive
  }
  // Exponential decay: 0.5^(dt*30)
  float factor = 1.0f - std::pow(0.5f, dt * 30.0f);
  float result = current + diff * factor;
  while (result > (float)M_PI)
    result -= 2.0f * (float)M_PI;
  while (result < -(float)M_PI)
    result += 2.0f * (float)M_PI;
  return result;
}

// ─── Weapon animation helpers (Main 5.2 _enum.h + ZzzCharacter.cpp) ────────

bool HeroCharacter::isDualWielding() const {
  // DK with weapon in right hand AND weapon (not shield) in left hand
  if (m_weaponInfo.category == 0xFF || m_shieldInfo.category == 0xFF)
    return false;
  return m_shieldInfo.category != 6; // Left hand has a weapon, not a shield
}

int HeroCharacter::weaponIdleAction() const {
  if (!m_weaponBmd)
    return ACTION_STOP_MALE;

  uint8_t cat = m_weaponInfo.category;
  bool twoH = m_weaponInfo.twoHanded;

  switch (cat) {
  case 0:
  case 1:
  case 2: // Sword / Axe / Mace
    return twoH ? ACTION_STOP_TWO_HAND_SWORD : ACTION_STOP_SWORD;
  case 3: // Spear / Scythe (index >= 7 = scythe-class: Berdysh+)
    return (m_weaponInfo.itemIndex >= 7) ? ACTION_STOP_SCYTHE
                                         : ACTION_STOP_SPEAR;
  case 4: // Bow / Crossbow (index >= 8 = crossbow)
    return (m_weaponInfo.itemIndex >= 8) ? ACTION_STOP_CROSSBOW
                                         : ACTION_STOP_BOW;
  case 5: // Staff
    return ACTION_STOP_WAND;
  default:
    return ACTION_STOP_SWORD;
  }
}

int HeroCharacter::weaponWalkAction() const {
  if (!m_weaponBmd)
    return ACTION_WALK_MALE;

  uint8_t cat = m_weaponInfo.category;
  bool twoH = m_weaponInfo.twoHanded;

  switch (cat) {
  case 0:
  case 1:
  case 2: // Sword / Axe / Mace
    return twoH ? ACTION_WALK_TWO_HAND_SWORD : ACTION_WALK_SWORD;
  case 3: // Spear / Scythe
    return (m_weaponInfo.itemIndex >= 7) ? ACTION_WALK_SCYTHE
                                         : ACTION_WALK_SPEAR;
  case 4: // Bow / Crossbow
    return (m_weaponInfo.itemIndex >= 8) ? ACTION_WALK_CROSSBOW
                                         : ACTION_WALK_BOW;
  case 5: // Staff
    return ACTION_WALK_WAND;
  default:
    return ACTION_WALK_SWORD;
  }
}

int HeroCharacter::nextAttackAction() {
  if (!m_weaponBmd) {
    return ACTION_ATTACK_FIST;
  }

  uint8_t cat = m_weaponInfo.category;
  bool twoH = m_weaponInfo.twoHanded;
  int sc = m_swordSwingCount++;

  // Dual-wield: R1→L1→R2→L2 cycle (Main 5.2 SwordCount%4)
  if (isDualWielding()) {
    static constexpr int cycle[4] = {
        ACTION_ATTACK_SWORD_R1, ACTION_ATTACK_SWORD_L1, ACTION_ATTACK_SWORD_R2,
        ACTION_ATTACK_SWORD_L2};
    return cycle[sc % 4];
  }

  switch (cat) {
  case 0:
  case 1:
  case 2: // Sword / Axe / Mace
    if (twoH) {
      // Two-hand: 3 attack variants (SwordCount%3)
      return ACTION_ATTACK_TWO_HAND_SWORD1 + (sc % 3);
    }
    // One-hand: 2 attack variants (SwordCount%2)
    return (sc % 2 == 0) ? ACTION_ATTACK_SWORD_R1 : ACTION_ATTACK_SWORD_R2;
  case 3: // Spear / Scythe
    if (m_weaponInfo.itemIndex >= 7) {
      // Scythe: 3 attack variants (SwordCount%3)
      return ACTION_ATTACK_SCYTHE1 + (sc % 3);
    }
    return ACTION_ATTACK_SPEAR1; // Spear: single attack
  case 4:                        // Bow / Crossbow
    return (m_weaponInfo.itemIndex >= 8) ? ACTION_ATTACK_CROSSBOW
                                         : ACTION_ATTACK_BOW;
  case 5: // Staff — use fist attack for melee (magic is separate)
    return ACTION_ATTACK_FIST;
  default:
    return ACTION_ATTACK_SWORD_R1;
  }
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

  static auto createShadowMeshes = [](const BMDData *bmd) {
    std::vector<HeroCharacter::ShadowMesh> meshes;
    if (!bmd)
      return meshes;
    for (auto &mesh : bmd->Meshes) {
      HeroCharacter::ShadowMesh sm;
      sm.vertexCount = mesh.NumTriangles * 3; // triangulated
      sm.indexCount = sm.vertexCount;
      if (sm.vertexCount == 0) {
        meshes.push_back(sm);
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
      meshes.push_back(sm);
    }
    return meshes;
  };

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
    m_parts[p].shadowMeshes = createShadowMeshes(bmd.get());
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
    // Heal/learn animation: stretch over 3 seconds, don't loop
    bool isHealAnim =
        (m_action == ACTION_SKILL_VITALITY && m_slowAnimDuration > 0.0f);
    if (isHealAnim)
      clampAnim = true;
    // Scale attack animations faster with agility (OpenMU: DEX/15 for DK)
    bool isAttacking = (m_action >= 38 && m_action <= 51) ||
                       (m_action >= 60 && m_action <= 71);
    float speed;
    if (isHealAnim)
      speed = (float)numKeys / m_slowAnimDuration; // Stretch to fit duration
    else if (isAttacking)
      speed = ANIM_SPEED * attackSpeedMultiplier();
    else
      speed = ANIM_SPEED;
    m_animFrame += speed * deltaTime;
    if (clampAnim) {
      if (m_animFrame >= (float)(numKeys - 1))
        m_animFrame = (float)(numKeys - 1);
    } else {
      int wrapKeys = lockPos ? (numKeys - 1) : numKeys;
      if (m_animFrame >= (float)wrapKeys)
        m_animFrame = std::fmod(m_animFrame, (float)wrapKeys);
    }
  }

  // Handle cross-fade blending animation
  if (m_isBlending) {
    m_blendAlpha += deltaTime / BLEND_DURATION;
    if (m_blendAlpha >= 1.0f) {
      m_blendAlpha = 1.0f;
      m_isBlending = false;
    }
  }

  // Compute bones for current animation frame
  std::vector<BoneWorldMatrix> bones;
  if (m_isBlending && m_priorAction != -1) {
    bones = ComputeBoneMatricesBlended(m_skeleton.get(), m_priorAction,
                                       m_priorAnimFrame, m_action, m_animFrame,
                                       m_blendAlpha);
  } else {
    bones = ComputeBoneMatricesInterpolated(m_skeleton.get(), m_action,
                                            m_animFrame);
  }

  // LockPositions: root bone X/Y locked to frame 0
  if (m_rootBone >= 0) {
    int i = m_rootBone;

    float dx = 0.0f, dy = 0.0f;

    if (m_isBlending && m_priorAction != -1) {
      // Blend root offsets from both actions if they have lockPos
      bool lock1 = false, lock2 = false;
      if (m_priorAction < (int)m_skeleton->Actions.size())
        lock1 = m_skeleton->Actions[m_priorAction].LockPositions;
      if (m_action < (int)m_skeleton->Actions.size())
        lock2 = m_skeleton->Actions[m_action].LockPositions;

      float dx1 = 0.0f, dy1 = 0.0f, dx2 = 0.0f, dy2 = 0.0f;

      if (lock1) {
        glm::vec3 p1;
        glm::vec4 q1;
        if (GetInterpolatedBoneData(m_skeleton.get(), m_priorAction,
                                    m_priorAnimFrame, i, p1, q1)) {
          auto &bm1 = m_skeleton->Bones[i].BoneMatrixes[m_priorAction];
          if (!bm1.Position.empty()) {
            dx1 = p1.x - bm1.Position[0].x;
            dy1 = p1.y - bm1.Position[0].y;
          }
        }
      }
      if (lock2) {
        glm::vec3 p2;
        glm::vec4 q2;
        if (GetInterpolatedBoneData(m_skeleton.get(), m_action, m_animFrame, i,
                                    p2, q2)) {
          auto &bm2 = m_skeleton->Bones[i].BoneMatrixes[m_action];
          if (!bm2.Position.empty()) {
            dx2 = p2.x - bm2.Position[0].x;
            dy2 = p2.y - bm2.Position[0].y;
          }
        }
      }

      // Final blended offset
      dx = dx1 * (1.0f - m_blendAlpha) + dx2 * m_blendAlpha;
      dy = dy1 * (1.0f - m_blendAlpha) + dy2 * m_blendAlpha;
    } else if (lockPos) {
      // Standard single-action lock
      auto &bm = m_skeleton->Bones[i].BoneMatrixes[m_action];
      if (!bm.Position.empty()) {
        dx = bones[i][0][3] - bm.Position[0].x;
        dy = bones[i][1][3] - bm.Position[0].y;
      }
    }

    if (dx != 0.0f || dy != 0.0f) {
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

  // Draw weapon (if equipped)
  // SafeZone: weapon renders on bone 47 (back) with rotation/offset
  // Combat: weapon renders on hand bone (33 or 42) with identity offset
  // Reference: ZzzCharacter.cpp RenderCharacterBackItem (line 14634)
  static constexpr int BONE_BACK = 47;
  auto &wCat = GetWeaponCategoryRender(m_weaponInfo.category);
  int attachBone = (m_inSafeZone && BONE_BACK < (int)bones.size())
                       ? BONE_BACK
                       : wCat.attachBone;
  if (m_weaponBmd && !m_weaponMeshBuffers.empty() &&
      attachBone < (int)bones.size()) {

    // SafeZone: back rotation (70,0,90) + offset (-20,5,40) (Main 5.2 line
    // 6693) Combat: identity (weapon BMD's own bone handles orientation)
    BoneWorldMatrix weaponOffsetMat =
        m_inSafeZone
            ? MuMath::BuildWeaponOffsetMatrix(glm::vec3(70.f, 0.f, 90.f),
                                              glm::vec3(-20.f, 5.f, 40.f))
            : MuMath::BuildWeaponOffsetMatrix(glm::vec3(0, 0, 0),
                                              glm::vec3(0, 0, 0));

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

  // --- Render shield / left-hand item ---
  // SafeZone: renders on bone 47 (back) offset to not overlap weapon
  // Combat: renders on bone 42 (left hand) with identity offset
  auto &sCat = GetWeaponCategoryRender(6); // category 6 = shield
  int shieldBone = (m_inSafeZone && BONE_BACK < (int)bones.size())
                       ? BONE_BACK
                       : sCat.attachBone;
  if (m_shieldBmd && !m_shieldMeshBuffers.empty() &&
      shieldBone < (int)bones.size()) {

    // SafeZone back rendering (Main 5.2 RenderLinkObject line 6710-6731):
    // Shield: rotation (70,0,90) + offset (-10,0,0)
    // Dual-wield left weapon: rotation (-110,180,90) + offset (20,15,40)
    //   (mirrors to opposite side of back — Kayito WeaponView.cpp)
    bool dualWieldLeft = m_inSafeZone && isDualWielding();
    BoneWorldMatrix shieldOffsetMat =
        m_inSafeZone ? (dualWieldLeft ? MuMath::BuildWeaponOffsetMatrix(
                                            glm::vec3(-110.f, 180.f, 90.f),
                                            glm::vec3(20.f, 15.f, 40.f))
                                      : MuMath::BuildWeaponOffsetMatrix(
                                            glm::vec3(70.f, 0.f, 90.f),
                                            glm::vec3(-10.f, 0.f, 0.f)))
                     : MuMath::BuildWeaponOffsetMatrix(glm::vec3(0, 0, 0),
                                                       glm::vec3(0, 0, 0));

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
  if (!m_skeleton || !m_shadowShader || m_cachedBones.empty())
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

  auto renderShadowBatch = [&](const BMDData *bmd,
                               std::vector<ShadowMesh> &shadowMeshes,
                               int attachBone = -1) {
    if (!bmd)
      return;
    for (int mi = 0;
         mi < (int)bmd->Meshes.size() && mi < (int)shadowMeshes.size(); ++mi) {
      auto &sm = shadowMeshes[mi];
      if (sm.vertexCount == 0 || sm.vao == 0)
        continue;

      auto &mesh = bmd->Meshes[mi];
      std::vector<glm::vec3> shadowVerts;
      shadowVerts.reserve(sm.vertexCount);

      const float(*boneMatrix)[4] = nullptr;
      if (attachBone >= 0 && attachBone < (int)m_cachedBones.size()) {
        boneMatrix = (const float(*)[4])m_cachedBones[attachBone].data();
      }

      for (int i = 0; i < mesh.NumTriangles; ++i) {
        auto &tri = mesh.Triangles[i];
        int steps = (tri.Polygon == 3) ? 3 : 4;
        for (int v = 0; v < 3; ++v) {
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 pos = srcVert.Position;

          if (boneMatrix) {
            // Weapon/Shield: transform by attach bone
            pos = MuMath::TransformPoint(boneMatrix, pos);
          } else {
            // Body parts: transform by per-vertex bone
            int boneIdx = srcVert.Node;
            if (boneIdx >= 0 && boneIdx < (int)m_cachedBones.size()) {
              pos = MuMath::TransformPoint(
                  (const float(*)[4])m_cachedBones[boneIdx].data(), pos);
            }
          }

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

            if (boneMatrix) {
              pos = MuMath::TransformPoint(boneMatrix, pos);
            } else {
              int boneIdx = srcVert.Node;
              if (boneIdx >= 0 && boneIdx < (int)m_cachedBones.size()) {
                pos = MuMath::TransformPoint(
                    (const float(*)[4])m_cachedBones[boneIdx].data(), pos);
              }
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
  };

  // Render all active parts
  for (int p = 0; p < PART_COUNT; ++p) {
    if (m_parts[p].bmd) {
      renderShadowBatch(m_parts[p].bmd.get(), m_parts[p].shadowMeshes, -1);
    }
  }

  // Weapons and shields (bone 47 = back in SafeZone)
  if (m_weaponBmd) {
    int bone = (m_inSafeZone && 47 < (int)m_cachedBones.size())
                   ? 47
                   : GetWeaponCategoryRender(m_weaponInfo.category).attachBone;
    renderShadowBatch(m_weaponBmd.get(), m_weaponShadowMeshes, bone);
  }
  if (m_shieldBmd) {
    int bone = (m_inSafeZone && 47 < (int)m_cachedBones.size())
                   ? 47
                   : GetWeaponCategoryRender(m_shieldInfo.category).attachBone;
    renderShadowBatch(m_shieldBmd.get(), m_shieldShadowMeshes, bone);
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
    m_targetFacing = atan2f(dir.z, -dir.x);
    m_facing = smoothFacing(m_facing, m_targetFacing, deltaTime);

    glm::vec3 step = dir * m_speed * deltaTime;
    glm::vec3 newPos = m_pos + step;

    const int S = TerrainParser::TERRAIN_SIZE;
    auto isWalkableAt = [&](float wx, float wz) -> bool {
      int tgz = (int)(wx / 100.0f);
      int tgx = (int)(wz / 100.0f);
      return (tgx >= 0 && tgz >= 0 && tgx < S && tgz < S) &&
             (m_terrainData->mapping.attributes[tgz * S + tgx] & 0x04) == 0;
    };

    // Wall sliding: try full move, then X-only, then Z-only
    // (Main 5.2 MapPath.cpp: direction fallback when diagonal is blocked)
    if (isWalkableAt(newPos.x, newPos.z)) {
      m_pos.x = newPos.x;
      m_pos.z = newPos.z;
    } else if (std::abs(step.x) > 0.01f &&
               isWalkableAt(m_pos.x + step.x, m_pos.z)) {
      m_pos.x += step.x; // Slide along X axis
    } else if (std::abs(step.z) > 0.01f &&
               isWalkableAt(m_pos.x, m_pos.z + step.z)) {
      m_pos.z += step.z; // Slide along Z axis
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
      (!m_inSafeZone && m_weaponBmd) ? weaponWalkAction() : ACTION_WALK_MALE;
  if (!m_moving || m_action != walkAction) {
    SetAction(walkAction);
    m_animFrame = 0.0f;
  }
  m_moving = true;
  // Compute target facing angle (smoothFace handles interpolation)
  float dx = target.x - m_pos.x;
  float dz = target.z - m_pos.z;
  m_targetFacing = atan2f(dz, -dx);
}

void HeroCharacter::StopMoving() {
  m_moving = false;
  // Use weapon-specific idle action when outside SafeZone with weapon
  if (!m_inSafeZone && m_weaponBmd) {
    SetAction(weaponIdleAction());
  } else {
    SetAction(ACTION_STOP_MALE);
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
    SetAction((!safe && m_weaponBmd) ? weaponWalkAction() : ACTION_WALK_MALE);
  } else {
    SetAction((!safe && m_weaponBmd) ? weaponIdleAction() : ACTION_STOP_MALE);
  }

  std::cout << "[Hero] " << (safe ? "Entered SafeZone" : "Left SafeZone")
            << ", action=" << m_action << std::endl;
}

void HeroCharacter::EquipWeapon(const WeaponEquipInfo &weapon) {
  // Cleanup old weapon
  CleanupMeshBuffers(m_weaponMeshBuffers);
  for (auto &sm : m_weaponShadowMeshes) {
    if (sm.vao)
      glDeleteVertexArrays(1, &sm.vao);
    if (sm.vbo)
      glDeleteBuffers(1, &sm.vbo);
  }
  m_weaponShadowMeshes.clear();

  if (weapon.category == 0xFF) {
    m_weaponBmd.reset();
    m_weaponInfo = weapon;
    m_inSafeZone = true;
    SetAction(ACTION_STOP_MALE);
    return;
  }

  m_weaponInfo = weapon;
  std::string fullPath = m_dataPath + "/Item/" + weapon.modelFile;
  auto bmd = BMDParser::Parse(fullPath);
  if (!bmd) {
    std::cerr << "[Hero] Failed to load weapon: " << fullPath << std::endl;
    return;
  }

  AABB weaponAABB{};
  for (auto &mesh : bmd->Meshes) {
    UploadMeshWithBones(mesh, m_dataPath + "/Item/", {}, m_weaponMeshBuffers,
                        weaponAABB, false);
  }

  // Shadow meshes for weapon
  static auto createShadowMeshes = [](const BMDData *bmd) {
    std::vector<HeroCharacter::ShadowMesh> meshes;
    if (!bmd)
      return meshes;
    for (auto &mesh : bmd->Meshes) {
      HeroCharacter::ShadowMesh sm;
      sm.vertexCount = mesh.NumTriangles * 3;
      sm.indexCount = sm.vertexCount;
      if (sm.vertexCount == 0) {
        meshes.push_back(sm);
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
      meshes.push_back(sm);
    }
    return meshes;
  };
  m_weaponShadowMeshes = createShadowMeshes(bmd.get());

  m_weaponBmd = std::move(bmd);

  auto &catRender = GetWeaponCategoryRender(weapon.category);
  std::cout << "[Hero] Loaded weapon " << weapon.modelFile << ": "
            << m_weaponBmd->Meshes.size() << " meshes, "
            << m_weaponBmd->Bones.size() << " bones"
            << " (bone=" << (int)catRender.attachBone
            << " idle=" << weaponIdleAction() << " walk=" << weaponWalkAction()
            << " 2H=" << weapon.twoHanded << ")" << std::endl;

  // Update animation to combat stance if outside SafeZone
  if (!m_inSafeZone) {
    SetAction(m_moving ? weaponWalkAction() : weaponIdleAction());
    m_animFrame = 0.0f;
  }

  std::cout << "[Hero] Weapon equipped: " << weapon.modelFile << " ("
            << m_weaponMeshBuffers.size() << " GPU meshes)" << std::endl;
}

void HeroCharacter::EquipShield(const WeaponEquipInfo &shield) {
  // Cleanup old shield
  CleanupMeshBuffers(m_shieldMeshBuffers);
  for (auto &sm : m_shieldShadowMeshes) {
    if (sm.vao)
      glDeleteVertexArrays(1, &sm.vao);
    if (sm.vbo)
      glDeleteBuffers(1, &sm.vbo);
  }
  m_shieldShadowMeshes.clear();

  if (shield.category == 0xFF) {
    m_shieldBmd.reset();
    m_shieldInfo = shield;
    return;
  }

  m_shieldInfo = shield;
  std::string fullPath = m_dataPath + "/Item/" + shield.modelFile;
  auto bmd = BMDParser::Parse(fullPath);
  if (!bmd) {
    std::cerr << "[Hero] Failed to load shield: " << fullPath << std::endl;
    return;
  }

  AABB shieldAABB{};
  std::string texPath = m_dataPath + "/Item/";
  for (auto &mesh : bmd->Meshes) {
    UploadMeshWithBones(mesh, texPath, {}, m_shieldMeshBuffers, shieldAABB,
                        false);
  }

  // Shadow meshes for shield
  static auto createShadowMeshes = [](const BMDData *bmd) {
    std::vector<HeroCharacter::ShadowMesh> meshes;
    if (!bmd)
      return meshes;
    for (auto &mesh : bmd->Meshes) {
      HeroCharacter::ShadowMesh sm;
      sm.vertexCount = mesh.NumTriangles * 3;
      sm.indexCount = sm.vertexCount;
      if (sm.vertexCount == 0) {
        meshes.push_back(sm);
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
      meshes.push_back(sm);
    }
    return meshes;
  };
  m_shieldShadowMeshes = createShadowMeshes(bmd.get());

  m_shieldBmd = std::move(bmd);

  std::cout << "[Hero] Loaded shield " << shield.modelFile << ": "
            << m_shieldBmd->Meshes.size() << " meshes, "
            << m_shieldBmd->Bones.size() << " bones" << std::endl;

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
  for (auto &sm : m_parts[partIndex].shadowMeshes) {
    if (sm.vao)
      glDeleteVertexArrays(1, &sm.vao);
    if (sm.vbo)
      glDeleteBuffers(1, &sm.vbo);
  }
  m_parts[partIndex].shadowMeshes.clear();

  // Recompute bones from skeleton bind pose
  auto bones = ComputeBoneMatrices(m_skeleton.get());
  AABB partAABB{};

  for (auto &mesh : bmd->Meshes) {
    UploadMeshWithBones(mesh, m_dataPath + "/Player/", bones,
                        m_parts[partIndex].meshBuffers, partAABB, true);
  }

  // Shadow meshes for body part
  static auto createShadowMeshes = [](const BMDData *bmd) {
    std::vector<HeroCharacter::ShadowMesh> meshes;
    if (!bmd)
      return meshes;
    for (auto &mesh : bmd->Meshes) {
      HeroCharacter::ShadowMesh sm;
      sm.vertexCount = mesh.NumTriangles * 3;
      sm.indexCount = sm.vertexCount;
      if (sm.vertexCount == 0) {
        meshes.push_back(sm);
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
      meshes.push_back(sm);
    }
    return meshes;
  };
  m_parts[partIndex].shadowMeshes = createShadowMeshes(bmd.get());

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
  if (monsterIndex == m_attackTargetMonster && m_activeSkillId == 0 &&
      (m_attackState == AttackState::SWINGING ||
       m_attackState == AttackState::COOLDOWN)) {
    m_attackTargetPos = monsterPos;
    return;
  }

  m_attackTargetMonster = monsterIndex;
  m_attackTargetPos = monsterPos;
  m_activeSkillId = 0; // Normal attack, no skill

  // Check distance
  glm::vec3 dir = monsterPos - m_pos;
  dir.y = 0.0f;
  float dist = glm::length(dir);

  if (dist <= getAttackRange()) {
    // In range — start swinging
    m_attackState = AttackState::SWINGING;
    m_attackAnimTimer = 0.0f;
    m_attackHitRegistered = false;
    m_moving = false;

    // Face the target
    m_targetFacing = atan2f(dir.z, -dir.x);

    // Weapon-type-specific attack animation (Main 5.2 SwordCount cycle)
    SetAction(nextAttackAction());
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

    if (dist <= getAttackRange()) {
      // Arrived — start swing
      m_moving = false;
      m_attackState = AttackState::SWINGING;
      m_attackAnimTimer = 0.0f;
      m_attackHitRegistered = false;

      // Face the target
      m_targetFacing = atan2f(dir.z, -dir.x);

      // Skill or weapon-type-specific attack animation
      if (m_activeSkillId > 0)
        SetAction(GetSkillAction(m_activeSkillId));
      else
        SetAction(nextAttackAction());
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

    float atkAnimSpeed = ANIM_SPEED * attackSpeedMultiplier();
    float animDuration = (numKeys > 1) ? (float)numKeys / atkAnimSpeed : 0.5f;
    m_attackAnimTimer += deltaTime;

    if (m_attackAnimTimer >= animDuration) {
      // Swing finished — go to cooldown (also scaled by attack speed)
      m_attackState = AttackState::COOLDOWN;
      m_attackCooldown = ATTACK_COOLDOWN_TIME / attackSpeedMultiplier();

      // Return to combat idle (weapon stance or unarmed)
      SetAction(m_weaponBmd ? weaponIdleAction() : ACTION_STOP_MALE);
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
        m_activeSkillId =
            0; // Reset so auto-attack re-engages with normal attacks
      } else {
        CancelAttack();
      }
    }
    break;
  }

  case AttackState::NONE:
    break;
  }

  // Smoothly rotate towards target facing in any attack state
  m_facing = smoothFacing(m_facing, m_targetFacing, deltaTime);
}

bool HeroCharacter::CheckAttackHit() {
  if (m_attackState != AttackState::SWINGING || m_attackHitRegistered)
    return false;

  int numKeys = 1;
  if (m_action >= 0 && m_action < (int)m_skeleton->Actions.size())
    numKeys = m_skeleton->Actions[m_action].NumAnimationKeys;

  float atkAnimSpeed = ANIM_SPEED * attackSpeedMultiplier();
  float animDuration = (numKeys > 1) ? (float)numKeys / atkAnimSpeed : 0.5f;
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
  m_activeSkillId = 0;
  m_swordSwingCount = 0;
  m_moving = false; // Stop any approach movement

  // Return to appropriate idle
  if (!m_inSafeZone && m_weaponBmd) {
    SetAction(weaponIdleAction());
  } else {
    SetAction(ACTION_STOP_MALE);
  }
}

int HeroCharacter::GetSkillAction(uint8_t skillId) {
  switch (skillId) {
  case 19:
    return ACTION_SKILL_SWORD1; // Falling Slash
  case 20:
    return ACTION_SKILL_SWORD2; // Lunge
  case 21:
    return ACTION_SKILL_SWORD3; // Uppercut
  case 22:
    return ACTION_SKILL_SWORD4; // Cyclone
  case 23:
    return ACTION_SKILL_SWORD5; // Slash
  case 41:
    return ACTION_SKILL_WHEEL; // Twisting Slash
  case 43:
    return ACTION_SKILL_DEATH_STAB; // Death Stab
  default:
    return ACTION_SKILL_SWORD1; // Fallback
  }
}

void HeroCharacter::SkillAttackMonster(int monsterIndex,
                                       const glm::vec3 &monsterPos,
                                       uint8_t skillId) {
  if (IsDead())
    return;

  // Already swinging same target with same skill — just update position
  if (monsterIndex == m_attackTargetMonster && m_activeSkillId == skillId &&
      (m_attackState == AttackState::SWINGING ||
       m_attackState == AttackState::COOLDOWN)) {
    m_attackTargetPos = monsterPos;
    return;
  }

  m_attackTargetMonster = monsterIndex;
  m_attackTargetPos = monsterPos;
  m_activeSkillId = skillId;

  glm::vec3 dir = monsterPos - m_pos;
  dir.y = 0.0f;
  float dist = glm::length(dir);

  int skillAction = GetSkillAction(skillId);
  std::cout << "[Skill] SkillAttackMonster: monIdx=" << monsterIndex
            << " skillId=" << (int)skillId << " action=" << skillAction
            << " dist=" << dist << " range=" << getAttackRange() << std::endl;

  if (dist <= getAttackRange()) {
    m_attackState = AttackState::SWINGING;
    m_attackAnimTimer = 0.0f;
    m_attackHitRegistered = false;
    m_moving = false;
    m_targetFacing = atan2f(dir.z, -dir.x);
    SetAction(skillAction);
    std::cout << "[Skill] Started SWINGING with action " << skillAction
              << std::endl;
  } else {
    m_attackState = AttackState::APPROACHING;
    MoveTo(monsterPos);
    std::cout << "[Skill] APPROACHING target (too far)" << std::endl;
  }
}

void HeroCharacter::ApplyHitReaction() {
  // Only trigger if alive (don't interrupt dying/dead)
  if (m_heroState != HeroState::ALIVE && m_heroState != HeroState::HIT_STUN)
    return;

  m_heroState = HeroState::HIT_STUN;
  m_stateTimer = HIT_STUN_TIME;
  m_moving = false; // Stop sliding when playing hit reaction
  // Brief shock animation — don't interrupt attack swing
  if (m_attackState != AttackState::SWINGING) {
    SetAction(ACTION_SHOCK);
  }
}

void HeroCharacter::TakeDamage(int damage) {
  // Accept damage when ALIVE or HIT_STUN (so rapid hits can kill)
  if (m_heroState != HeroState::ALIVE && m_heroState != HeroState::HIT_STUN)
    return;

  m_hp -= damage;
  if (m_hp <= 0) {
    ForceDie();
  } else {
    ApplyHitReaction();
  }
}

void HeroCharacter::ForceDie() {
  m_hp = 0;
  m_heroState = HeroState::DYING;
  m_stateTimer = 0.0f;
  CancelAttack();
  m_moving = false;
  SetAction(ACTION_DIE1);
  std::cout << "[Hero] Dying (Forced) — action=" << ACTION_DIE1
            << " numActions="
            << (m_skeleton ? (int)m_skeleton->Actions.size() : 0) << std::endl;
}

void HeroCharacter::UpdateState(float deltaTime) {
  switch (m_heroState) {
  case HeroState::ALIVE:
    // HP Regeneration in Safe Zone (~2% of Max HP per second)
    if (m_inSafeZone && m_hp < m_maxHp) {
      m_hpRemainder += 0.02f * (float)m_maxHp * deltaTime;
      float threshold = std::max(1.0f, 0.02f * (float)m_maxHp);
      if (m_hpRemainder >= threshold) {
        int gain = (int)m_hpRemainder;
        m_hp = std::min(m_hp + gain, m_maxHp);
        m_hpRemainder -= (float)gain;
        std::cout << "[Regen] Hero healed +" << gain
                  << " HP in SafeZone (Local). New HP: " << m_hp << "/"
                  << m_maxHp << std::endl;
      }
    } else {
      m_hpRemainder = 0.0f;
    }
    break; // Normal operation
  case HeroState::HIT_STUN:
    m_stateTimer -= deltaTime;
    if (m_stateTimer <= 0.0f) {
      m_heroState = HeroState::ALIVE;
      // Return to appropriate idle if not attacking/moving
      if (m_attackState == AttackState::NONE && !m_moving) {
        if (!m_inSafeZone && m_weaponBmd) {
          SetAction(weaponIdleAction());
        } else {
          SetAction(ACTION_STOP_MALE);
        }
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

  // Final step: ensure we are always snapped to the ground heights
  if (m_heroState != HeroState::DYING && m_heroState != HeroState::DEAD) {
    SnapToTerrain();
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
    SetAction(weaponIdleAction());
  } else {
    SetAction(ACTION_STOP_MALE);
  }
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

void HeroCharacter::SetAction(int newAction) {
  if (m_action == newAction)
    return;

  // Cross-fade blending for smooth transitions:
  // - Fist attack transitions
  // - Walk -> idle (stopping)
  // - Attack -> combat idle (weapon attacks finishing)
  bool involvesFists =
      (m_action == ACTION_ATTACK_FIST || newAction == ACTION_ATTACK_FIST);

  // Detect walk actions (15-23) and stop/idle actions (0-10)
  bool isWalkingAction = (m_action >= 15 && m_action <= 23);
  bool isStopAction = (newAction >= 0 && newAction <= 10);
  bool isStopping = (isWalkingAction && isStopAction);

  // Attack/skill -> combat idle blend (all weapon types + skill actions)
  bool isAttackAction =
      (m_action >= 38 && m_action <= 51) || (m_action >= 60 && m_action <= 71);
  bool isAttackToIdle = (isAttackAction && isStopAction);

  if (involvesFists || isStopping || isAttackToIdle) {
    m_priorAction = m_action;
    m_priorAnimFrame = m_animFrame;
    m_isBlending = true;
    m_blendAlpha = 0.0f;
  } else {
    m_isBlending = false;
    m_blendAlpha = 1.0f;
  }

  m_action = newAction;
  m_animFrame = 0.0f;
}

void HeroCharacter::Cleanup() {
  auto cleanupShadows = [](std::vector<ShadowMesh> &shadowMeshes) {
    for (auto &sm : shadowMeshes) {
      if (sm.vao)
        glDeleteVertexArrays(1, &sm.vao);
      if (sm.vbo)
        glDeleteBuffers(1, &sm.vbo);
    }
    shadowMeshes.clear();
  };

  for (int p = 0; p < PART_COUNT; ++p) {
    CleanupMeshBuffers(m_parts[p].meshBuffers);
    cleanupShadows(m_parts[p].shadowMeshes);
  }

  CleanupMeshBuffers(m_weaponMeshBuffers);
  cleanupShadows(m_weaponShadowMeshes);
  m_weaponBmd.reset();

  CleanupMeshBuffers(m_shieldMeshBuffers);
  cleanupShadows(m_shieldShadowMeshes);
  m_shieldBmd.reset();

  m_shader.reset();
  m_shadowShader.reset();
  m_skeleton.reset();
}
