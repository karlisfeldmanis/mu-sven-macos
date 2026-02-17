#ifndef VFX_MANAGER_HPP
#define VFX_MANAGER_HPP

#include "Shader.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

enum class ParticleType {
  BLOOD,     // Red spray
  HIT_SPARK, // Yellow/white flash
  MAGIC_HIT  // Blue/purple swirl (for Lich)
};

class VFXManager {
public:
  void Init(const std::string &effectDataPath);
  void Update(float deltaTime);
  void Render(const glm::mat4 &view, const glm::mat4 &projection);
  void Cleanup();

  // Spawns a burst of particles at a given world position
  void SpawnBurst(ParticleType type, const glm::vec3 &position, int count = 10);

  // Spawns a lightning strike from start to end
  void SpawnLightning(const glm::vec3 &start, const glm::vec3 &end,
                      float duration = 0.3f);

  // Trigger screen flash for player damage
  void TriggerDamageFlash();

  // Render HUD effects (vignette)
  void RenderHUD(int width, int height, float playerHPRatio);

private:
  struct Particle {
    ParticleType type;
    glm::vec3 position;
    glm::vec3 velocity;
    float scale;
    float rotation;
    float lifetime;
    float maxLifetime;
    glm::vec3 color;
    float alpha;
  };

  struct Lightning {
    glm::vec3 start;
    glm::vec3 end;
    float lifetime;
    float maxLifetime;
    std::vector<glm::vec3> points; // Internal segments
  };

  struct InstanceData {
    glm::vec3 worldPos;
    float scale;
    float rotation;
    float frame;
    glm::vec3 color;
    float alpha;
  };

  std::vector<Particle> m_particles;
  std::vector<Lightning> m_lightnings;

  GLuint m_bloodTexture = 0;
  GLuint m_hitTexture = 0;
  GLuint m_lightningTexture = 0;
  std::unique_ptr<Shader> m_shader;
  std::unique_ptr<Shader> m_lineShader;

  GLuint m_quadVAO = 0, m_quadVBO = 0, m_quadEBO = 0;
  GLuint m_instanceVBO = 0;
  GLuint m_lineVAO = 0, m_lineVBO = 0;

  static constexpr int MAX_PARTICLES = 8192;

  void initBuffers();
  void generateLightningPoints(Lightning &ln);
};

#endif // VFX_MANAGER_HPP
