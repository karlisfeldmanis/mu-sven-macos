#ifndef MONSTER_MANAGER_HPP
#define MONSTER_MANAGER_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "Shader.hpp"
#include "TerrainParser.hpp"
#include "HeroCharacter.hpp" // For PointLight
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class MonsterState {
  IDLE,
  WALKING,
  HIT,
  DYING,
  DEAD
};

struct ServerMonsterSpawn {
  uint16_t monsterType;
  uint8_t gridX;
  uint8_t gridY;
  uint8_t dir;
};

struct MonsterInfo {
  glm::vec3 position;
  float radius;
  float height;
  std::string name;
  uint16_t type;
  int hp;
  int maxHp;
  MonsterState state;
};

class MonsterManager {
public:
  void InitModels(const std::string &dataPath);
  void AddMonster(uint16_t monsterType, uint8_t gridX, uint8_t gridY,
                  uint8_t dir);

  void Update(float deltaTime);
  void Render(const glm::mat4 &view, const glm::mat4 &proj,
              const glm::vec3 &camPos, float deltaTime);
  void RenderShadows(const glm::mat4 &view, const glm::mat4 &proj);
  void RenderOutline(int monsterIndex, const glm::mat4 &view,
                     const glm::mat4 &proj);
  void Cleanup();

  bool DealDamage(int monsterIndex, int damage);
  int GetMonsterCount() const { return (int)m_monsters.size(); }
  MonsterInfo GetMonsterInfo(int index) const;

  void SetTerrainData(const TerrainData *td) { m_terrainData = td; }
  void SetTerrainLightmap(const std::vector<glm::vec3> &lm) {
    m_terrainLightmap = lm;
  }
  void SetPointLights(const std::vector<PointLight> &lights) {
    m_pointLights = lights;
  }
  void SetLuminosity(float l) { m_luminosity = l; }

private:
  struct MonsterModel {
    std::string name;
    BMDData *bmd;
    float scale = 1.0f;
    float collisionRadius = 60.0f;
    float collisionHeight = 80.0f;
  };

  struct MonsterInstance {
    int modelIdx;
    glm::vec3 position;
    glm::vec3 spawnPosition;
    float facing;
    float animFrame = 0.0f;
    int action = 0;
    float scale = 1.0f;
    uint16_t monsterType = 0;
    std::string name;

    MonsterState state = MonsterState::IDLE;
    float stateTimer = 0.0f;
    float idleWanderTimer = 0.0f;
    glm::vec3 wanderTarget;

    int hp = 30;
    int maxHp = 30;
    float corpseTimer = 0.0f;
    float corpseAlpha = 1.0f;

    std::vector<MeshBuffers> meshBuffers;
    struct ShadowMesh {
      GLuint vao = 0, vbo = 0;
      int vertexCount = 0;
    };
    std::vector<ShadowMesh> shadowMeshes;
    std::vector<BoneWorldMatrix> cachedBones;
  };

  std::vector<std::unique_ptr<BMDData>> m_ownedBmds;
  std::vector<MonsterModel> m_models;
  std::vector<MonsterInstance> m_monsters;

  std::unique_ptr<Shader> m_shader;
  std::unique_ptr<Shader> m_shadowShader;

  std::string m_monsterTexPath;
  const TerrainData *m_terrainData = nullptr;
  std::vector<glm::vec3> m_terrainLightmap;
  std::vector<PointLight> m_pointLights;
  static constexpr int MAX_POINT_LIGHTS = 64;
  float m_luminosity = 1.0f;

  std::unordered_map<uint16_t, int> m_typeToModel;
  bool m_modelsLoaded = false;

  // Monster action constants (_define.h)
  static constexpr int ACTION_STOP1 = 0;
  static constexpr int ACTION_WALK = 2;
  static constexpr int ACTION_ATTACK1 = 3;
  static constexpr int ACTION_SHOCK = 5;
  static constexpr int ACTION_DIE = 6;

  static constexpr float ANIM_SPEED = 4.0f;
  static constexpr float CORPSE_FADE_TIME = 3.0f;
  static constexpr float RESPAWN_TIME = 10.0f;
  static constexpr float WANDER_RADIUS = 300.0f;
  static constexpr float WANDER_SPEED = 100.0f;

  int loadMonsterModel(const std::string &bmdFile, const std::string &name,
                       float scale, float radius, float height);
  float snapToTerrain(float worldX, float worldZ);
  glm::vec3 sampleTerrainLightAt(const glm::vec3 &worldPos) const;
  void updateStateMachine(MonsterInstance &mon, float dt);
  void setAction(MonsterInstance &mon, int action);
};

#endif // MONSTER_MANAGER_HPP
