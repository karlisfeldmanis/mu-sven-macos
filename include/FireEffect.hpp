#ifndef FIRE_EFFECT_HPP
#define FIRE_EFFECT_HPP

#include "Shader.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

// Fire offset table: MU local-space offsets where fire spawns on each object type
struct FireOffsetEntry {
  int type;
  std::vector<glm::vec3> offsets; // MU local coords (x, y, z)
};

// Returns MU-local fire offsets for a given object type, or empty if no fire
const std::vector<glm::vec3> &GetFireOffsets(int objectType);

// Returns the object type for a BMD filename, or -1 if not a fire object
int GetFireTypeFromFilename(const std::string &bmdFilename);

class FireEffect {
public:
  void Init(const std::string &effectDataPath);
  void ClearEmitters();
  void AddEmitter(const glm::vec3 &worldPos); // GL world coordinates
  void Update(float deltaTime);
  void Render(const glm::mat4 &view, const glm::mat4 &projection);
  void Cleanup();

  int GetEmitterCount() const { return (int)emitters.size(); }
  int GetParticleCount() const { return (int)particles.size(); }

private:
  struct Emitter {
    glm::vec3 position;
    float spawnAccum = 0.0f;
  };

  struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    float scale;
    float rotation;
    float lifetime;
    float maxLifetime;
    glm::vec3 color;
  };

  // Per-instance GPU data (must match vertex attribute layout)
  struct InstanceData {
    glm::vec3 worldPos;
    float scale;
    float rotation;
    float frame;
    glm::vec3 color;
    float alpha;
  };

  std::vector<Emitter> emitters;
  std::vector<Particle> particles;

  GLuint fireTexture = 0;
  std::unique_ptr<Shader> billboardShader;
  GLuint quadVAO = 0, quadVBO = 0, quadEBO = 0;
  GLuint instanceVBO = 0;

  static constexpr int MAX_PARTICLES = 4096;
  static constexpr float PARTICLE_LIFETIME = 1.0f;
  static constexpr float SPAWN_RATE = 12.0f; // particles/sec per emitter
};

#endif // FIRE_EFFECT_HPP
