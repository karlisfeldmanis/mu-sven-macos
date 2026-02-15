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

  // Skeleton + body parts
  std::unique_ptr<BMDData> m_skeleton;
  static const int PART_COUNT = 5;
  struct BodyPart {
    std::unique_ptr<BMDData> bmd;
    std::vector<MeshBuffers> meshBuffers;
  };
  BodyPart m_parts[PART_COUNT];
  std::unique_ptr<Shader> m_shader;

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
  static const int MAX_POINT_LIGHTS = 64;
  float m_luminosity = 1.0f;
};

#endif // HERO_CHARACTER_HPP
