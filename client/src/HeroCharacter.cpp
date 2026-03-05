#include "HeroCharacter.hpp"
#include "ItemModelManager.hpp"
#include "SoundManager.hpp"
#include "TextureLoader.hpp"
#include "VFXManager.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

// Class code → body part suffix: DW=Class01, DK=Class02, ELF=Class03, MG=Class04
static const char *GetClassBodySuffix(uint8_t classCode) {
  switch (classCode) {
  case 0:  return "Class01"; // DW
  case 16: return "Class02"; // DK
  case 32: return "Class03"; // ELF
  case 48: return "Class04"; // MG
  default: return "Class02";
  }
}

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

  // AG/Mana per class (OpenMU formulas, matching server StatCalculator)
  if (m_class == 16) { // CLASS_DK — AG
    m_maxMana = (int)(m_energy * 1.0f + m_vitality * 0.3f + m_dexterity * 0.2f +
                      m_strength * 0.15f);
  } else if (m_class == 0) { // CLASS_DW
    m_maxMana = 60 + (m_level - 1) * 2 + (m_energy - 30) * 2;
  } else if (m_class == 32) { // CLASS_ELF
    m_maxMana = (int)(30 + (m_level - 1) * 1.5f + (m_energy - 15) * 1.5f);
  } else if (m_class == 48) { // CLASS_MG
    m_maxMana = 60 + (m_level - 1) * 1 + (m_energy - 26) * 2;
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
  uint8_t oldClass = m_class;
  m_level = level;
  m_class = charClass;

  // Reload default body parts if class changed (e.g. DK→DW)
  if (m_class != oldClass && m_skeleton) {
    for (int i = 0; i < PART_COUNT; i++)
      EquipBodyPart(i, ""); // empty = reload class default
    std::cout << "[Hero] Class changed " << (int)oldClass << " -> "
              << (int)m_class << ", reloaded body parts" << std::endl;
  }
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
  // Only recalc damage — don't call RecalcStats which clobbers server-authoritative HP/AG
  m_damageMin = std::max(1, (int)m_strength / 6 + m_weaponDamageMin);
  m_damageMax = std::max(m_damageMin, (int)m_strength / 4 + m_weaponDamageMax);
}

void HeroCharacter::SetDefenseBonus(int def) {
  m_equipDefenseBonus = def;
  // Only recalc defense — don't call RecalcStats which clobbers server-authoritative HP/AG
  m_defense = (int)m_dexterity / 3 + m_equipDefenseBonus;
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
  if (isMountRiding())
    return m_weaponBmd ? ACTION_STOP_RIDE_WEAPON : ACTION_STOP_RIDE;

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
  case 5: // Staff — Main 5.2: WAND animation only for items 14-20 (Season 2+)
    if (m_weaponInfo.itemIndex >= 14 && m_weaponInfo.itemIndex <= 20)
      return ACTION_STOP_WAND;
    return twoH ? ACTION_STOP_SCYTHE : ACTION_STOP_SWORD;
  default:
    return ACTION_STOP_SWORD;
  }
}

int HeroCharacter::weaponWalkAction() const {
  // Both mounts bounce — character uses running ride animation
  if (isMountRiding())
    return m_weaponBmd ? ACTION_RUN_RIDE_WEAPON : ACTION_RUN_RIDE;

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
  case 5: // Staff — Main 5.2: WAND animation only for items 14-20 (Season 2+)
    if (m_weaponInfo.itemIndex >= 14 && m_weaponInfo.itemIndex <= 20)
      return ACTION_WALK_WAND;
    return twoH ? ACTION_WALK_SCYTHE : ACTION_WALK_SWORD;
  default:
    return ACTION_WALK_SWORD;
  }
}

int HeroCharacter::nextAttackAction() {
  if (isMountRiding()) {
    if (!m_weaponBmd) return ACTION_ATTACK_RIDE_SWORD;
    uint8_t rideCat = m_weaponInfo.category;
    switch (rideCat) {
    case 0: case 1: case 2:
      return m_weaponInfo.twoHanded ? ACTION_ATTACK_RIDE_TWO_HAND_SWORD
                                    : ACTION_ATTACK_RIDE_SWORD;
    case 3:
      return (m_weaponInfo.itemIndex >= 7) ? ACTION_ATTACK_RIDE_SCYTHE
                                           : ACTION_ATTACK_RIDE_SPEAR;
    case 4:
      return (m_weaponInfo.itemIndex >= 8) ? ACTION_ATTACK_RIDE_CROSSBOW
                                           : ACTION_ATTACK_RIDE_BOW;
    default: return ACTION_ATTACK_RIDE_SWORD;
    }
  }

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

  // Load naked body parts for current class
  const char *suffix = GetClassBodySuffix(m_class);
  char partFiles[5][64];
  snprintf(partFiles[0], 64, "Helm%s.bmd", suffix);
  snprintf(partFiles[1], 64, "Armor%s.bmd", suffix);
  snprintf(partFiles[2], 64, "Pant%s.bmd", suffix);
  snprintf(partFiles[3], 64, "Glove%s.bmd", suffix);
  snprintf(partFiles[4], 64, "Boot%s.bmd", suffix);

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

  m_weaponTrailValid = false; // Reset each frame

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
    bool isAttacking = (m_action >= 38 && m_action <= 59) ||
                       (m_action >= 60 && m_action <= 71) ||
                       (m_action >= 146 && m_action <= 154);
    // Main 5.2 ride PlaySpeeds: idle 0.28 (7.0fps), walk 0.3 (7.5fps)
    bool isRideIdle = (m_action == ACTION_STOP_RIDE ||
                       m_action == ACTION_STOP_RIDE_WEAPON);
    bool isRideWalk = (m_action == ACTION_RUN_RIDE ||
                       m_action == ACTION_RUN_RIDE_WEAPON);
    float speed;
    if (isHealAnim)
      speed = (float)numKeys / m_slowAnimDuration; // Stretch to fit duration
    else if (isAttacking)
      speed = ANIM_SPEED * attackSpeedMultiplier();
    else if (isRideIdle)
      speed = 7.0f;  // Main 5.2: PlaySpeed 0.28 * 25fps
    else if (isRideWalk)
      speed = 7.5f;  // Main 5.2: PlaySpeed 0.3 * 25fps
    else
      speed = ANIM_SPEED;
    // Main 5.2: Flash animation slowdown during gathering phase (frames 1.0-3.0)
    // PlaySpeed /= 2 creates dramatic wind-up before beam release
    if (m_activeSkillId == 12 && m_animFrame >= 1.0f && m_animFrame < 3.0f)
      speed *= 0.5f;

    m_animFrame += speed * deltaTime;
    if (clampAnim) {
      if (m_animFrame >= (float)(numKeys - 1))
        m_animFrame = (float)(numKeys - 1);
    } else {
      int wrapKeys = lockPos ? (numKeys - 1) : numKeys;
      if (m_animFrame >= (float)wrapKeys)
        m_animFrame = std::fmod(m_animFrame, (float)wrapKeys);
    }

    // Main 5.2: footstep sounds at animation frames 1.5 and 4.5 during walk
    // City (safe zone) uses soil footsteps, fields use grass
    // Slight pitch variation (0.9-1.1) so steps don't sound identical
    if (m_moving) {
      int walkSound = m_inSafeZone ? SOUND_WALK_SOIL : SOUND_WALK_GRASS;
      if (m_animFrame >= 1.5f && !m_foot[0]) {
        m_foot[0] = true;
        SoundManager::PlayPitched(walkSound, 0.9f, 1.1f);
      }
      if (m_animFrame >= 4.5f && !m_foot[1]) {
        m_foot[1] = true;
        SoundManager::PlayPitched(walkSound, 0.9f, 1.1f);
      }
      // Reset feet on animation wrap
      if (m_animFrame < 1.0f) {
        m_foot[0] = false;
        m_foot[1] = false;
      }
    } else {
      m_foot[0] = false;
      m_foot[1] = false;
    }
  }

  // Main 5.2: Flash (Aqua Beam) — gathering during wind-up, beam at frame 7.0
  if (m_pendingAquaBeam && m_activeSkillId == 12 && m_vfxManager) {
    // Gathering particles during frames 1.2-3.0 (BITMAP_GATHERING SubType 2)
    if (m_animFrame >= 1.2f && m_animFrame < 3.0f) {
      m_aquaGatherTimer += deltaTime;
      while (m_aquaGatherTimer >= 0.04f) {
        m_aquaGatherTimer -= 0.04f;
        glm::vec3 handPos = m_pos + glm::vec3(0.0f, 120.0f, 0.0f);
        m_vfxManager->SpawnAquaGathering(handPos);
      }
    }
    // Main 5.2: visual beam triggers at frames 7.0-8.0 (hands fully extended)
    if (m_animFrame >= 7.0f && !m_aquaBeamSpawned) {
      m_aquaBeamSpawned = true;
      m_vfxManager->SpawnAquaBeam(m_pos, m_facing);
      m_pendingAquaBeam = false;
      m_aquaPacketReady = true; // Signal main loop to send damage packet now
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
      // Blend root offsets from both actions if they have lockPos.
      // When mounted: force lock for all actions to keep player on mount.
      bool mounted = isMountRiding();
      bool lock1 = mounted, lock2 = mounted;
      if (m_priorAction < (int)m_skeleton->Actions.size())
        lock1 = lock1 || m_skeleton->Actions[m_priorAction].LockPositions;
      if (m_action < (int)m_skeleton->Actions.size())
        lock2 = lock2 || m_skeleton->Actions[m_action].LockPositions;

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
    } else if (lockPos || isMountRiding()) {
      // Standard single-action lock.
      // When mounted: always strip XY root motion to keep player aligned with mount,
      // even if ride actions don't have LockPositions set in BMD.
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
  // Re-skin base head (for accessory helms that show face)
  if (m_showBaseHead && m_baseHead.bmd) {
    for (int mi = 0; mi < (int)m_baseHead.meshBuffers.size() &&
                     mi < (int)m_baseHead.bmd->Meshes.size();
         ++mi) {
      RetransformMeshWithBones(m_baseHead.bmd->Meshes[mi], bones,
                               m_baseHead.meshBuffers[mi]);
    }
  }

  // Build model matrix: translate -> MU->GL coord conversion -> facing rotation
  // Main 5.2: when riding Dinorant, player is elevated +30 above terrain (sits on mount).
  // Uniria: no height offset (unicorn is smaller).
  glm::vec3 renderPos = m_pos;
  if (isMountRiding() && m_mount.itemIndex == 3) // Dinorant
    renderPos.y += 30.0f;
  glm::mat4 model = glm::translate(glm::mat4(1.0f), renderPos);
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
  m_shader->setVec3("uFogColor", glm::vec3(0.117f, 0.078f, 0.039f));
  m_shader->setFloat("uFogNear", 1500.0f);
  m_shader->setFloat("uFogFar", 3500.0f);
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
  // Draw base head for accessory helms (face visible underneath helm)
  if (m_showBaseHead) {
    for (auto &mb : m_baseHead.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
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

    // Use cached weapon local bones (static bind-pose, computed once at equip)
    const auto &wLocalBones = m_weaponLocalBones;
    std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
    for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
      MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                               (const float(*)[4])wLocalBones[bi].data(),
                               (float(*)[4])wFinalBones[bi].data());
    }

    // Compute weapon blur trail points (Main 5.2: BlurType 1)
    // Weapon-local offsets transformed through parentMat → BMD-local → world
    if (m_weaponTrailActive && !m_inSafeZone) {
      glm::vec3 tipLocal(0.f, -20.f, 0.f);    // Blade tip (BlurType 1)
      glm::vec3 baseLocal(0.f, -120.f, 0.f);   // Blade base
      glm::vec3 tipBmd = MuMath::TransformPoint(
          (const float(*)[4])parentMat.data(), tipLocal);
      glm::vec3 baseBmd = MuMath::TransformPoint(
          (const float(*)[4])parentMat.data(), baseLocal);
      // Apply character model transform: rotZ(facing) → rotY(-90) → rotZ(-90)
      float cosF = cosf(m_facing), sinF = sinf(m_facing);
      auto toWorld = [&](const glm::vec3 &bmd) -> glm::vec3 {
        float rx = bmd.x * cosF - bmd.y * sinF;
        float ry = bmd.x * sinF + bmd.y * cosF;
        return m_pos + glm::vec3(ry, bmd.z, rx);
      };
      m_weaponTrailTip = toWorld(tipBmd);
      m_weaponTrailBase = toWorld(baseBmd);
      m_weaponTrailValid = true;
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

      // Main 5.2: ItemLight — glow mesh renders additively with pulsing brightness
      if (m_weaponBlendMesh >= 0 && mi == m_weaponBlendMesh) {
        float pulseLight = sinf((float)glfwGetTime() * 4.0f) * 0.3f + 0.7f;
        m_shader->setFloat("blendMeshLight", pulseLight);
        glBlendFunc(GL_ONE, GL_ONE); // Additive
        glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_shader->setFloat("blendMeshLight", 1.0f);
      } else {
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      }
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

    const auto &sLocalBones = m_shieldLocalBones;
    std::vector<BoneWorldMatrix> sFinalBones(sLocalBones.size());
    for (int bi = 0; bi < (int)sLocalBones.size(); ++bi) {
      MuMath::ConcatTransforms((const float(*)[4])shieldParentMat.data(),
                               (const float(*)[4])sLocalBones[bi].data(),
                               (float(*)[4])sFinalBones[bi].data());
    }

    // Re-skin shield vertices
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
      glBufferSubData(GL_ARRAY_BUFFER, 0,
                      verts.size() * sizeof(ViewerVertex), verts.data());
    }
    // Draw shield meshes
    for (auto &mb : m_shieldMeshBuffers) {
      if (mb.indexCount == 0)
        continue;
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }

  // ── Twisting Slash: render ghost weapon copies orbiting the hero ──
  if (m_twistingSlashActive && !m_ghostWeaponMeshBuffers.empty()) {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Additive blend for ghostly glow
    glDepthMask(GL_FALSE);

    for (int i = 0; i < MAX_WHEEL_GHOSTS; ++i) {
      const auto &g = m_wheelGhosts[i];
      if (!g.active)
        continue;

      // Ghost world position: orbit around hero at radius 150, height +100
      float orbitRad = glm::radians(g.orbitAngle);
      glm::vec3 ghostPos =
          m_pos + glm::vec3(sinf(orbitRad) * 150.0f, 100.0f,
                            cosf(orbitRad) * 150.0f);

      // Build model matrix:
      // 1. Translate to ghost position
      // 2. Standard BMD coordinate conversion (-90Z, -90Y)
      // 3. Face along orbit tangent (orbitAngle + 90°)
      // 4. Horizontal tilt (weapon blade parallel to ground)
      // 5. Self-spin (accelerating rotation)
      glm::mat4 ghostModel = glm::translate(glm::mat4(1.0f), ghostPos);
      ghostModel = glm::rotate(ghostModel, glm::radians(-90.0f),
                                glm::vec3(0, 0, 1));
      ghostModel = glm::rotate(ghostModel, glm::radians(-90.0f),
                                glm::vec3(0, 1, 0));
      // Orbital facing: tangent direction
      ghostModel = glm::rotate(ghostModel, glm::radians(g.orbitAngle),
                                glm::vec3(0, 0, 1));
      // Main 5.2: Angle[1] = 90 → horizontal tilt
      ghostModel = glm::rotate(ghostModel, glm::radians(90.0f),
                                glm::vec3(0, 1, 0));
      // Accelerating self-spin
      ghostModel = glm::rotate(ghostModel, glm::radians(g.spinAngle),
                                glm::vec3(0, 0, 1));
      // Main 5.2: ItemObjectAttribute sets Scale=0.8 for weapon items
      ghostModel = glm::scale(ghostModel, glm::vec3(0.8f));

      m_shader->setMat4("model", ghostModel);
      m_shader->setFloat("objectAlpha", g.alpha);
      m_shader->setFloat("blendMeshLight", 1.0f);

      for (auto &mb : m_ghostWeaponMeshBuffers) {
        if (mb.indexCount == 0)
          continue;
        glBindTexture(GL_TEXTURE_2D, mb.texture);
        glBindVertexArray(mb.vao);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      }
    }

    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Restore the character's model matrix and objectAlpha for subsequent renders
    m_shader->setFloat("objectAlpha", 1.0f);
    glm::mat4 charModel = glm::translate(glm::mat4(1.0f), renderPos);
    charModel = glm::rotate(charModel, glm::radians(-90.0f),
                             glm::vec3(0, 0, 1));
    charModel = glm::rotate(charModel, glm::radians(-90.0f),
                             glm::vec3(0, 1, 0));
    charModel =
        glm::rotate(charModel, m_facing, glm::vec3(0, 0, 1));
    m_shader->setMat4("model", charModel);
  }

  // ── Mount rendering (Uniria / Dinorant) ──
  // Main 5.2 GOBoid.cpp: mount renders at player position with Z offset.
  // Mount and player have independent animation frames (both ~0.34f/tick).
  if (m_mount.active && m_mount.bmd && !m_mount.meshBuffers.empty()) {
    // Main 5.2: alpha fades to 0 in safe zone, 1 outside
    float targetAlpha = m_inSafeZone ? 0.0f : 1.0f;
    m_mount.alpha += (targetAlpha - m_mount.alpha) * glm::clamp(deltaTime * 4.0f, 0.0f, 1.0f);
    if (m_mount.alpha > 0.99f) m_mount.alpha = 1.0f;
    if (m_mount.alpha < 0.01f) m_mount.alpha = 0.0f;

    // Mount animation: action mapping based on owner's state.
    // Rider01 (Uniria): idle=0, walk=2, attack=3  (4 actions)
    // Rider02 (Dinorant): idle=0, walk=2, attack=4 (8 actions, ground maps)
    // Main 5.2 GOBoid.cpp: mount action set based on owner's current action
    bool isRideAttack = (m_action >= ACTION_ATTACK_RIDE_SWORD &&
                         m_action <= ACTION_ATTACK_RIDE_CROSSBOW);
    int mountAction;
    if (isRideAttack) {
      mountAction = (m_mount.itemIndex == 3) ? 4 : 3; // Dinorant=4, Uniria=3
    } else {
      mountAction = m_moving ? 2 : 0;
    }
    if (mountAction >= (int)m_mount.bmd->Actions.size())
      mountAction = 0;

    // Sync mount animation frame to player's ride animation frame.
    // Both Player.bmd ride actions and Rider01/02.bmd walk actions have matching
    // key counts (7 for walk, 6 for idle) and identical 2-beat gallop Z patterns.
    // Independent frames cause visible rider/mount Z separation (~25 unit bounce).
    int mountNumKeys = m_mount.bmd->Actions[mountAction].NumAnimationKeys;
    bool mountLockPos = m_mount.bmd->Actions[mountAction].LockPositions;
    int mountWrapKeys = mountLockPos ? (mountNumKeys - 1) : mountNumKeys;
    if (mountWrapKeys > 0) {
      m_mount.animFrame = std::fmod(m_animFrame, (float)mountWrapKeys);
    }

    // Compute mount bones and re-skin meshes
    auto mountBones = ComputeBoneMatricesInterpolated(m_mount.bmd.get(), mountAction,
                                                       m_mount.animFrame);
    // Remove HORIZONTAL root motion only — vertical bounce preserved in bones.
    // Use m_mount.rootBone (0 for Rider01/Uniria, 1 for Rider02/Dinorant)
    // because Rider02's bone 0 is a static Box01 helper, not the animated root.
    int rb = m_mount.rootBone;
    if (!mountBones.empty() && rb < (int)mountBones.size()) {
      glm::vec3 idlePos, curPos;
      glm::vec4 dummyQ;
      GetInterpolatedBoneData(m_mount.bmd.get(), 0, 0.0f, rb, idlePos, dummyQ);
      GetInterpolatedBoneData(m_mount.bmd.get(), mountAction, m_mount.animFrame,
                              rb, curPos, dummyQ);
      float dx = curPos.x - idlePos.x;
      float dy = curPos.y - idlePos.y;
      for (auto &bone : mountBones) {
        bone[0][3] -= dx;
        bone[1][3] -= dy;
      }
    }
    for (int mi = 0; mi < (int)m_mount.meshBuffers.size() &&
                     mi < (int)m_mount.bmd->Meshes.size(); ++mi) {
      RetransformMeshWithBones(m_mount.bmd->Meshes[mi], mountBones,
                               m_mount.meshBuffers[mi]);
    }

    // Skip GPU rendering when fully faded (safe zone), but animation stays updated
    if (m_mount.alpha > 0.0f) {
      // Main 5.2 GOBoid.cpp: mount at player pos, Dinorant offset -30 Z (ground level)
      // Player is elevated +30 (set above in renderPos), mount stays at terrain.
      glm::mat4 mountModel = glm::translate(glm::mat4(1.0f), m_pos);
      mountModel = glm::rotate(mountModel, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      mountModel = glm::rotate(mountModel, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      mountModel = glm::rotate(mountModel, m_facing, glm::vec3(0, 0, 1));
      // Main 5.2 RenderMount: overrides creation scale to 1.0 for in-game rendering
      // (0.9 during CreateMountSub, but 1.0 in RenderMount for all non-Fenrir mounts)
      mountModel = glm::scale(mountModel, glm::vec3(1.0f));

      m_shader->setMat4("model", mountModel);
      m_shader->setFloat("objectAlpha", m_mount.alpha);
      m_shader->setFloat("blendMeshLight", 1.0f);
      m_shader->setVec3("terrainLight", tLight);

      glDisable(GL_CULL_FACE);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      // Pass 1: Normal (non-blend) meshes
      for (int mi = 0; mi < (int)m_mount.meshBuffers.size() &&
                       mi < (int)m_mount.bmd->Meshes.size(); ++mi) {
        auto &mb = m_mount.meshBuffers[mi];
        if (mb.indexCount == 0 || mb.hidden) continue;
        int bmdTex = m_mount.bmd->Meshes[mi].Texture;
        if (bmdTex == m_mount.blendMesh || mb.bright) continue;
        glBindTexture(GL_TEXTURE_2D, mb.texture);
        glBindVertexArray(mb.vao);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      }

      // Pass 2: Additive blend meshes (glow/wings)
      if (m_mount.blendMesh >= 0) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        m_shader->setFloat("blendMeshLight", 1.5f);
        for (int mi = 0; mi < (int)m_mount.meshBuffers.size() &&
                         mi < (int)m_mount.bmd->Meshes.size(); ++mi) {
          auto &mb = m_mount.meshBuffers[mi];
          if (mb.indexCount == 0 || mb.hidden) continue;
          int bmdTex = m_mount.bmd->Meshes[mi].Texture;
          if (bmdTex != m_mount.blendMesh && !mb.bright) continue;
          glBindTexture(GL_TEXTURE_2D, mb.texture);
          glBindVertexArray(mb.vao);
          glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_shader->setFloat("blendMeshLight", 1.0f);
      }

      glEnable(GL_CULL_FACE);

      // Restore shader state for subsequent rendering
      m_shader->setFloat("objectAlpha", 1.0f);
      glm::mat4 restoreModel = glm::translate(glm::mat4(1.0f), m_pos);
      restoreModel = glm::rotate(restoreModel, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      restoreModel = glm::rotate(restoreModel, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      restoreModel = glm::rotate(restoreModel, m_facing, glm::vec3(0, 0, 1));
      m_shader->setMat4("model", restoreModel);
    }
  }

  // ── Pet companion: update + render (Guardian Angel / Imp) ──
  // Main 5.2 GOBoid.cpp: Direction-vector movement with random wandering
  if (m_pet.active && m_pet.bmd && !m_pet.meshBuffers.empty()) {
    constexpr float FLY_RANGE = 100.0f;         // Max wander distance from owner
    constexpr float MAX_DIST = 180.0f;           // Hard leash — teleport back if exceeded
    constexpr float TICK_INTERVAL = 0.04f;       // 25fps tick rate
    constexpr float MAX_TURN_PER_TICK = 20.0f;   // Degrees per tick
    constexpr float MIN_HEIGHT = 100.0f;         // Above owner
    constexpr float MAX_HEIGHT = 200.0f;         // Above owner

    // ── Teleport pet to owner if too far (login, teleport, initial spawn at origin) ──
    float petDistX = m_pet.pos.x - m_pos.x;
    float petDistZ = m_pet.pos.z - m_pos.z;
    float petDistSq = petDistX * petDistX + petDistZ * petDistZ;
    if (petDistSq > MAX_DIST * MAX_DIST) {
      m_pet.pos = m_pos + glm::vec3(
          (float)(rand() % 100 - 50), MIN_HEIGHT + (float)(rand() % 50),
          (float)(rand() % 100 - 50));
      m_pet.lastOwnerPos = m_pos;
      m_pet.dirAngle = glm::radians((float)(rand() % 360));
      m_pet.speed = 0.5f;
      m_pet.heightVel = 0.0f;
      m_pet.tickAccum = 0.0f;
      m_pet.followDelay = 0.0f;
      m_pet.wasOwnerMoving = false;
    }

    // ── Detect if owner is moving ──
    glm::vec3 ownerDelta = m_pos - m_pet.lastOwnerPos;
    float ownerMoveSq = ownerDelta.x * ownerDelta.x + ownerDelta.z * ownerDelta.z;
    bool ownerMoving = ownerMoveSq > 0.5f; // Threshold to avoid float noise
    m_pet.lastOwnerPos = m_pos;

    // Accumulate time for tick-based logic
    m_pet.tickAccum += deltaTime;

    // Follow ramp: when character just started moving, angel lerp starts slow and accelerates
    if (ownerMoving && !m_pet.wasOwnerMoving) {
      m_pet.followDelay = 0.0f; // Reset ramp timer on movement start
    }
    // When character just stopped, seed idle wander from current position
    if (!ownerMoving && m_pet.wasOwnerMoving) {
      float edx = m_pet.pos.x - m_pos.x;
      float edz = m_pet.pos.z - m_pos.z;
      m_pet.dirAngle = atan2f(edz, edx); // Continue moving in current direction
      m_pet.speed = 0.3f;                // Slow drift, not a snap
      m_pet.heightVel *= 0.5f;           // Dampen vertical momentum
      m_pet.tickAccum = 0.0f;            // Prevent burst of accumulated ticks
    }
    if (ownerMoving) {
      m_pet.followDelay += deltaTime; // Ramps up from 0
    } else {
      m_pet.followDelay = 0.0f;
    }
    m_pet.wasOwnerMoving = ownerMoving;

    if (ownerMoving) {
      // ── MOVING: angel trails behind character, facing same direction ──
      constexpr float TRAIL_DIST = 60.0f;
      constexpr float RAMP_DURATION = 0.5f; // Time to reach full follow speed
      float behindX = m_pos.x - cosf(m_facing) * TRAIL_DIST;
      float behindZ = m_pos.z - sinf(m_facing) * TRAIL_DIST;

      // Lateral weave: dirAngle is repurposed as smooth lateral offset during MOVING
      // Perpendicular to character facing — adds organic sway to follow path
      float perpX = -sinf(m_facing);
      float perpZ =  cosf(m_facing);
      behindX += perpX * m_pet.dirAngle;
      behindZ += perpZ * m_pet.dirAngle;

      // Lerp rate ramps from ~0.5 to 5.0 over RAMP_DURATION seconds
      float ramp = glm::clamp(m_pet.followDelay / RAMP_DURATION, 0.0f, 1.0f);
      float speed = 0.5f + ramp * 4.5f; // 0.5 → 5.0
      float lerpRate = glm::clamp(deltaTime * speed, 0.0f, 1.0f);
      m_pet.pos.x += (behindX - m_pet.pos.x) * lerpRate;
      m_pet.pos.z += (behindZ - m_pet.pos.z) * lerpRate;

      // Smoothly lerp facing toward character's direction (also ramps)
      float targetFacing = m_facing;
      float facingDiff = targetFacing - m_pet.facing;
      while (facingDiff > M_PI) facingDiff -= 2.0f * M_PI;
      while (facingDiff < -M_PI) facingDiff += 2.0f * M_PI;
      float facingSpeed = 1.0f + ramp * 3.0f; // 1.0 → 4.0
      m_pet.facing += facingDiff * glm::clamp(deltaTime * facingSpeed, 0.0f, 1.0f);
      constexpr float MOVE_HEAD_HEIGHT = 120.0f;
      float mdy = (m_pos.y + MOVE_HEAD_HEIGHT) - m_pet.pos.y;
      float mdx = m_pet.pos.x - m_pos.x;
      float mdz = m_pet.pos.z - m_pos.z;
      float mhDist = sqrtf(mdx * mdx + mdz * mdz);
      m_pet.pitch = atan2f(mdy, std::max(mhDist, 1.0f));

      // Vertical + lateral: smooth toward target height with gentle bobbing
      while (m_pet.tickAccum >= TICK_INTERVAL) {
        m_pet.tickAccum -= TICK_INTERVAL;
        m_pet.pos.y += m_pet.heightVel;
        m_pet.heightVel += ((float)(rand() % 10 - 5)) * 0.1f; // Gentle random nudge
        if (m_pet.pos.y < m_pos.y + MIN_HEIGHT) m_pet.heightVel += 1.0f;
        if (m_pet.pos.y > m_pos.y + MAX_HEIGHT) m_pet.heightVel -= 1.0f;
        m_pet.heightVel *= 0.92f;

        // Lateral weave: random drift ±25 units perpendicular to path
        m_pet.dirAngle += ((float)(rand() % 10 - 5)) * 0.4f;
        m_pet.dirAngle *= 0.93f; // Decay toward center

        // Sparkle — angel gets white dots, imp gets red-orange embers
        if (m_vfxManager && rand() % 4 == 0) {
          glm::vec3 sparkPos = m_pet.pos + glm::vec3(
              (float)(rand() % 16 - 8), (float)(rand() % 16 - 8), (float)(rand() % 16 - 8));
          auto sparkType = m_pet.itemIndex == 1 ? ParticleType::IMP_SPARKLE
                                                : ParticleType::PET_SPARKLE;
          m_vfxManager->SpawnBurst(sparkType, sparkPos, 1);
        }
      }
    } else {
      // ── IDLE: wander smoothly around owner, always face toward character ──
      while (m_pet.tickAccum >= TICK_INTERVAL) {
        m_pet.tickAccum -= TICK_INTERVAL;

        // Smooth turn toward target direction (max 8° per tick)
        constexpr float MAX_TURN = glm::radians(8.0f);
        float angleDiff = m_pet.dirAngle - atan2f(
            sinf(m_pet.dirAngle) * m_pet.speed,
            cosf(m_pet.dirAngle) * m_pet.speed);
        // dirAngle is the target — smoothly steer current movement
        float curMoveAngle = atan2f(sinf(m_pet.dirAngle), cosf(m_pet.dirAngle));

        // Apply smooth movement
        m_pet.pos.x += cosf(m_pet.dirAngle) * m_pet.speed;
        m_pet.pos.z += sinf(m_pet.dirAngle) * m_pet.speed;
        m_pet.pos.y += m_pet.heightVel;

        // Body exclusion: gently push pet away if too close (soft spring, no snap)
        constexpr float MIN_RADIUS = 40.0f;
        float edx = m_pet.pos.x - m_pos.x;
        float edz = m_pet.pos.z - m_pos.z;
        float eDist = sqrtf(edx * edx + edz * edz);
        if (eDist < MIN_RADIUS && eDist > 0.01f) {
          float pushStr = (MIN_RADIUS - eDist) * 0.15f; // Soft push
          m_pet.pos.x += (edx / eDist) * pushStr;
          m_pet.pos.z += (edz / eDist) * pushStr;
        } else if (eDist < 0.01f) {
          m_pet.pos.x += cosf(m_pet.dirAngle) * 0.5f;
          m_pet.pos.z += sinf(m_pet.dirAngle) * 0.5f;
        }

        // Gentle random direction drift: ~1.5% chance per tick, small angle change
        if (rand() % 64 == 0) {
          // New target direction: small random offset from current (±60°)
          float angleOffset = glm::radians((float)(rand() % 120 - 60));
          m_pet.dirAngle += angleOffset;
          m_pet.speed = 0.5f + (float)(rand() % 15) * 0.1f; // 0.5-2.0 units/tick
          m_pet.heightVel += ((float)(rand() % 20 - 10)) * 0.05f; // Gentle nudge
        }

        // Soft wander radius — pull back gradually, never snap
        edx = m_pet.pos.x - m_pos.x;
        edz = m_pet.pos.z - m_pos.z;
        float wanderDist = sqrtf(edx * edx + edz * edz);
        if (wanderDist > FLY_RANGE && wanderDist > 0.01f) {
          float overshoot = wanderDist - FLY_RANGE;
          float pullStr = std::min(overshoot * 0.1f, 2.0f); // Gradual pull
          m_pet.pos.x -= (edx / wanderDist) * pullStr;
          m_pet.pos.z -= (edz / wanderDist) * pullStr;
          // Steer direction back toward owner
          m_pet.dirAngle = atan2f(-edz, -edx) + glm::radians((float)(rand() % 60 - 30));
          m_pet.speed = std::min(m_pet.speed, 1.0f);
        }

        // Height constraints — gentle spring
        float targetY = m_pos.y + (MIN_HEIGHT + MAX_HEIGHT) * 0.5f; // Center of range
        float heightErr = targetY - m_pet.pos.y;
        m_pet.heightVel += heightErr * 0.02f; // Soft spring
        if (m_pet.pos.y < m_pos.y + MIN_HEIGHT) m_pet.heightVel += 0.5f;
        if (m_pet.pos.y > m_pos.y + MAX_HEIGHT) m_pet.heightVel -= 0.5f;
        m_pet.heightVel *= 0.92f; // Strong damping for smooth bobbing

        // Sparkle — angel gets white dots, imp gets red-orange embers
        if (m_vfxManager && rand() % 4 == 0) {
          glm::vec3 sparkPos = m_pet.pos + glm::vec3(
              (float)(rand() % 16 - 8), (float)(rand() % 16 - 8), (float)(rand() % 16 - 8));
          auto sparkType = m_pet.itemIndex == 1 ? ParticleType::IMP_SPARKLE
                                                : ParticleType::PET_SPARKLE;
          m_vfxManager->SpawnBurst(sparkType, sparkPos, 1);
        }
      }
      // Update facing AFTER movement so angel always looks at character head
      constexpr float HEAD_HEIGHT = 120.0f; // Approximate character head height
      float dx = m_pos.x - m_pet.pos.x;
      float dy = (m_pos.y + HEAD_HEIGHT) - m_pet.pos.y;
      float dz = m_pos.z - m_pet.pos.z;
      float hDist = sqrtf(dx * dx + dz * dz);
      float rawAngle = atan2f(dz, dx);
      float targetFacing = rawAngle + glm::half_pi<float>(); // +90° offset for Helper BMD front
      // Smoothly lerp facing toward target
      float facingDiff = targetFacing - m_pet.facing;
      while (facingDiff > M_PI) facingDiff -= 2.0f * M_PI;
      while (facingDiff < -M_PI) facingDiff += 2.0f * M_PI;
      m_pet.facing += facingDiff * glm::clamp(deltaTime * 3.0f, 0.0f, 1.0f);
      m_pet.pitch = atan2f(dy, std::max(hDist, 1.0f));
    }

    // ── Alpha: exponential smoothing (Main 5.2: Alpha += (AlphaTarget - Alpha) * 0.1f per tick) ──
    // Adapted for delta-time: alpha approaches 1.0 with ~10% convergence per tick
    if (m_pet.alpha < 0.99f) {
      float ticksThisFrame = deltaTime / TICK_INTERVAL;
      m_pet.alpha += (1.0f - m_pet.alpha) * (1.0f - powf(0.9f, ticksThisFrame));
      if (m_pet.alpha > 0.99f) m_pet.alpha = 1.0f;
    }

    // Advance wing flap animation — slow gentle flap (Main 5.2: helpers are graceful)
    int petNumKeys = 1;
    if (!m_pet.bmd->Actions.empty())
      petNumKeys = m_pet.bmd->Actions[0].NumAnimationKeys;
    if (petNumKeys > 1) {
      m_pet.animFrame += 14.0f * deltaTime; // Wing flap speed
      if (m_pet.animFrame >= (float)petNumKeys)
        m_pet.animFrame = std::fmod(m_pet.animFrame, (float)petNumKeys);
    }

    // Compute pet bones — blend toward bind pose to reduce leg/body shake
    // while preserving wing flap at full speed (14fps frequency, reduced amplitude)
    auto petBones = ComputeBoneMatricesInterpolated(m_pet.bmd.get(), 0,
                                                     m_pet.animFrame);
    auto bindBones = ComputeBoneMatricesInterpolated(m_pet.bmd.get(), 0, 0.0f);
    constexpr float ANIM_STRENGTH = 0.6f; // Keep 60% of animation motion
    for (size_t b = 0; b < petBones.size() && b < bindBones.size(); ++b) {
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
          petBones[b][r][c] = bindBones[b][r][c] +
              (petBones[b][r][c] - bindBones[b][r][c]) * ANIM_STRENGTH;
    }

    // Re-skin pet mesh vertices
    for (int mi = 0; mi < (int)m_pet.meshBuffers.size() &&
                     mi < (int)m_pet.bmd->Meshes.size(); ++mi) {
      RetransformMeshWithBones(m_pet.bmd->Meshes[mi], petBones,
                               m_pet.meshBuffers[mi]);
    }

    // Build pet model matrix: translate to pet.pos, scale 0.7 (Main 5.2: o->Scale = 0.7f)
    glm::mat4 petModel = glm::translate(glm::mat4(1.0f), m_pet.pos);
    petModel = glm::rotate(petModel, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    petModel = glm::rotate(petModel, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    petModel = glm::rotate(petModel, m_pet.facing, glm::vec3(0, 0, 1));
    petModel = glm::scale(petModel, glm::vec3(0.55f));

    m_shader->setMat4("model", petModel);
    m_shader->setFloat("objectAlpha", m_pet.alpha);
    m_shader->setFloat("blendMeshLight", 1.0f);

    // Self-illumination — brighter than surroundings for ethereal glow
    glm::vec3 petTLight = sampleTerrainLightAt(m_pet.pos);
    petTLight = glm::clamp(petTLight * 2.0f, 0.5f, 1.5f);
    m_shader->setVec3("terrainLight", petTLight);

    glDisable(GL_CULL_FACE); // Double-sided wing meshes

    // Render body mesh first (normal alpha blend), then wings (additive)
    // Main 5.2: BlendMesh compares mesh's Texture index, not mesh array index
    for (int mi = 0; mi < (int)m_pet.meshBuffers.size() &&
                     mi < (int)m_pet.bmd->Meshes.size(); ++mi) {
      auto &mb = m_pet.meshBuffers[mi];
      if (mb.indexCount == 0 || mb.hidden)
        continue;

      bool isBlendMesh = (m_pet.bmd->Meshes[mi].Texture == m_pet.blendMesh)
                         || mb.bright;
      if (isBlendMesh)
        continue;

      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }

    // Second pass: wing meshes — standard alpha blend with brightness boost
    // Additive (GL_ONE) washes out brights while leaving darks invisible;
    // standard blend gives consistent opacity from texture alpha.
    for (int mi = 0; mi < (int)m_pet.meshBuffers.size() &&
                     mi < (int)m_pet.bmd->Meshes.size(); ++mi) {
      auto &mb = m_pet.meshBuffers[mi];
      if (mb.indexCount == 0 || mb.hidden)
        continue;

      bool isBlendMesh = (m_pet.bmd->Meshes[mi].Texture == m_pet.blendMesh)
                         || mb.bright;
      if (!isBlendMesh)
        continue;

      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDepthMask(GL_FALSE);
      if (m_pet.itemIndex == 0) {
        // Angel: additive blend for ethereal transparent wings
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        m_shader->setFloat("blendMeshLight", m_pet.alpha);
      } else {
        // Imp: standard alpha blend with brightness boost
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_shader->setFloat("blendMeshLight", 1.5f * m_pet.alpha);
      }
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      glDepthMask(GL_TRUE);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      m_shader->setFloat("blendMeshLight", 1.0f);
    }

    glEnable(GL_CULL_FACE);

    // Restore shader state
    m_shader->setFloat("objectAlpha", 1.0f);
    m_shader->setVec3("terrainLight", tLight);
    glm::mat4 restoreModel = glm::translate(glm::mat4(1.0f), m_pos);
    restoreModel = glm::rotate(restoreModel, glm::radians(-90.0f),
                               glm::vec3(0, 0, 1));
    restoreModel = glm::rotate(restoreModel, glm::radians(-90.0f),
                               glm::vec3(0, 1, 0));
    restoreModel = glm::rotate(restoreModel, m_facing, glm::vec3(0, 0, 1));
    m_shader->setMat4("model", restoreModel);
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
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  // Stencil: draw each shadow pixel exactly once — body + weapon + shield
  // merge into one unified shadow silhouette.
  glEnable(GL_STENCIL_TEST);
  glStencilMask(0xFF);
  glClear(GL_STENCIL_BUFFER_BIT);
  glStencilFunc(GL_EQUAL, 0, 0xFF);
  glStencilOp(GL_KEEP, GL_INCR, GL_INCR);

  // Shadow projection constants (from ZzzBMD.cpp RenderBodyShadow)
  const float sx = 2000.0f;
  const float sy = 4000.0f;

  // Pre-compute facing rotation in MU-local space (around MU Z = height axis)
  float cosF = cosf(m_facing);
  float sinF = sinf(m_facing);

  auto renderShadowBatch = [&](const BMDData *bmd,
                               std::vector<ShadowMesh> &shadowMeshes,
                               int attachBone = -1,
                               const std::vector<BoneWorldMatrix> *weaponFinalBones = nullptr) {
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
      if (!weaponFinalBones && attachBone >= 0 && attachBone < (int)m_cachedBones.size()) {
        boneMatrix = (const float(*)[4])m_cachedBones[attachBone].data();
      }

      for (int i = 0; i < mesh.NumTriangles; ++i) {
        auto &tri = mesh.Triangles[i];
        int steps = (tri.Polygon == 3) ? 3 : 4;
        for (int v = 0; v < 3; ++v) {
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 pos = srcVert.Position;

          if (weaponFinalBones) {
            // Weapon/Shield: per-vertex bone from precomputed final bones
            int boneIdx = srcVert.Node;
            if (boneIdx >= 0 && boneIdx < (int)weaponFinalBones->size())
              pos = MuMath::TransformPoint(
                  (const float(*)[4])(*weaponFinalBones)[boneIdx].data(), pos);
          } else if (boneMatrix) {
            // Single attach bone (legacy path)
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

            if (weaponFinalBones) {
              int boneIdx = srcVert.Node;
              if (boneIdx >= 0 && boneIdx < (int)weaponFinalBones->size())
                pos = MuMath::TransformPoint(
                    (const float(*)[4])(*weaponFinalBones)[boneIdx].data(), pos);
            } else if (boneMatrix) {
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
  // Base head shadow (accessory helms)
  if (m_showBaseHead && m_baseHead.bmd) {
    renderShadowBatch(m_baseHead.bmd.get(), m_baseHead.shadowMeshes, -1);
  }

  // Weapons and shields — compute full bone matrices matching visible rendering
  // (parentMat * weaponLocalBones[i] for per-vertex skinning)
  static constexpr int SHADOW_BONE_BACK = 47;
  if (m_weaponBmd) {
    auto &wCat = GetWeaponCategoryRender(m_weaponInfo.category);
    int bone = (m_inSafeZone && SHADOW_BONE_BACK < (int)m_cachedBones.size())
                   ? SHADOW_BONE_BACK
                   : wCat.attachBone;
    if (bone < (int)m_cachedBones.size()) {
      BoneWorldMatrix off =
          m_inSafeZone
              ? MuMath::BuildWeaponOffsetMatrix(glm::vec3(70.f, 0.f, 90.f),
                                                glm::vec3(-20.f, 5.f, 40.f))
              : MuMath::BuildWeaponOffsetMatrix(glm::vec3(0, 0, 0),
                                                glm::vec3(0, 0, 0));
      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms((const float(*)[4])m_cachedBones[bone].data(),
                               (const float(*)[4])off.data(),
                               (float(*)[4])parentMat.data());
      const auto &wLocalBones = m_weaponLocalBones;
      std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
      for (int bi = 0; bi < (int)wLocalBones.size(); ++bi)
        MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                 (const float(*)[4])wLocalBones[bi].data(),
                                 (float(*)[4])wFinalBones[bi].data());
      renderShadowBatch(m_weaponBmd.get(), m_weaponShadowMeshes, -1, &wFinalBones);
    }
  }
  if (m_shieldBmd) {
    int bone = (m_inSafeZone && SHADOW_BONE_BACK < (int)m_cachedBones.size())
                   ? SHADOW_BONE_BACK
                   : GetWeaponCategoryRender(6).attachBone;
    if (bone < (int)m_cachedBones.size()) {
      bool dw = m_inSafeZone && isDualWielding();
      BoneWorldMatrix off =
          m_inSafeZone
              ? (dw ? MuMath::BuildWeaponOffsetMatrix(
                          glm::vec3(-110.f, 180.f, 90.f),
                          glm::vec3(20.f, 15.f, 40.f))
                    : MuMath::BuildWeaponOffsetMatrix(
                          glm::vec3(70.f, 0.f, 90.f),
                          glm::vec3(-10.f, 0.f, 0.f)))
              : MuMath::BuildWeaponOffsetMatrix(glm::vec3(0, 0, 0),
                                                glm::vec3(0, 0, 0));
      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms((const float(*)[4])m_cachedBones[bone].data(),
                               (const float(*)[4])off.data(),
                               (float(*)[4])parentMat.data());
      const auto &sLocalBones = m_shieldLocalBones;
      std::vector<BoneWorldMatrix> sFinalBones(sLocalBones.size());
      for (int bi = 0; bi < (int)sLocalBones.size(); ++bi)
        MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                 (const float(*)[4])sLocalBones[bi].data(),
                                 (float(*)[4])sFinalBones[bi].data());
      renderShadowBatch(m_shieldBmd.get(), m_shieldShadowMeshes, -1, &sFinalBones);
    }
  }

  glBindVertexArray(0);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_DEPTH_TEST);
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

    // If currently on an unwalkable tile (e.g. snapped to chair), allow escape
    bool currentWalkable = isWalkableAt(m_pos.x, m_pos.z);

    // Wall sliding: try full move, then X-only, then Z-only
    // (Main 5.2 MapPath.cpp: direction fallback when diagonal is blocked)
    if (isWalkableAt(newPos.x, newPos.z)) {
      m_pos.x = newPos.x;
      m_pos.z = newPos.z;
    } else if (!currentWalkable) {
      // Stuck on unwalkable tile — force move toward target to escape
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
  if (m_sittingOrPosing)
    CancelSitPose();
  m_target = target;
  // Only reset walk animation if not already walking
  int walkAction = (isMountRiding() || (!m_inSafeZone && m_weaponBmd))
                       ? weaponWalkAction()
                       : ACTION_WALK_MALE;
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
  // Use weapon/mount-specific idle action
  if (isMountRiding() || (!m_inSafeZone && m_weaponBmd)) {
    SetAction(weaponIdleAction());
  } else {
    SetAction(ACTION_STOP_MALE);
  }
  m_animFrame = 0.0f;
}

void HeroCharacter::StartSitPose(bool isSit, float facingAngleDeg,
                                 bool alignToObject,
                                 const glm::vec3 &snapPos) {
  if (IsDead())
    return;
  SoundManager::Play(SOUND_DROP_ITEM01); // Main 5.2: PlayBuffer(SOUND_DROP_ITEM01)
  m_moving = false;
  CancelAttack();
  ClearPendingPickup();
  m_sittingOrPosing = true;

  // Snap character to the object's world position
  // Always snap — the object is the sit/pose target regardless of tile walkability
  m_pos = snapPos;
  m_target = snapPos;
  SnapToTerrain();

  if (alignToObject) {
    // Main 5.2: Object.Angle[2] is the raw MU Z rotation in degrees
    // Convert to our facing angle in radians
    m_facing = facingAngleDeg * (float)(M_PI / 180.0);
    m_targetFacing = m_facing;
  }

  if (isSit) {
    SetAction(ACTION_SIT1);
  } else {
    SetAction(ACTION_POSE1);
  }
  m_animFrame = 0.0f;
}

void HeroCharacter::CancelSitPose() {
  if (!m_sittingOrPosing)
    return;
  m_sittingOrPosing = false;
  // Return to idle
  if (isMountRiding() || (!m_inSafeZone && m_weaponBmd)) {
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
  // Main 5.2: mount stays loaded in safe zone, alpha fades to 0.
  // Player uses ground animations. Mount auto-restores when leaving.
  // isMountRiding() handles the safe zone check for animation selection.

  // Switch animation: isMountRiding() returns false in safe zone,
  // so weaponIdleAction/weaponWalkAction will return ground animations.
  if (m_moving) {
    SetAction((isMountRiding() || (!safe && m_weaponBmd)) ? weaponWalkAction()
                                                          : ACTION_WALK_MALE);
  } else {
    SetAction((isMountRiding() || (!safe && m_weaponBmd)) ? weaponIdleAction()
                                                          : ACTION_STOP_MALE);
  }

  std::cout << "[Hero] " << (safe ? "Entered SafeZone" : "Left SafeZone")
            << ", action=" << m_action
            << (m_mount.active ? " (mount fading)" : "") << std::endl;
}

void HeroCharacter::EquipWeapon(const WeaponEquipInfo &weapon) {
  // Cleanup old weapon
  CleanupMeshBuffers(m_weaponMeshBuffers);
  CleanupMeshBuffers(m_ghostWeaponMeshBuffers);
  for (auto &sm : m_weaponShadowMeshes) {
    if (sm.vao)
      glDeleteVertexArrays(1, &sm.vao);
    if (sm.vbo)
      glDeleteBuffers(1, &sm.vbo);
  }
  m_weaponShadowMeshes.clear();
  m_twistingSlashActive = false;

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

  // Ghost weapon mesh buffers for Twisting Slash VFX (static bind-pose copy)
  AABB ghostAABB{};
  for (auto &mesh : bmd->Meshes) {
    UploadMeshWithBones(mesh, m_dataPath + "/Item/", {}, m_ghostWeaponMeshBuffers,
                        ghostAABB, false);
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
  m_weaponLocalBones = ComputeBoneMatrices(m_weaponBmd.get());

  // Main 5.2: ItemLight — per-weapon BlendMesh glow assignment
  m_weaponBlendMesh = ItemModelManager::GetItemBlendMesh(
      weapon.category, weapon.itemIndex);

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
  if (!m_shieldBmd->Bones.empty()) {
    m_shieldLocalBones = ComputeBoneMatrices(m_shieldBmd.get());
  } else {
    BoneWorldMatrix identity{};
    identity[0] = {1, 0, 0, 0};
    identity[1] = {0, 1, 0, 0};
    identity[2] = {0, 0, 1, 0};
    m_shieldLocalBones = {identity};
  }

  std::cout << "[Hero] Loaded shield " << shield.modelFile << ": "
            << m_shieldBmd->Meshes.size() << " meshes, "
            << m_shieldBmd->Bones.size() << " bones" << std::endl;

  const auto &shieldBones = m_shieldLocalBones;

  CleanupMeshBuffers(m_shieldMeshBuffers);
  for (auto &mesh : m_shieldBmd->Meshes) {
    UploadMeshWithBones(mesh, texPath, shieldBones, m_shieldMeshBuffers,
                        shieldAABB, true);
  }

  std::cout << "[Hero] Shield equipped: " << shield.modelFile << " ("
            << m_shieldMeshBuffers.size() << " GPU meshes)" << std::endl;
}

// Main 5.2 ZzzCharacter.cpp:11718 — helm model indices that show the base head
// underneath (accessory helms that don't cover the full face).
// MODEL_HELM + index: 0=Bronze, 2=Pad, 10=Vine, 11=Silk, 12=Wind, 13=Spirit
static bool IsShowHeadHelm(const std::string &helmFile) {
  std::string lower = helmFile;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  // Male01=Bronze(idx0), Male03=Pad(idx2)
  if (lower.find("helmmale01") != std::string::npos) return true;
  if (lower.find("helmmale03") != std::string::npos) return true;
  // Elf01-Elf04 = Vine/Silk/Wind/Spirit (idx 10-13)
  if (lower.find("helmelf01") != std::string::npos) return true;
  if (lower.find("helmelf02") != std::string::npos) return true;
  if (lower.find("helmelf03") != std::string::npos) return true;
  if (lower.find("helmelf04") != std::string::npos) return true;
  return false;
}

void HeroCharacter::EquipBodyPart(int partIndex, const std::string &modelFile) {
  if (partIndex < 0 || partIndex >= PART_COUNT)
    return;

  // Default naked body parts for current class
  const char *suffix = GetClassBodySuffix(m_class);
  static const char *partPrefixes[] = {"Helm", "Armor", "Pant", "Glove", "Boot"};
  char defaultPart[64];
  snprintf(defaultPart, 64, "%s%s.bmd", partPrefixes[partIndex], suffix);

  std::string fileToLoad =
      modelFile.empty() ? defaultPart : modelFile;
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

  // For helms (partIndex 0): load base head model underneath accessory helms
  // Main 5.2 ZzzCharacter.cpp:11718 — certain helms show the face
  if (partIndex == 0) {
    // Cleanup old base head
    CleanupMeshBuffers(m_baseHead.meshBuffers);
    for (auto &sm : m_baseHead.shadowMeshes) {
      if (sm.vao) glDeleteVertexArrays(1, &sm.vao);
      if (sm.vbo) glDeleteBuffers(1, &sm.vbo);
    }
    m_baseHead.shadowMeshes.clear();
    m_baseHead.bmd.reset();
    m_showBaseHead = false;

    bool isDefault = modelFile.empty() || fileToLoad == std::string(defaultPart);
    if (!isDefault && IsShowHeadHelm(fileToLoad)) {
      // Load class default head (HelmClassXX.bmd) underneath
      std::string headPath = m_dataPath + "/Player/" + defaultPart;
      auto headBmd = BMDParser::Parse(headPath);
      if (headBmd) {
        AABB headAABB{};
        for (auto &mesh : headBmd->Meshes) {
          UploadMeshWithBones(mesh, m_dataPath + "/Player/", bones,
                              m_baseHead.meshBuffers, headAABB, true);
        }
        m_baseHead.shadowMeshes = createShadowMeshes(headBmd.get());
        m_baseHead.bmd = std::move(headBmd);
        m_showBaseHead = true;
        std::cout << "[Hero] Base head loaded: " << defaultPart << std::endl;
      }
    }
  }

  std::cout << "[Hero] Equipped body part[" << partIndex << "]: " << fileToLoad
            << " (" << m_parts[partIndex].meshBuffers.size() << " GPU meshes)"
            << std::endl;
}

void HeroCharacter::AttackMonster(int monsterIndex,
                                  const glm::vec3 &monsterPos) {
  if (IsDead())
    return;
  if (m_sittingOrPosing)
    CancelSitPose();
  if (m_globalAttackCooldown > 0.0f)
    return; // Still on cooldown from cancelled attack

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
    int act = nextAttackAction();
    SetAction(act);
    // Main 5.2: weapon-type-specific swing sound (ZzzCharacter.cpp:1199-1204)
    // Light Saber (sword 10) + spears (cat 3) → eSwingLightSword; others → random
    if (HasWeapon()) {
      if (m_weaponInfo.category == 3 ||
          (m_weaponInfo.category == 0 && m_weaponInfo.itemIndex == 10))
        SoundManager::Play(SOUND_SWING_LIGHT);
      else
        SoundManager::Play(SOUND_SWING1 + rand() % 2);
    }

    // Normal melee: weapon blur trail (Main 5.2: BlurType 1, BlurMapping 0)
    // BlurMapping 0 = blur01.OZJ texture, level-based color
    if (HasWeapon() && m_vfxManager) {
      glm::vec3 trailColor(0.5f, 0.5f, 0.5f); // Default gray
      uint8_t wlvl = m_weaponInfo.itemLevel;
      if (wlvl >= 7)
        trailColor = glm::vec3(1.0f, 0.6f, 0.2f);  // Orange
      else if (wlvl >= 5)
        trailColor = glm::vec3(0.2f, 0.4f, 1.0f);   // Blue
      else if (wlvl >= 3)
        trailColor = glm::vec3(1.0f, 0.2f, 0.2f);   // Red
      m_weaponTrailActive = true;
      m_vfxManager->StartWeaponTrail(trailColor, false);
    }

    // Set GCD = full attack cycle (animation + cooldown)
    int nk = (act >= 0 && act < (int)m_skeleton->Actions.size())
                 ? m_skeleton->Actions[act].NumAnimationKeys : 1;
    float spd = ANIM_SPEED * attackSpeedMultiplier();
    float animDur = (nk > 1) ? (float)nk / spd : 0.5f;
    float cd = ATTACK_COOLDOWN_TIME / attackSpeedMultiplier();
    m_globalAttackCooldown = animDur + cd;
    m_globalAttackCooldownMax = m_globalAttackCooldown;
  } else {
    // Out of range — walk toward target
    m_attackState = AttackState::APPROACHING;
    MoveTo(monsterPos);
  }
}

void HeroCharacter::UpdateAttack(float deltaTime) {
  // Update Twisting Slash ghost weapon effect
  UpdateTwistingSlash(deltaTime);

  // Tick global cooldown (persists after cancel to prevent exploit)
  if (m_globalAttackCooldown > 0.0f) {
    m_globalAttackCooldown -= deltaTime;
    if (m_globalAttackCooldown < 0.0f)
      m_globalAttackCooldown = 0.0f;
  }

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

      // Face the target — snap for directional VFX (Aqua Beam)
      m_targetFacing = atan2f(dir.z, -dir.x);
      m_facing = m_targetFacing;

      // Skill or weapon-type-specific attack animation
      if (m_activeSkillId > 0) {
        SetAction(GetSkillAction(m_activeSkillId));
        // Play skill sound on approach-to-swing transition (same as in-range path)
        switch (m_activeSkillId) {
        case 19: SoundManager::Play(SOUND_KNIGHT_SKILL1); break;
        case 20: SoundManager::Play(SOUND_KNIGHT_SKILL2); break;
        case 21: SoundManager::Play(SOUND_KNIGHT_SKILL3); break;
        case 22: SoundManager::Play(SOUND_KNIGHT_SKILL4); break;
        case 23: SoundManager::Play(SOUND_KNIGHT_SKILL4); break;
        case 41: SoundManager::Play(SOUND_KNIGHT_SKILL4); break; // Twisting Slash
        case 42: SoundManager::Play(SOUND_RAGE_BLOW1); break;
        case 43: SoundManager::Play(SOUND_KNIGHT_SKILL2); break;
        default:
          if (HasWeapon())
            SoundManager::Play(SOUND_SWING1 + rand() % 2);
          break;
        }
        if (m_vfxManager) {
          m_vfxManager->SpawnSkillCast(m_activeSkillId, m_pos, m_facing, m_attackTargetPos);
          // Twisting Slash: spawn ghost weapon orbit on approach arrival
          if (m_activeSkillId == 41)
            StartTwistingSlash();
          // Spell VFX dispatch (same as SkillAttackMonster in-range path)
          switch (m_activeSkillId) {
          case 17: // Energy Ball
          case 4:  // Fire Ball
            m_vfxManager->SpawnSpellProjectile(m_activeSkillId, m_pos,
                                               m_attackTargetPos);
            break;
          case 1: // Poison — Main 5.2: MODEL_POISON cloud + 10 smoke at target
            m_vfxManager->SpawnPoisonCloud(m_attackTargetPos);
            break;
          case 7: // Ice — MODEL_ICE crystal + 5x MODEL_ICE_SMALL debris
            m_vfxManager->SpawnIceStrike(m_attackTargetPos);
            break;
          case 2: // Meteorite — fireball falls from sky
            m_vfxManager->SpawnMeteorStrike(m_attackTargetPos);
            break;
          case 3: { // Lightning: AT_SKILL_THUNDER — ribbon beams from caster to target
            glm::vec3 castPos = m_pos + glm::vec3(0, 100, 0);
            glm::vec3 hitPos = m_attackTargetPos + glm::vec3(0, 50, 0);
            m_vfxManager->SpawnRibbon(castPos, hitPos, 50.0f,
                                      glm::vec3(0.4f, 0.6f, 1.0f), 0.5f);
            m_vfxManager->SpawnRibbon(castPos, hitPos, 10.0f,
                                      glm::vec3(0.6f, 0.8f, 1.0f), 0.5f);
            m_vfxManager->SpawnBurst(ParticleType::SPELL_LIGHTNING, hitPos, 15);
            SoundManager::Play(SOUND_THUNDER01); // Main 5.2: PlayBuffer(SOUND_THUNDER01)
            break;
          }
          case 13: // Cometfall: AT_SKILL_BLAST — sky-strike at target
            m_vfxManager->SpawnLightningStrike(m_attackTargetPos);
            break;
          case 5: // Flame — persistent ground fire at target
            m_vfxManager->SpawnFlameGround(m_attackTargetPos);
            break;
          case 8: // Twister — tornado travels toward target
            m_vfxManager->SpawnTwisterStorm(m_pos, m_attackTargetPos - m_pos);
            break;
          case 9: // Evil Spirit — 4-directional beams from caster
            m_vfxManager->SpawnEvilSpirit(m_pos, m_facing);
            break;
          case 10: // Hellfire — ground fire ring (beams at blast phase)
            m_vfxManager->SpawnHellfire(m_pos);
            break;
          case 12: // Aqua Beam — delayed: beam spawns at anim frame 5.5
            m_pendingAquaBeam = true;
            m_aquaBeamSpawned = false;
            m_aquaGatherTimer = 0.0f;
            break;
          }
        }
      } else {
        SetAction(nextAttackAction());
        // Normal attack swing sound on approach arrival
        if (HasWeapon()) {
          if (m_weaponInfo.category == 3 ||
              (m_weaponInfo.category == 0 && m_weaponInfo.itemIndex == 10))
            SoundManager::Play(SOUND_SWING_LIGHT);
          else
            SoundManager::Play(SOUND_SWING1 + rand() % 2);
        }
      }
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

    // Flash uses m_animFrame for swing-done check (animation slowdown desyncs timer)
    bool swingDone = (m_activeSkillId == 12)
        ? (m_animFrame >= (float)(numKeys - 1))
        : (m_attackAnimTimer >= animDuration);

    if (swingDone) {
      // Stop weapon blur trail on swing end
      if (m_weaponTrailActive && m_vfxManager) {
        m_weaponTrailActive = false;
        m_vfxManager->StopWeaponTrail();
      }
      // Swing finished — go to cooldown (also scaled by attack speed)
      m_attackState = AttackState::COOLDOWN;
      // Spells have shorter cooldown (0.2s base) for smoother casting flow
      float baseCooldown =
          (m_activeSkillId > 0) ? 0.2f : ATTACK_COOLDOWN_TIME;
      m_attackCooldown = baseCooldown / attackSpeedMultiplier();

      // If beam never spawned (animation too short), force spawn now
      if (m_pendingAquaBeam && !m_aquaBeamSpawned && m_vfxManager) {
        m_aquaBeamSpawned = true;
        m_vfxManager->SpawnAquaBeam(m_pos, m_facing);
        m_pendingAquaBeam = false;
      }

      // Kill beam VFX when animation ends — beam syncs with hands
      if (m_activeSkillId == 12 && m_vfxManager) {
        m_vfxManager->KillAquaBeams();
      }

      // Return to combat idle (weapon/mount stance or unarmed)
      SetAction((isMountRiding() || m_weaponBmd) ? weaponIdleAction()
                                                 : ACTION_STOP_MALE);
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
  // GCD already set when swing started — don't reduce it on cancel

  // Stop weapon blur trail if active
  if (m_weaponTrailActive && m_vfxManager) {
    m_weaponTrailActive = false;
    m_vfxManager->StopWeaponTrail();
  }

  m_attackState = AttackState::NONE;
  m_attackTargetMonster = -1;
  m_activeSkillId = 0;
  m_swordSwingCount = 0;
  m_moving = false; // Stop any approach movement
  m_pendingAquaBeam = false; // Clear pending beam on cancel
  m_aquaBeamSpawned = false;
  m_aquaPacketReady = false; // Don't send damage if cancelled
  // Return to appropriate idle
  if (isMountRiding() || (!m_inSafeZone && m_weaponBmd)) {
    SetAction(weaponIdleAction());
  } else {
    SetAction(ACTION_STOP_MALE);
  }
}

int HeroCharacter::GetSkillAction(uint8_t skillId) {
  switch (skillId) {
  // DK skills
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
  case 42:
    return ACTION_SKILL_FURY; // Rageful Blow
  case 43:
    return ACTION_SKILL_DEATH_STAB; // Death Stab
  // DW spells
  case 17:
    return ACTION_SKILL_HAND1; // Energy Ball
  case 4:
    // Fire Ball — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 1:
    // Poison — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 3:
    // Lightning — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 2:
    // Meteorite — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 7:
    // Ice — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 5:
    // Flame — Main 5.2: SetPlayerMagic() randomly picks HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 8:
    // Twister — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 6:
    return ACTION_SKILL_TELEPORT; // Teleport
  case 9:
    // Evil Spirit — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 12:
    return ACTION_SKILL_FLASH; // Aqua Beam
  case 10:
    return ACTION_SKILL_HELL; // Hellfire
  case 13:
    // Cometfall — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 14:
    return ACTION_SKILL_INFERNO; // Inferno (self-centered AoE)
  default:
    return ACTION_SKILL_SWORD1; // Fallback
  }
}

void HeroCharacter::SkillAttackMonster(int monsterIndex,
                                       const glm::vec3 &monsterPos,
                                       uint8_t skillId) {
  if (IsDead())
    return;
  if (m_sittingOrPosing)
    CancelSitPose();
  // Allow spell to interrupt normal melee attacks
  if (m_globalAttackCooldown > 0.0f) {
    if (m_activeSkillId > 0)
      return; // Block if GCD from a previous skill cast
    m_globalAttackCooldown = 0.0f; // Reset melee GCD
    CancelAttack();
  }

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
    m_facing = m_targetFacing; // Snap facing for directional VFX (Aqua Beam)

    SetAction(skillAction);
    // Skill-specific sounds (Main 5.2: PlaySkillSound)
    switch (skillId) {
    case 19: SoundManager::Play(SOUND_KNIGHT_SKILL1); break; // Falling Slash
    case 20: SoundManager::Play(SOUND_KNIGHT_SKILL2); break; // Lunge
    case 21: SoundManager::Play(SOUND_KNIGHT_SKILL3); break; // Uppercut
    case 22: SoundManager::Play(SOUND_KNIGHT_SKILL4); break; // Cyclone
    case 23: SoundManager::Play(SOUND_KNIGHT_SKILL4); break; // Slash (same as Cyclone)
    case 41: SoundManager::Play(SOUND_KNIGHT_SKILL4); break; // Twisting Slash
    case 42: SoundManager::Play(SOUND_RAGE_BLOW1); break;    // Rageful Blow
    case 43: SoundManager::Play(SOUND_KNIGHT_SKILL2); break; // Death Stab (same as Lunge)
    default:
      if (HasWeapon())
        SoundManager::Play(SOUND_SWING1 + rand() % 2);
      break;
    }

    // Set GCD = full attack cycle (animation + cooldown)
    int nk = (skillAction >= 0 && skillAction < (int)m_skeleton->Actions.size())
                 ? m_skeleton->Actions[skillAction].NumAnimationKeys : 1;
    float spd = ANIM_SPEED * attackSpeedMultiplier();
    float animDur = (nk > 1) ? (float)nk / spd : 0.5f;
    // Flash: animation slowdown during frames 1.0-3.0 adds ~2 frames at half speed
    if (skillId == 12)
      animDur += 2.0f / spd;
    float cd = 0.2f / attackSpeedMultiplier(); // Spell cooldown = 0.2s base
    m_globalAttackCooldown = animDur + cd;
    m_globalAttackCooldownMax = m_globalAttackCooldown;

    if (m_vfxManager) {
      m_vfxManager->SpawnSkillCast(skillId, m_pos, m_facing, monsterPos);

      // DK melee skills: weapon blur trail (Main 5.2: BlurType 1, BlurMapping 2)
      // BlurMapping 2 = motion_blur_r.OZJ texture, WHITE color (1,1,1)
      // Note: Twisting Slash (41) has NO weapon trail in Main 5.2
      if (skillId >= 19 && skillId <= 23) {
        if (HasWeapon()) {
          m_weaponTrailActive = true;
          m_vfxManager->StartWeaponTrail(glm::vec3(1.0f, 1.0f, 1.0f), true);
        }
      }
      // Twisting Slash: spawn ghost weapon orbit effect
      if (skillId == 41)
        StartTwistingSlash();

      // Spell VFX: dispatch by skill ID (not class — server authorizes skills)
      switch (skillId) {
      case 17: // Energy Ball: traveling BITMAP_ENERGY projectile
      case 4:  // Fire Ball: traveling MODEL_FIRE projectile
        m_vfxManager->SpawnSpellProjectile(skillId, m_pos, monsterPos);
        break;
      case 1: // Poison — Main 5.2: MODEL_POISON cloud + 10 smoke at target
        m_vfxManager->SpawnPoisonCloud(monsterPos);
        break;
      case 7: // Ice — MODEL_ICE crystal + 5x MODEL_ICE_SMALL debris
        m_vfxManager->SpawnIceStrike(monsterPos);
        break;
      case 2: // Meteorite — fireball falls from sky
        m_vfxManager->SpawnMeteorStrike(monsterPos);
        break;
      case 3: { // Lightning: AT_SKILL_THUNDER — ribbon beams from caster to target
        glm::vec3 castPos = m_pos + glm::vec3(0, 100, 0);
        glm::vec3 hitPos = monsterPos + glm::vec3(0, 50, 0);
        m_vfxManager->SpawnRibbon(castPos, hitPos, 50.0f,
                                  glm::vec3(0.4f, 0.6f, 1.0f), 0.5f);
        m_vfxManager->SpawnRibbon(castPos, hitPos, 10.0f,
                                  glm::vec3(0.6f, 0.8f, 1.0f), 0.5f);
        m_vfxManager->SpawnBurst(ParticleType::SPELL_LIGHTNING, hitPos, 15);
        SoundManager::Play(SOUND_THUNDER01); // Main 5.2: PlayBuffer(SOUND_THUNDER01)
        break;
      }
      case 13: // Cometfall: AT_SKILL_BLAST — sky-strike at target
        m_vfxManager->SpawnLightningStrike(monsterPos);
        break;
      case 5: // Flame — persistent ground fire at target
        m_vfxManager->SpawnFlameGround(monsterPos);
        break;
      case 8: // Twister — tornado travels toward target
        m_vfxManager->SpawnTwisterStorm(m_pos, monsterPos - m_pos);
        break;
      case 9: // Evil Spirit — 4-directional beams from caster
        m_vfxManager->SpawnEvilSpirit(m_pos, m_facing);
        break;
      case 10: // Hellfire — ground fire ring (beams at blast phase)
        m_vfxManager->SpawnHellfire(m_pos);
        break;
      case 12: // Aqua Beam — delayed: beam spawns at anim frame 5.5
        m_pendingAquaBeam = true;
        m_aquaBeamSpawned = false;
        m_aquaGatherTimer = 0.0f;
        break;
      case 14: // Inferno — ring of fire explosions around caster
        m_vfxManager->SpawnInferno(m_pos);
        break;
      }
    }
    std::cout << "[Skill] Started SWINGING with action " << skillAction
              << std::endl;
  } else {
    m_attackState = AttackState::APPROACHING;
    MoveTo(monsterPos);
    std::cout << "[Skill] APPROACHING target (too far)" << std::endl;
  }
}

void HeroCharacter::CastSelfAoE(uint8_t skillId, const glm::vec3 &targetPos) {
  if (IsDead())
    return;
  // Allow spell to interrupt normal melee attacks (reset GCD from auto-attack)
  if (m_globalAttackCooldown > 0.0f && m_activeSkillId > 0)
    return; // Only block if GCD is from a previous SKILL cast
  // Cancel any in-progress attack and reset melee GCD
  m_globalAttackCooldown = 0.0f;
  CancelAttack();
  m_moving = false;
  m_activeSkillId = skillId;
  m_attackTargetMonster = -1; // No specific target

  // Face toward target — snap immediately for directional spells (Aqua Beam)
  glm::vec3 dir = targetPos - m_pos;
  dir.y = 0.0f;
  if (glm::length(dir) > 0.01f) {
    m_targetFacing = atan2f(dir.z, -dir.x);
    m_facing = m_targetFacing;
  }

  int skillAction = GetSkillAction(skillId);

  // Play cast animation
  m_attackState = AttackState::SWINGING;
  m_attackAnimTimer = 0.0f;
  m_attackHitRegistered = true; // No hit to register (AoE handled by server)
  SetAction(skillAction);

  // Skill-specific sounds (Main 5.2: AttackStage)
  switch (skillId) {
  case 41: SoundManager::Play(SOUND_KNIGHT_SKILL4); break; // Twisting Slash
  case 42: SoundManager::Play(SOUND_RAGE_BLOW1); break;    // Rageful Blow
  default: break; // DW spells have no swing/cast sound
  }

  // Set GCD
  int nk = (skillAction >= 0 && skillAction < (int)m_skeleton->Actions.size())
               ? m_skeleton->Actions[skillAction].NumAnimationKeys : 1;
  float spd = ANIM_SPEED * attackSpeedMultiplier();
  float animDur = (nk > 1) ? (float)nk / spd : 0.5f;
  // Flash: animation slowdown during frames 1.0-3.0 adds ~2 frames at half speed
  if (skillId == 12)
    animDur += 2.0f / spd;
  float cd = 0.2f / attackSpeedMultiplier();
  m_globalAttackCooldown = animDur + cd;
  m_globalAttackCooldownMax = m_globalAttackCooldown;

  // Spawn VFX
  if (m_vfxManager) {
    m_vfxManager->SpawnSkillCast(skillId, m_pos, m_facing, targetPos);

    // Twisting Slash: ghost weapon orbit (Main 5.2: no weapon trail, just ghosts)
    if (skillId == 41 && HasWeapon())
      StartTwistingSlash();

    switch (skillId) {
    case 8: // Twister — tornado travels toward click direction
      m_vfxManager->SpawnTwisterStorm(m_pos, targetPos - m_pos);
      break;
    case 9: // Evil Spirit — 4-directional beams from caster
      m_vfxManager->SpawnEvilSpirit(m_pos, m_facing);
      break;
    case 10: // Hellfire — ground fire ring (beams spawned at blast phase)
      m_vfxManager->SpawnHellfire(m_pos);
      break;
    case 12: // Aqua Beam — delayed: beam spawns at anim frame 5.5
      m_pendingAquaBeam = true;
      m_aquaBeamSpawned = false;
      m_aquaGatherTimer = 0.0f;
      break;
    case 14: // Inferno — ring of fire explosions around caster
      m_vfxManager->SpawnInferno(m_pos);
      break;
    }
  }

  std::cout << "[Skill] CastSelfAoE: skillId=" << (int)skillId
            << " action=" << skillAction << std::endl;
}

void HeroCharacter::TeleportTo(const glm::vec3 &target) {
  if (IsDead())
    return;
  if (m_globalAttackCooldown > 0.0f)
    return;

  // Cancel any in-progress attack/movement
  CancelAttack();
  m_moving = false;

  // Main 5.2: teleport does NOT dismount. Mount persists, just teleports with player.

  // VFX: white rising sparks at origin and destination (Main 5.2)
  if (m_vfxManager) {
    m_vfxManager->SpawnSkillCast(6, m_pos, m_facing);
    m_vfxManager->SpawnSkillImpact(6, target);
  }

  // No animation change — instant teleport, keep current pose
  // Fixed 1.5s GCD to prevent spam
  m_globalAttackCooldown = 1.5f;
  m_globalAttackCooldownMax = m_globalAttackCooldown;

  // Instantly move to target position
  m_pos = target;
  m_target = target;
  SnapToTerrain();
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
  SoundManager::Play(SOUND_MALE_DIE);
  std::cout << "[Hero] Dying (Forced) — action=" << ACTION_DIE1
            << " numActions="
            << (m_skeleton ? (int)m_skeleton->Actions.size() : 0) << std::endl;
}

// ── Twisting Slash ghost weapon effect (Main 5.2: MODEL_SKILL_WHEEL) ──

void HeroCharacter::StartTwistingSlash() {
  if (m_ghostWeaponMeshBuffers.empty())
    return; // No weapon equipped
  m_twistingSlashActive = true;
  m_wheelSpawnTimer = 0.0f;
  m_wheelSpawnCount = 0;
  m_wheelSmokeTimer = 0.0f;
  for (int i = 0; i < MAX_WHEEL_GHOSTS; ++i)
    m_wheelGhosts[i].active = false;
}

void HeroCharacter::UpdateTwistingSlash(float dt) {
  if (!m_twistingSlashActive)
    return;

  // Main 5.2: WHEEL1 lives 5 ticks, spawns 1 WHEEL2 per tick with SubType=4-LifeTime
  // SubType 0 has no alpha case (first spawn), SubType 1-4 = 0.6, 0.5, 0.4, 0.3
  m_wheelSpawnTimer += dt;
  while (m_wheelSpawnTimer >= 0.04f && m_wheelSpawnCount < MAX_WHEEL_GHOSTS) {
    m_wheelSpawnTimer -= 0.04f;
    auto &g = m_wheelGhosts[m_wheelSpawnCount];
    g.active = true;
    // Main 5.2: all WHEEL2 inherit same initial angle from WHEEL1 (= player facing).
    // Natural 18° stagger emerges from time-staggered spawning + 450°/sec rotation.
    g.orbitAngle = 0.0f;
    g.spinAngle = 0.0f;
    g.spinVelocity = 0.0f;
    // Main 5.2: SubType = 4-LifeTime → spawns -1,0,1,2,3
    // SubType -1,0 have no alpha case → default 1.0 (fully opaque)
    // SubType 1=0.6, 2=0.5, 3=0.4
    static constexpr float alphas[5] = {1.0f, 1.0f, 0.6f, 0.5f, 0.4f};
    g.alpha = alphas[m_wheelSpawnCount];
    g.lifetime = 1.0f; // 25 ticks at 25fps
    m_wheelSpawnCount++;
  }

  // Update each active ghost
  bool anyActive = false;
  m_wheelSmokeTimer += dt;
  bool spawnParticles = m_wheelSmokeTimer >= 0.04f;
  for (int i = 0; i < MAX_WHEEL_GHOSTS; ++i) {
    auto &g = m_wheelGhosts[i];
    if (!g.active)
      continue;
    g.lifetime -= dt;
    if (g.lifetime <= 0.0f) {
      g.active = false;
      continue;
    }
    anyActive = true;
    // Main 5.2: orbital rotation = Angle[2] -= 18 per tick = 450°/sec
    g.orbitAngle -= 450.0f * dt;
    // Main 5.2: RenderWheelWeapon does Direction[2] -= 30 per render frame (cumulative)
    // This is acceleration: 30°/frame * 25fps = 750°/sec² acceleration
    g.spinVelocity -= 750.0f * dt;
    g.spinAngle += g.spinVelocity * dt;

    if (m_vfxManager && spawnParticles) {
      float orbitRad = glm::radians(g.orbitAngle);
      glm::vec3 ghostPos = m_pos + glm::vec3(sinf(orbitRad) * 150.0f, 100.0f,
                                               cosf(orbitRad) * 150.0f);
      // Main 5.2: CreateParticle(BITMAP_SMOKE) + 4x JOINT_SPARK + 1x SPARK
      // + CreateSprite(BITMAP_LIGHT) warm glow per tick per ghost
      m_vfxManager->SpawnBurst(ParticleType::SMOKE, ghostPos, 1);
      m_vfxManager->SpawnBurst(ParticleType::HIT_SPARK, ghostPos, 3);
      m_vfxManager->SpawnBurst(ParticleType::FLARE, ghostPos, 1);
    }
  }
  if (spawnParticles)
    m_wheelSmokeTimer -= 0.04f;

  if (!anyActive && m_wheelSpawnCount >= MAX_WHEEL_GHOSTS)
    m_twistingSlashActive = false;
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
      // Return to appropriate idle if not attacking/moving/sitting
      if (m_attackState == AttackState::NONE && !m_moving &&
          !m_sittingOrPosing) {
        if (isMountRiding() || (!m_inSafeZone && m_weaponBmd)) {
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
  if (isMountRiding() || (!m_inSafeZone && m_weaponBmd)) {
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
  // - Attack/skill -> combat idle (weapon attacks / spells finishing)
  // - Idle -> skill (entering spell cast animation)
  bool involvesFists =
      (m_action == ACTION_ATTACK_FIST || newAction == ACTION_ATTACK_FIST);

  // Detect walk actions (15-23) and stop/idle actions (0-10)
  bool isWalkingAction = (m_action >= 15 && m_action <= 23);
  bool isNewStop = (newAction >= 0 && newAction <= 10);
  bool isStopping = (isWalkingAction && isNewStop);

  // Attack/skill -> idle blend (weapon attacks 38-51, DK skills 60-74, magic 146-154)
  bool isAttackAction =
      (m_action >= 38 && m_action <= 51) || (m_action >= 60 && m_action <= 74) ||
      (m_action >= 146 && m_action <= 154);
  bool isAttackToIdle = (isAttackAction && isNewStop);

  // Idle -> skill/attack blend (smooth entry into spell cast animations)
  bool isCurrentStop = (m_action >= 0 && m_action <= 10);
  bool isNewSkill = (newAction >= 60 && newAction <= 74) ||
                    (newAction >= 146 && newAction <= 154);
  bool isIdleToSkill = (isCurrentStop && isNewSkill);

  // Mount transitions (mounting/dismounting — blend between normal and ride poses)
  bool isMountTransition =
      (newAction == ACTION_STOP_RIDE || newAction == ACTION_STOP_RIDE_WEAPON ||
       newAction == ACTION_RUN_RIDE || newAction == ACTION_RUN_RIDE_WEAPON ||
       m_action == ACTION_STOP_RIDE || m_action == ACTION_STOP_RIDE_WEAPON ||
       m_action == ACTION_RUN_RIDE || m_action == ACTION_RUN_RIDE_WEAPON);

  if (involvesFists || isStopping || isAttackToIdle || isIdleToSkill ||
      isMountTransition) {
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

void HeroCharacter::EquipPet(uint8_t itemIndex) {
  UnequipPet(); // Clear any existing pet

  // Helper01.bmd = Guardian Angel, Helper02.bmd = Imp
  std::string bmdFile = m_dataPath + "/Player/Helper0" +
                         std::to_string(itemIndex + 1) + ".bmd";
  auto bmd = BMDParser::Parse(bmdFile);
  if (!bmd) {
    std::cerr << "[Hero] Failed to load pet model: " << bmdFile << std::endl;
    return;
  }

  m_pet.itemIndex = itemIndex;

  // Main 5.2 GOBoid.cpp: BlendMesh=1 — mesh with Texture==1 renders additive
  // BlendMesh compares against the mesh's Texture INDEX, not the mesh array index
  m_pet.blendMesh = 1; // Standard for all helpers

  // Upload mesh buffers with per-mesh texture resolution
  // Helper BMDs are in Player/ but textures are in Item/
  AABB petAABB{};
  auto petBones = ComputeBoneMatrices(bmd.get());
  for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
    auto &mesh = bmd->Meshes[mi];

    // Try Player/ first, then Item/ fallback for this specific mesh's texture
    UploadMeshWithBones(mesh, m_dataPath + "/Player/", petBones,
                        m_pet.meshBuffers, petAABB, true);

    // Check if the just-uploaded mesh buffer got a valid texture
    auto &mb = m_pet.meshBuffers.back();
    if (mb.texture == 0) {
      // Fallback: resolve THIS mesh's texture from Item/ directory
      auto texInfo = TextureLoader::ResolveWithInfo(
          m_dataPath + "/Item/", mesh.TextureName);
      if (texInfo.textureID) {
        mb.texture = texInfo.textureID;
        std::cout << "[Hero] Pet mesh " << mi << ": texture '"
                  << mesh.TextureName << "' resolved from Item/" << std::endl;
      }
    }
  }

  m_pet.bmd = std::move(bmd);
  m_pet.active = true;
  m_pet.alpha = 0.0f; // Start transparent, exponential fade in
  m_pet.animFrame = 0.0f;
  m_pet.sparkTimer = 0.0f;
  // Direction-vector movement init (Main 5.2 GOBoid.cpp)
  // Main 5.2: spawn at random offset ±256 XY, +128-256 Z from owner
  m_pet.dirAngle = glm::radians((float)(rand() % 360));
  m_pet.speed = (16.0f + (float)(rand() % 64)) * 0.1f; // Initial idle speed
  m_pet.heightVel = ((float)(rand() % 64 - 32)) * 0.1f;
  m_pet.facing = m_pet.dirAngle;
  m_pet.tickAccum = 0.0f;
  m_pet.lastOwnerPos = m_pos;
  m_pet.pos = m_pos + glm::vec3(
      (float)(rand() % 200 - 100), 128.0f + (float)(rand() % 128),
      (float)(rand() % 200 - 100));

  std::cout << "[Hero] Pet companion equipped: Helper0"
            << (int)(itemIndex + 1) << ".bmd ("
            << m_pet.meshBuffers.size() << " meshes, blendMesh="
            << m_pet.blendMesh << ")" << std::endl;
}

void HeroCharacter::UnequipPet() {
  if (!m_pet.active && m_pet.meshBuffers.empty())
    return;
  CleanupMeshBuffers(m_pet.meshBuffers);
  m_pet.bmd.reset();
  m_pet.active = false;
  m_pet.alpha = 0.0f;
  std::cout << "[Hero] Pet companion unequipped" << std::endl;
}

void HeroCharacter::EquipMount(uint8_t itemIndex) {
  UnequipPet();    // Can't have pet + mount simultaneously
  UnequipMount();  // Clear previous mount

  // Main 5.2 ZzzOpenData.cpp: ride models are in Data/Skill/
  //   AccessModel(MODEL_UNICON,  "Data\\Skill\\", "Rider", 1)  → Rider01.bmd (Uniria)
  //   AccessModel(MODEL_PEGASUS, "Data\\Skill\\", "Rider", 2)  → Rider02.bmd (Dinorant)
  int riderIndex = (itemIndex == 2) ? 1 : 2;
  std::string bmdFile = m_dataPath + "/Skill/Rider0" +
                         std::to_string(riderIndex) + ".bmd";
  auto bmd = BMDParser::Parse(bmdFile);
  if (!bmd) {
    std::cerr << "[Hero] Failed to load mount model: " << bmdFile << std::endl;
    return;
  }

  m_mount.itemIndex = itemIndex;
  m_mount.blendMesh = -1;

  // Rider01.bmd (Uniria): bone 0 = Bip01 (animated root)
  // Rider02.bmd (Dinorant): bone 0 = Box01 (STATIC helper), bone 1 = Bip01 (animated root)
  // Find first bone named "Bip01" as the skeleton root for root motion removal.
  m_mount.rootBone = 0;
  for (int bi = 0; bi < (int)bmd->Bones.size(); ++bi) {
    if (std::strcmp(bmd->Bones[bi].Name, "Bip01") == 0) {
      m_mount.rootBone = bi;
      break;
    }
  }

  // Texture directory: Uniria textures in Item/, Dinorant textures in Skill/
  // Main 5.2: OpenTexture(MODEL_UNICON, "Item\\"), OpenTexture(MODEL_PEGASUS, "Skill\\")
  std::string texDir = m_dataPath + ((itemIndex == 2) ? "/Item/" : "/Skill/");

  AABB mountAABB{};
  auto mountBones = ComputeBoneMatrices(bmd.get());
  for (int mi = 0; mi < (int)bmd->Meshes.size(); ++mi) {
    auto &mesh = bmd->Meshes[mi];
    UploadMeshWithBones(mesh, texDir, mountBones,
                        m_mount.meshBuffers, mountAABB, true);

    auto &mb = m_mount.meshBuffers.back();
    if (mb.texture == 0) {
      // Fallback: try the other directory
      std::string fallbackDir = m_dataPath +
          ((itemIndex == 2) ? "/Skill/" : "/Item/");
      auto texInfo = TextureLoader::ResolveWithInfo(fallbackDir, mesh.TextureName);
      if (texInfo.textureID) {
        mb.texture = texInfo.textureID;
      }
    }
  }

  m_mount.bmd = std::move(bmd);
  m_mount.active = true;
  m_mount.alpha = 0.0f;
  m_mount.animFrame = 0.0f;
  m_mountEquippedIndex = itemIndex;

  // Switch player to riding animation
  if (m_moving) {
    SetAction(weaponWalkAction());
  } else {
    SetAction(weaponIdleAction());
  }

  std::cout << "[Hero] Mount equipped: Rider0" << riderIndex << ".bmd ("
            << m_mount.meshBuffers.size() << " meshes, "
            << m_mount.bmd->Bones.size() << " bones, "
            << m_mount.bmd->Actions.size() << " actions)" << std::endl;
}

void HeroCharacter::UnequipMount() {
  if (!m_mount.active && m_mount.meshBuffers.empty())
    return;
  CleanupMeshBuffers(m_mount.meshBuffers);
  m_mount.bmd.reset();
  m_mount.active = false;
  m_mount.alpha = 0.0f;

  // Switch back to normal animation
  if (m_moving) {
    SetAction(m_weaponBmd ? weaponWalkAction() : ACTION_WALK_MALE);
  } else {
    SetAction(m_weaponBmd ? weaponIdleAction() : ACTION_STOP_MALE);
  }
  std::cout << "[Hero] Mount unequipped" << std::endl;
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
  CleanupMeshBuffers(m_baseHead.meshBuffers);
  cleanupShadows(m_baseHead.shadowMeshes);
  m_baseHead.bmd.reset();

  CleanupMeshBuffers(m_weaponMeshBuffers);
  CleanupMeshBuffers(m_ghostWeaponMeshBuffers);
  cleanupShadows(m_weaponShadowMeshes);
  m_weaponBmd.reset();

  CleanupMeshBuffers(m_shieldMeshBuffers);
  cleanupShadows(m_shieldShadowMeshes);
  m_shieldBmd.reset();

  // Pet companion
  CleanupMeshBuffers(m_pet.meshBuffers);
  m_pet.bmd.reset();
  m_pet.active = false;

  m_shader.reset();
  m_shadowShader.reset();
  m_skeleton.reset();
}
