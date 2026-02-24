#ifndef VFX_MANAGER_HPP
#define VFX_MANAGER_HPP

#include "Shader.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

enum class ParticleType {
  BLOOD,       // Red spray (Main 5.2: CreateBlood)
  HIT_SPARK,   // White sparks with gravity (Main 5.2: BITMAP_SPARK)
  SMOKE,       // Gray ambient smoke (monsters)
  FIRE,        // Orange fire particle (Budge Dragon breath)
  ENERGY,      // Blue/white energy flash (Lich hand)
  FLARE,       // Bright additive impact flash (Main 5.2: BITMAP_FLASH)
  LEVEL_FLARE, // Main 5.2: BITMAP_FLARE level-up ring (15 joints rising)
  // DK Skill effects
  SKILL_SLASH,   // White-blue slash sparks (Sword1-5)
  SKILL_CYCLONE, // Cyan spinning spark ring (Cyclone/Twisting Slash)
  SKILL_FURY,    // Orange-red ground burst (Rageful Blow)
  SKILL_STAB,    // Dark red piercing sparks (Death Stab)
};

class VFXManager {
public:
  void Init(const std::string &effectDataPath);
  void Update(float deltaTime);
  void Render(const glm::mat4 &view, const glm::mat4 &projection);
  void Cleanup();

  // Spawns a burst of particles at a given world position
  void SpawnBurst(ParticleType type, const glm::vec3 &position, int count = 10);

  // Main 5.2 level-up effect: 15 BITMAP_FLARE joints rising in a ring
  void SpawnLevelUpEffect(const glm::vec3 &position);

  // Update level-up effect center to follow the character
  void UpdateLevelUpCenter(const glm::vec3 &position);

  // Spawn skill cast VFX at hero position (Main 5.2: BITMAP_SHINY+2 sparkle)
  void SpawnSkillCast(uint8_t skillId, const glm::vec3 &heroPos, float facing);

  // Spawn skill impact VFX at monster position (skill-specific particles)
  void SpawnSkillImpact(uint8_t skillId, const glm::vec3 &monsterPos);

  // Spawns a textured ribbon from start heading toward target
  // Main 5.2: two passes per Lich bolt — scale=50 (thick) + scale=10 (thin)
  void SpawnRibbon(const glm::vec3 &start, const glm::vec3 &target, float scale,
                   const glm::vec3 &color, float duration = 0.5f);

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

  // Ribbon segment: one cross-section of the trail (Main 5.2 JOINT Tails)
  struct RibbonSegment {
    glm::vec3 center;
    glm::vec3 right; // Half-width offset in local X (horizontal face)
    glm::vec3 up;    // Half-width offset in local Z (vertical face)
  };

  // Textured ribbon effect (Main 5.2 JOINT with BITMAP_JOINT_THUNDER)
  struct Ribbon {
    glm::vec3 headPos;      // Current head position
    glm::vec3 targetPos;    // Where the ribbon is heading
    float headYaw = 0.0f;   // Current heading yaw (radians)
    float headPitch = 0.0f; // Current heading pitch (radians)
    float scale;            // Half-width (50.0 or 10.0)
    glm::vec3 color;        // RGB tint
    float lifetime;
    float maxLifetime;
    float velocity = 1500.0f;            // World units/second
    float uvScroll = 0.0f;               // UV scroll offset (animated)
    std::vector<RibbonSegment> segments; // Tail trail (newest at [0])
    static constexpr int MAX_SEGMENTS = 50;
  };

  struct InstanceData {
    glm::vec3 worldPos;
    float scale;
    float rotation;
    float frame;
    glm::vec3 color;
    float alpha;
  };

  // Ribbon vertex: position + UV for textured quad strip
  struct RibbonVertex {
    glm::vec3 pos;
    glm::vec2 uv;
  };

  // Main 5.2: BITMAP_MAGIC ground circle (level-up, teleport)
  struct GroundCircle {
    glm::vec3 position;
    float rotation; // Current rotation angle (radians)
    float lifetime;
    float maxLifetime;
    glm::vec3 color;
  };

  // Main 5.2: Level-up orbiting flare effect (ZzzEffectJoint.cpp)
  // 15 BITMAP_FLARE joints, each orbiting with a trail of 20 flat quads.
  static constexpr int LEVEL_UP_MAX_TAILS = 20;
  struct LevelUpSprite {
    float phase;     // Direction[1]: random initial orbit phase
    float riseSpeed; // Direction[2]: random upward speed (world units/tick)
    float height;    // Accumulated height (Position[2])
    int numTails;    // Current number of tail quads (grows to MAX_TAILS)
    glm::vec3 tails[LEVEL_UP_MAX_TAILS]; // Trail positions (newest at [0])
  };
  struct LevelUpEffect {
    glm::vec3 center;  // Character position at spawn
    int lifeTime;      // Remaining ticks (starts at 50, decrements)
    float tickAccum;   // Fractional tick accumulator
    float radius;      // Velocity: orbit radius (40 world units)
    float spriteScale; // Scale: sprite size (40 world units)
    std::vector<LevelUpSprite> sprites; // 15 orbiting BITMAP_FLARE joints
  };

  std::vector<Particle> m_particles;
  std::vector<Ribbon> m_ribbons;
  std::vector<GroundCircle> m_groundCircles;
  std::vector<LevelUpEffect> m_levelUpEffects;

  // Textures
  GLuint m_bloodTexture = 0;
  GLuint m_hitTexture = 0;   // Legacy (Interface/hit.OZT)
  GLuint m_sparkTexture = 0; // Main 5.2: BITMAP_SPARK (Effect/Spark01.OZJ)
  GLuint m_flareTexture = 0; // Main 5.2: BITMAP_FLASH (Effect/flare01.OZJ)
  GLuint m_smokeTexture = 0;
  GLuint m_fireTexture = 0;
  GLuint m_energyTexture = 0;
  GLuint m_lightningTexture = 0; // JointThunder01.OZJ for ribbons
  GLuint m_magicGroundTexture =
      0; // Main 5.2: Magic_Ground2.OZJ (level-up circle)
  GLuint m_ringTexture = 0;         // ring_of_gradation.OZJ (level-up ring)
  GLuint m_bitmapFlareTexture = 0;  // Main 5.2: BITMAP_FLARE (Effect/Flare.OZJ)

  std::unique_ptr<Shader> m_shader;
  std::unique_ptr<Shader> m_lineShader;

  // Billboard particle buffers
  GLuint m_quadVAO = 0, m_quadVBO = 0, m_quadEBO = 0;
  GLuint m_instanceVBO = 0;

  // Ribbon buffers (pos + uv per vertex)
  GLuint m_ribbonVAO = 0, m_ribbonVBO = 0;
  static constexpr int MAX_RIBBON_VERTS =
      1600; // 4 ribbons x 50 seg x 2 faces x 4 verts

  static constexpr int MAX_PARTICLES = 8192;

  void initBuffers();
  void updateRibbon(Ribbon &r, float dt);
  void renderRibbons(const glm::mat4 &view, const glm::mat4 &projection);
  void renderGroundCircles(const glm::mat4 &view, const glm::mat4 &projection);
  void renderLevelUpEffects(const glm::mat4 &view, const glm::mat4 &projection);
};

#endif // VFX_MANAGER_HPP
