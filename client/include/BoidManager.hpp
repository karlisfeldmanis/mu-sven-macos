#ifndef BOID_MANAGER_HPP
#define BOID_MANAGER_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "HeroCharacter.hpp" // For PointLight
#include "MeshBuffers.hpp"
#include "Shader.hpp"
#include "TerrainParser.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

// Boid AI states (Main 5.2: GOBoid.cpp lines 27-30)
enum class BoidAI : uint8_t {
  FLY = 0,    // Soaring at altitude
  DOWN = 1,   // Descending toward ground
  GROUND = 2, // Resting on terrain
  UP = 3      // Ascending back to altitude
};

// A single ambient boid (bird/bat/butterfly/crow)
struct Boid {
  bool live = false;
  glm::vec3 position{0.0f};
  glm::vec3 angle{0.0f};     // MU-space Euler angles (degrees)
  glm::vec3 direction{0.0f}; // Movement direction / look-ahead
  float velocity = 1.0f;
  float alpha = 0.0f;
  float alphaTarget = 1.0f;
  float timer = 0.0f;
  float scale = 0.8f;
  float shadowScale = 10.0f;
  float animFrame = 0.0f;
  float priorAnimFrame = 0.0f;
  int action = 0;
  BoidAI ai = BoidAI::FLY;
  float gravity = 8.0f; // Max turn rate for flocking (degrees/tick)
  int subType = 0;      // Bounce counter for despawn
  int lifetime = 0;
  float respawnDelay = 0.0f; // Cooldown before respawn (seconds)
};

// A single ambient fish (Lorencia water tiles)
struct Fish {
  bool live = false;
  glm::vec3 position{0.0f};
  glm::vec3 angle{0.0f};
  float velocity = 0.6f;
  float alpha = 0.0f;
  float alphaTarget = 0.3f;
  float scale = 0.5f;
  float animFrame = 0.0f;
  float priorAnimFrame = 0.0f;
  int action = 0;
  int subType = 0; // Wall-hit counter
  int lifetime = 0;
};

// A falling leaf particle (Main 5.2: ZzzEffectFireLeave.cpp)
struct LeafParticle {
  bool live = false;
  glm::vec3 position{0.0f};
  glm::vec3 velocity{0.0f};
  glm::vec3 angle{0.0f};        // Euler rotation (degrees)
  glm::vec3 turningForce{0.0f}; // Angular velocity (degrees/tick)
  float alpha = 1.0f;
  bool onGround = false;
};

class BoidManager {
public:
  void Init(const std::string &dataPath);
  void Update(float deltaTime, const glm::vec3 &heroPos, int heroAction,
              float worldTime);
  void Render(const glm::mat4 &view, const glm::mat4 &proj,
              const glm::vec3 &camPos);
  void RenderShadows(const glm::mat4 &view, const glm::mat4 &proj);
  void RenderLeaves(const glm::mat4 &view, const glm::mat4 &proj);
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

private:
  static constexpr int MAX_BOIDS = 2;  // Lorencia bird count (reduced)
  static constexpr int MAX_FISHS = 3;  // Lorencia fish count (GOBoid.cpp:1661)
  static constexpr int MAX_LEAVES = 80; // Lorencia leaf count (ZzzEffectFireLeave.cpp)
  static constexpr int MAX_POINT_LIGHTS = 64;

  Boid m_boids[MAX_BOIDS];
  Fish m_fishs[MAX_FISHS];
  LeafParticle m_leaves[MAX_LEAVES];

  // Bird model
  std::unique_ptr<BMDData> m_birdBmd;
  std::vector<MeshBuffers> m_birdMeshes;
  std::vector<BoneWorldMatrix> m_birdBones;

  // Fish model
  std::unique_ptr<BMDData> m_fishBmd;
  std::vector<MeshBuffers> m_fishMeshes;
  std::vector<BoneWorldMatrix> m_fishBones;

  // Shadow mesh buffers (one per boid for bird, one per fish)
  struct ShadowMesh {
    GLuint vao = 0, vbo = 0;
    int vertexCount = 0;
  };
  ShadowMesh m_birdShadow;
  ShadowMesh m_fishShadow;

  std::unique_ptr<Shader> m_shader;
  std::unique_ptr<Shader> m_shadowShader;

  const TerrainData *m_terrainData = nullptr;
  std::vector<glm::vec3> m_terrainLightmap;
  std::vector<PointLight> m_pointLights;
  float m_luminosity = 1.0f;

  // World time accumulator (ticks at 25fps equivalent)
  float m_worldTime = 0.0f;

  // Helpers
  float getTerrainHeight(float worldX, float worldZ) const;
  glm::vec3 sampleTerrainLight(const glm::vec3 &pos) const;
  uint8_t getTerrainLayer1(float worldX, float worldZ) const;
  uint8_t getTerrainAttribute(float worldX, float worldZ) const;

  void updateBoids(float dt, const glm::vec3 &heroPos, int heroAction);
  void updateFishs(float dt, const glm::vec3 &heroPos);
  void moveBird(Boid &b, const glm::vec3 &heroPos, int heroAction);
  void moveBoidGroup(Boid &b);
  void moveBoidFlock(Boid &b, int selfIdx);
  void alphaFade(float &alpha, float target, float dt);

  void renderBoid(const Boid &b, const glm::mat4 &view, const glm::mat4 &proj);
  void renderFish(const Fish &f, const glm::mat4 &view, const glm::mat4 &proj);

  // Falling leaves (Main 5.2: ZzzEffectFireLeave.cpp)
  std::unique_ptr<Shader> m_leafShader;
  GLuint m_leafTexture = 0;
  GLuint m_leafVAO = 0, m_leafVBO = 0, m_leafEBO = 0;
  void updateLeaves(float dt, const glm::vec3 &heroPos);
  void spawnLeaf(LeafParticle &leaf, const glm::vec3 &heroPos);
};

#endif // BOID_MANAGER_HPP
