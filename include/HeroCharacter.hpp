#ifndef HERO_CHARACTER_HPP
#define HERO_CHARACTER_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "Shader.hpp"
#include "TerrainParser.hpp"
#include "ViewerCommon.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

struct PointLight {
  glm::vec3 position;
  glm::vec3 color;
  float range;
};

// Item identity received from server (equipment packet 0x24)
// The server only sends what the character has equipped — the client
// resolves all rendering config (animations, bones, rotation) locally.
struct WeaponEquipInfo {
  uint8_t category = 0xFF;  // ItemCategory (0xFF = none equipped)
  uint8_t itemIndex = 0;
  uint8_t itemLevel = 0;
  std::string modelFile;    // e.g. "Sword01.bmd"
};

// Client-side rendering config per weapon category
// Resolved locally from category — never stored in DB or sent over the wire
struct WeaponCategoryRender {
  uint8_t actionIdle;   // Player action for combat idle
  uint8_t actionWalk;   // Player action for combat walk
  uint8_t attachBone;   // Bone index (33=R Hand mount, 42=L Hand mount)
};

// Client-side weapon category rendering config table
// Reference: ZzzCharacter.cpp CreateCharacterPointer(), _enum.h player actions
// Identity rotation (0,0,0) + zero offset — weapon BMD's own bone handles orientation
inline const WeaponCategoryRender &GetWeaponCategoryRender(uint8_t category) {
  // Indexed by ItemCategory enum (0=Sword..6=Shield)
  static const WeaponCategoryRender table[] = {
    {4,  17, 33}, // SWORD:  PLAYER_STOP_SWORD / PLAYER_WALK_SWORD, R Hand
    {4,  17, 33}, // AXE:    same as sword
    {4,  17, 33}, // MACE:   same as sword
    {6,  19, 33}, // SPEAR:  PLAYER_STOP_SPEAR / PLAYER_WALK_SPEAR, R Hand
    {8,  20, 42}, // BOW:    PLAYER_STOP_BOW / PLAYER_WALK_BOW, L Hand
    {10, 22, 42}, // STAFF:  PLAYER_STOP_WAND / PLAYER_WALK_WAND, L Hand
    {4,  17, 42}, // SHIELD: PLAYER_STOP_SWORD / PLAYER_WALK_SWORD, L Hand
  };
  if (category < sizeof(table) / sizeof(table[0]))
    return table[category];
  static const WeaponCategoryRender fallback{1, 15, 33};
  return fallback;
}

enum class AttackState { NONE, APPROACHING, SWINGING, COOLDOWN };

class HeroCharacter {
public:
  void Init(const std::string &dataPath);
  void Render(const glm::mat4 &view, const glm::mat4 &proj,
              const glm::vec3 &camPos, float deltaTime);
  void RenderShadow(const glm::mat4 &view, const glm::mat4 &proj);
  void ProcessMovement(float deltaTime);
  void MoveTo(const glm::vec3 &target);
  void StopMoving();
  void Cleanup();

  // Weapon equipping (called after server sends equipment packet)
  void EquipWeapon(const WeaponEquipInfo &weapon);

  // Combat: attack a monster by index
  void AttackMonster(int monsterIndex, const glm::vec3 &monsterPos);
  void UpdateAttack(float deltaTime);
  bool CheckAttackHit();
  void CancelAttack();
  int RollDamage() const;
  int GetAttackTarget() const { return m_attackTargetMonster; }
  AttackState GetAttackState() const { return m_attackState; }
  bool IsAttacking() const { return m_attackState != AttackState::NONE; }

  // SafeZone state (called from main.cpp each frame)
  void SetInSafeZone(bool safe);
  bool IsInSafeZone() const { return m_inSafeZone; }
  bool HasWeapon() const { return m_weaponBmd != nullptr; }

  // Accessors
  glm::vec3 GetPosition() const { return m_pos; }
  void SetPosition(const glm::vec3 &pos) { m_pos = pos; }
  float GetFacing() const { return m_facing; }
  bool IsMoving() const { return m_moving; }
  Shader *GetShader() { return m_shader.get(); }

  // Terrain linkage
  void SetTerrainData(const TerrainData *td) { m_terrainData = td; }
  void SetTerrainLightmap(const std::vector<glm::vec3> &lightmap) {
    m_terrainLightmap = lightmap;
  }
  void SetPointLights(const std::vector<PointLight> &lights) {
    m_pointLights = lights;
  }
  void SetLuminosity(float l) { m_luminosity = l; }

  // Snap hero Y to terrain height
  void SnapToTerrain();

private:
  glm::vec3 sampleTerrainLightAt(const glm::vec3 &worldPos) const;

  // Position & movement
  glm::vec3 m_pos{12800.0f, 0.0f, 12800.0f};
  glm::vec3 m_target{12800.0f, 0.0f, 12800.0f};
  float m_facing = 0.0f;
  float m_speed = 334.0f;
  bool m_moving = false;

  // Animation
  int m_action = 1; // PLAYER_STOP1 (male idle)
  float m_animFrame = 0.0f;
  static constexpr float ANIM_SPEED = 8.25f;
  int m_rootBone = -1;

  // Default player actions (no weapon / SafeZone)
  static constexpr int ACTION_STOP_MALE = 1;  // PLAYER_STOP_MALE
  static constexpr int ACTION_WALK_MALE = 15; // PLAYER_WALK_MALE

  // Attack actions (sword right hand)
  static constexpr int ACTION_ATTACK_SWORD_R1 = 39;
  static constexpr int ACTION_ATTACK_SWORD_R2 = 40;

  // Combat state
  bool m_inSafeZone = true;    // Start in SafeZone (Lorencia town)
  WeaponEquipInfo m_weaponInfo; // Current weapon config (from server)

  // Attack state machine
  AttackState m_attackState = AttackState::NONE;
  int m_attackTargetMonster = -1;
  glm::vec3 m_attackTargetPos{0.0f};
  float m_attackAnimTimer = 0.0f;
  bool m_attackHitRegistered = false;
  int m_swordSwingCount = 0;
  float m_attackCooldown = 0.0f;
  static constexpr float ATTACK_RANGE = 150.0f;
  static constexpr float ATTACK_COOLDOWN_TIME = 0.6f;
  static constexpr float ATTACK_HIT_FRACTION = 0.4f; // Hit at 40% through anim
  int m_damageMin = 1;
  int m_damageMax = 6;

  // Skeleton + body parts
  std::unique_ptr<BMDData> m_skeleton;
  static const int PART_COUNT = 5;
  struct BodyPart {
    std::unique_ptr<BMDData> bmd;
    std::vector<MeshBuffers> meshBuffers;
  };
  BodyPart m_parts[PART_COUNT];
  std::unique_ptr<Shader> m_shader;

  // Weapon (attached item model)
  std::unique_ptr<BMDData> m_weaponBmd;
  std::vector<MeshBuffers> m_weaponMeshBuffers;
  std::string m_dataPath; // Cached for late weapon loading

  // Shadow rendering
  std::unique_ptr<Shader> m_shadowShader;
  struct ShadowMesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    int indexCount = 0;
    int vertexCount = 0;
  };
  std::vector<ShadowMesh> m_shadowMeshes; // one per body part mesh
  std::vector<BoneWorldMatrix> m_cachedBones; // cached from last Render()

  // External data (non-owning)
  const TerrainData *m_terrainData = nullptr;
  std::vector<glm::vec3> m_terrainLightmap;
  std::vector<PointLight> m_pointLights;
  static constexpr int MAX_POINT_LIGHTS = 64;
  float m_luminosity = 1.0f;
};

#endif // HERO_CHARACTER_HPP
