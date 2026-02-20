#ifndef NPC_MANAGER_HPP
#define NPC_MANAGER_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "Shader.hpp"
#include "TerrainParser.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ImDrawList;

// PointLight defined in HeroCharacter.hpp
#include "HeroCharacter.hpp"

// NPC spawn data received from server (0x13 viewport packet)
struct ServerNpcSpawn {
  uint16_t type;  // NPC type ID (253=Amy, 250=Merchant, etc.)
  uint8_t gridX;
  uint8_t gridY;
  uint8_t dir;    // 0-7 direction
};

// NPC interaction data (for mouse picking, name labels)
struct NpcInfo {
  glm::vec3 position;
  float radius;    // Cylindrical collision radius (~80 units)
  float height;    // Collision height (~200 units)
  std::string name;
  uint16_t type;
};

class NpcManager {
public:
  // Full init with hardcoded NPCs (fallback when no server)
  void Init(const std::string &dataPath);

  // Two-phase init for server mode:
  // 1) InitModels() loads BMD models + shaders only
  // 2) AddNpcByType() places individual NPCs from server data
  void InitModels(const std::string &dataPath);
  void AddNpcByType(uint16_t npcType, uint8_t gridX, uint8_t gridY, uint8_t dir);

  void Render(const glm::mat4 &view, const glm::mat4 &proj,
              const glm::vec3 &camPos, float deltaTime);
  void RenderShadows(const glm::mat4 &view, const glm::mat4 &proj);
  void RenderOutline(int npcIndex, const glm::mat4 &view, const glm::mat4 &proj);
  void RenderLabels(ImDrawList *dl, const glm::mat4 &view, const glm::mat4 &proj,
                    int winW, int winH, const glm::vec3 &camPos, int hoveredNpc);
  int PickLabel(float screenX, float screenY, const glm::mat4 &view,
                const glm::mat4 &proj, int winW, int winH,
                const glm::vec3 &camPos) const;
  void Cleanup();

  // Terrain linkage
  void SetTerrainData(const TerrainData *td) { m_terrainData = td; }
  void SetTerrainLightmap(const std::vector<glm::vec3> &lm) {
    m_terrainLightmap = lm;
  }
  void SetPointLights(const std::vector<PointLight> &lights) {
    m_pointLights = lights;
  }
  void SetLuminosity(float l) { m_luminosity = l; }
  int GetNpcCount() const { return (int)m_npcs.size(); }
  NpcInfo GetNpcInfo(int index) const;

private:
  // Shared model data (loaded once per NPC model type)
  struct NpcModel {
    std::string name;
    BMDData *skeleton;             // Non-owning, points into m_ownedBmds
    std::vector<BMDData *> parts;  // Non-owning body part BMDs
  };

  // Per-NPC instance
  struct NpcInstance {
    int modelIdx;       // Index into m_models
    glm::vec3 position;
    float facing;
    float animFrame = 0.0f;
    int action = 0;
    float scale = 1.0f;
    uint16_t npcType = 0;
    std::string name;

    // Per-instance mesh buffers (for CPU re-skinning)
    struct BodyPart {
      int bmdIdx; // Which part in NpcModel::parts (or -1 for skeleton meshes)
      std::vector<MeshBuffers> meshBuffers;
    };
    std::vector<BodyPart> bodyParts;

    // Shadow
    struct ShadowMesh {
      GLuint vao = 0, vbo = 0;
      int vertexCount = 0;
    };
    std::vector<ShadowMesh> shadowMeshes;
    std::vector<BoneWorldMatrix> cachedBones;
  };

  std::vector<std::unique_ptr<BMDData>> m_ownedBmds; // Owns all loaded BMDs
  std::vector<NpcModel> m_models;
  std::vector<NpcInstance> m_npcs;

  std::unique_ptr<Shader> m_shader;
  std::unique_ptr<Shader> m_shadowShader;

  // Path to NPC textures
  std::string m_npcTexPath;

  // External data
  const TerrainData *m_terrainData = nullptr;
  std::vector<glm::vec3> m_terrainLightmap;
  std::vector<PointLight> m_pointLights;
  static constexpr int MAX_POINT_LIGHTS = 64;
  float m_luminosity = 1.0f;

  // NPC type → model index mapping (for server-spawned NPCs)
  std::unordered_map<uint16_t, int> m_typeToModel;
  // NPC type → scale overrides
  std::unordered_map<uint16_t, float> m_typeScale;
  bool m_modelsLoaded = false;

  // Helpers
  int loadModel(const std::string &npcPath, const std::string &skeletonFile,
                const std::vector<std::string> &partFiles,
                const std::string &modelName);
  void addNpc(int modelIdx, int gridX, int gridY, int dir, float scale = 1.0f);
  float snapToTerrain(float worldX, float worldZ);
  glm::vec3 sampleTerrainLightAt(const glm::vec3 &worldPos) const;
  static constexpr float ANIM_SPEED = 4.0f;
};

#endif // NPC_MANAGER_HPP
