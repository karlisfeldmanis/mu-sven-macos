#include "VFXManager.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

void VFXManager::Init(const std::string &effectDataPath) {
  // Blood texture
  m_bloodTexture =
      TextureLoader::LoadOZT(effectDataPath + "/Effect/blood01.ozt");

  // Main 5.2: BITMAP_SPARK — white star sparks on melee hit
  m_sparkTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Spark01.OZJ");

  // Main 5.2: BITMAP_FLASH — bright additive impact flare
  m_flareTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/flare01.OZJ");

  // Legacy hit texture (fallback if spark fails)
  m_hitTexture = TextureLoader::LoadOZT(effectDataPath + "/Interface/hit.OZT");

  // Lightning ribbon texture (Main 5.2: BITMAP_JOINT_THUNDER)
  m_lightningTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/JointThunder01.OZJ");

  // Monster ambient VFX textures
  m_smokeTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/smoke01.OZJ");
  m_fireTexture = TextureLoader::LoadOZJ(effectDataPath + "/Effect/Fire01.OZJ");
  m_energyTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/JointEnergy01.OZJ");

  // Main 5.2: BITMAP_MAGIC+1 — level-up magic circle ground decal
  m_magicGroundTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Magic_Ground2.OZJ");

  // Main 5.2: ring_of_gradation — golden ring for level-up effect
  m_ringTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/ring_of_gradation.OZJ");

  // Main 5.2: BITMAP_ENERGY — Energy Ball projectile (Effect/Thunder01.jpg)
  m_thunderTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Thunder01.OZJ");

  // Main 5.2: BITMAP_FLARE — level-up orbiting flare texture (Effect/Flare.jpg)
  m_bitmapFlareTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Flare.OZJ");
  if (m_bitmapFlareTexture == 0)
    m_bitmapFlareTexture = m_flareTexture; // Fallback to flare01.OZJ

  if (m_bloodTexture == 0)
    std::cerr << "[VFX] Failed to load blood texture" << std::endl;
  if (m_sparkTexture == 0)
    std::cerr << "[VFX] Failed to load spark texture (Spark01.OZJ)"
              << std::endl;
  if (m_flareTexture == 0)
    std::cerr << "[VFX] Failed to load flare texture (flare01.OZJ)"
              << std::endl;
  if (m_lightningTexture == 0)
    std::cerr << "[VFX] Failed to load lightning texture" << std::endl;
  if (m_smokeTexture == 0)
    std::cerr << "[VFX] Failed to load smoke texture" << std::endl;
  if (m_fireTexture == 0)
    std::cerr << "[VFX] Failed to load fire texture" << std::endl;
  if (m_energyTexture == 0)
    std::cerr << "[VFX] Failed to load energy texture" << std::endl;
  if (m_magicGroundTexture == 0)
    std::cerr << "[VFX] Failed to load magic ground texture (Magic_Ground2.OZJ)"
              << std::endl;

  if (m_thunderTexture == 0)
    std::cerr << "[VFX] WARNING: Thunder01.OZJ failed to load — Energy Ball will use "
                 "fallback texture" << std::endl;

  // Shaders
  std::ifstream test("shaders/billboard.vert");
  if (test.good()) {
    m_shader = std::make_unique<Shader>("shaders/billboard.vert",
                                        "shaders/billboard.frag");
    m_lineShader =
        std::make_unique<Shader>("shaders/line.vert", "shaders/line.frag");
  } else {
    m_shader = std::make_unique<Shader>("../shaders/billboard.vert",
                                        "../shaders/billboard.frag");
    m_lineShader = std::make_unique<Shader>("../shaders/line.vert",
                                            "../shaders/line.frag");
  }

  // Load Fire Ball 3D model (Main 5.2: MODEL_FIRE = Data/Skill/Fire01.bmd)
  std::string skillPath = effectDataPath + "/Skill/";
  m_fireBmd = BMDParser::Parse(skillPath + "Fire01.bmd");
  if (m_fireBmd) {
    auto bones = ComputeBoneMatrices(m_fireBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_fireBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_fireMeshes, dummyAABB, false);
    }
    std::cout << "[VFX] Fire01.bmd loaded: " << m_fireMeshes.size() << " meshes"
              << std::endl;
  } else {
    std::cerr << "[VFX] Failed to load Fire01.bmd — Fire Ball will use billboard fallback"
              << std::endl;
  }

  // Load Lightning sky-strike model (Main 5.2: MODEL_SKILL_BLAST = Data/Skill/Blast01.bmd)
  m_blastBmd = BMDParser::Parse(skillPath + "Blast01.bmd");
  if (m_blastBmd) {
    auto bones = ComputeBoneMatrices(m_blastBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_blastBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_blastMeshes, dummyAABB,
                          false);
    }
    std::cout << "[VFX] Blast01.bmd loaded: " << m_blastMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Poison cloud model (Main 5.2: MODEL_POISON = Data/Skill/Poison01.bmd)
  m_poisonBmd = BMDParser::Parse(skillPath + "Poison01.bmd");
  if (m_poisonBmd) {
    auto bones = ComputeBoneMatrices(m_poisonBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_poisonBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_poisonMeshes, dummyAABB,
                          false);
    }
    std::cout << "[VFX] Poison01.bmd loaded: " << m_poisonMeshes.size()
              << " meshes" << std::endl;
  }

  // Model shader for 3D effect models (Fire Ball, etc.)
  std::ifstream modelTest("shaders/model.vert");
  if (modelTest.good()) {
    m_modelShader = std::make_unique<Shader>("shaders/model.vert",
                                              "shaders/model.frag");
  } else {
    m_modelShader = std::make_unique<Shader>("../shaders/model.vert",
                                              "../shaders/model.frag");
  }

  initBuffers();
}

void VFXManager::initBuffers() {
  // Billboard quad (existing)
  float quadVerts[] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,
  };
  unsigned int quadIndices[] = {0, 1, 2, 0, 2, 3};

  glGenVertexArrays(1, &m_quadVAO);
  glGenBuffers(1, &m_quadVBO);
  glGenBuffers(1, &m_quadEBO);
  glGenBuffers(1, &m_instanceVBO);

  glBindVertexArray(m_quadVAO);

  glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_quadEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices,
               GL_STATIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
  glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * sizeof(InstanceData), nullptr,
               GL_DYNAMIC_DRAW);

  // location 1: iWorldPos (vec3)
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, worldPos));
  glEnableVertexAttribArray(1);
  glVertexAttribDivisor(1, 1);

  // location 2: iScale (float)
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, scale));
  glEnableVertexAttribArray(2);
  glVertexAttribDivisor(2, 1);

  // location 3: iRotation (float)
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, rotation));
  glEnableVertexAttribArray(3);
  glVertexAttribDivisor(3, 1);

  // location 4: iFrame (float)
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, frame));
  glEnableVertexAttribArray(4);
  glVertexAttribDivisor(4, 1);

  // location 5: iColor (vec3)
  glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, color));
  glEnableVertexAttribArray(5);
  glVertexAttribDivisor(5, 1);

  // location 6: iAlpha (float)
  glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, alpha));
  glEnableVertexAttribArray(6);
  glVertexAttribDivisor(6, 1);

  // Ribbon buffers: vec3 pos + vec2 uv = 5 floats per vertex
  // Matches line.vert layout: location 0 = aPos (vec3), location 1 = aTexCoord
  // (vec2)
  glGenVertexArrays(1, &m_ribbonVAO);
  glGenBuffers(1, &m_ribbonVBO);
  glBindVertexArray(m_ribbonVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVBO);
  glBufferData(GL_ARRAY_BUFFER, MAX_RIBBON_VERTS * sizeof(RibbonVertex),
               nullptr, GL_DYNAMIC_DRAW);

  // location 0: aPos (vec3)
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex),
                        (void *)offsetof(RibbonVertex, pos));
  glEnableVertexAttribArray(0);

  // location 1: aTexCoord (vec2)
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex),
                        (void *)offsetof(RibbonVertex, uv));
  glEnableVertexAttribArray(1);

  glBindVertexArray(0);
}

void VFXManager::SpawnBurst(ParticleType type, const glm::vec3 &position,
                            int count) {
  for (int i = 0; i < count; ++i) {
    if (m_particles.size() >= (size_t)MAX_PARTICLES)
      break;

    Particle p;
    p.type = type;
    p.position = position;
    p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;

    float angle = (float)(rand() % 360) * 3.14159f / 180.0f;

    switch (type) {
    case ParticleType::BLOOD: {
      // Main 5.2: CreateBlood — red spray, gravity-affected
      float speed = 50.0f + (float)(rand() % 80);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 100.0f + (float)(rand() % 60),
                    std::sin(angle) * speed);
      p.scale = 20.0f + (float)(rand() % 15);
      p.maxLifetime = 0.6f + (float)(rand() % 40) / 100.0f;
      p.color = glm::vec3(0.8f, 0.0f, 0.0f);
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::HIT_SPARK: {
      // Main 5.2: BITMAP_SPARK — 20 white sparks, gravity, arc trajectory
      // Lifetime 8-15 frames (0.32-0.6s), scale 0.4-0.8 × base ~25
      float speed = 80.0f + (float)(rand() % 120);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 100.0f + (float)(rand() % 100),
                    std::sin(angle) * speed);
      p.scale = 10.0f + (float)(rand() % 10);
      p.maxLifetime = 0.32f + (float)(rand() % 28) / 100.0f;
      p.color = glm::vec3(1.0f, 1.0f, 1.0f);
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SMOKE: {
      // Main 5.2: BITMAP_SMOKE — ambient monster smoke, slow rise
      float speed = 10.0f + (float)(rand() % 20);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 20.0f + (float)(rand() % 30),
                    std::sin(angle) * speed);
      p.scale = 20.0f + (float)(rand() % 20);
      p.maxLifetime = 1.0f + (float)(rand() % 50) / 100.0f;
      p.color = glm::vec3(0.6f, 0.6f, 0.6f);
      p.alpha = 0.6f;
      break;
    }
    case ParticleType::FIRE: {
      // Main 5.2: BITMAP_FIRE_CURSEDLICH — fire breath, upward burst
      // Lifetime 8-20 frames, scale 0.2-0.5, gravity 0.15-0.30
      float speed = 30.0f + (float)(rand() % 40);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 60.0f + (float)(rand() % 40),
                    std::sin(angle) * speed);
      p.scale = 30.0f + (float)(rand() % 20);
      p.maxLifetime = 0.32f + (float)(rand() % 24) / 100.0f;
      p.color = glm::vec3(1.0f, 0.8f, 0.3f);
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::ENERGY: {
      // Main 5.2: BITMAP_ENERGY — Lich hand flash, fast fade
      float speed = 40.0f + (float)(rand() % 60);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 50.0f + (float)(rand() % 30),
                    std::sin(angle) * speed);
      p.scale = 40.0f + (float)(rand() % 20);
      p.maxLifetime = 0.2f + (float)(rand() % 10) / 100.0f;
      p.color = glm::vec3(0.6f, 0.7f, 1.0f);
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::FLARE: {
      // Main 5.2: BITMAP_FLASH — bright stationary impact flash
      // Lifetime 8-12 frames (0.3-0.5s), large scale, no movement
      p.velocity = glm::vec3(0.0f);
      p.scale = 80.0f + (float)(rand() % 40);
      p.maxLifetime = 0.3f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(1.0f, 1.0f, 1.0f);
      p.alpha = 0.8f;
      break;
    }
    case ParticleType::LEVEL_FLARE: {
      // Main 5.2: BITMAP_FLARE level-up joint — rises upward from ring
      // Scale 40 in original (CreateJoint arg), mapped to our world units
      p.velocity =
          glm::vec3(std::cos(angle) * 30.0f, 80.0f + (float)(rand() % 40),
                    std::sin(angle) * 30.0f);
      p.scale = 50.0f + (float)(rand() % 30);
      p.maxLifetime = 1.2f + (float)(rand() % 40) / 100.0f;
      p.color =
          glm::vec3(1.0f, 0.7f, 0.2f); // Golden-orange (match ground circle)
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SKILL_SLASH: {
      // Main 5.2: BITMAP_SPARK+1 — white-blue slash sparks, wide horizontal
      float speed = 120.0f + (float)(rand() % 100);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 40.0f + (float)(rand() % 60),
                    std::sin(angle) * speed);
      p.scale = 12.0f + (float)(rand() % 10);
      p.maxLifetime = 0.25f + (float)(rand() % 15) / 100.0f;
      p.color = glm::vec3(0.7f, 0.85f, 1.0f); // White-blue
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SKILL_CYCLONE: {
      // Main 5.2: Spinning ring of cyan sparks (evenly spaced + jitter)
      float ringAngle =
          (float)i / (float)std::max(count, 1) * 6.2832f + angle * 0.3f;
      float speed = 60.0f + (float)(rand() % 40);
      p.velocity =
          glm::vec3(std::cos(ringAngle) * speed,
                    30.0f + (float)(rand() % 40),
                    std::sin(ringAngle) * speed);
      p.scale = 15.0f + (float)(rand() % 12);
      p.maxLifetime = 0.4f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(0.3f, 0.9f, 1.0f); // Cyan-teal
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SKILL_FURY: {
      // Main 5.2: CreateEffect(MODEL_SKILL_FURY_STRIKE) — ground burst
      float speed = 40.0f + (float)(rand() % 80);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 150.0f + (float)(rand() % 80),
                    std::sin(angle) * speed);
      p.scale = 40.0f + (float)(rand() % 30);
      p.maxLifetime = 0.5f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(1.0f, 0.5f, 0.15f); // Orange-red
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SKILL_STAB: {
      // Main 5.2: Piercing directional sparks — narrow cone, fast, dark red
      float spread = 0.4f; // Narrow cone
      float fwdAngle = angle * spread;
      float speed = 150.0f + (float)(rand() % 100);
      p.velocity =
          glm::vec3(std::cos(fwdAngle) * speed,
                    20.0f + (float)(rand() % 30),
                    std::sin(fwdAngle) * speed);
      p.scale = 10.0f + (float)(rand() % 8);
      p.maxLifetime = 0.2f + (float)(rand() % 10) / 100.0f;
      p.color = glm::vec3(0.9f, 0.2f, 0.2f); // Dark red
      p.alpha = 1.0f;
      break;
    }
    // ── DW Spell particles ──
    case ParticleType::SPELL_ENERGY: {
      // Blue-white energy burst — medium speed outward, rising
      float speed = 60.0f + (float)(rand() % 80);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 80.0f + (float)(rand() % 60),
                    std::sin(angle) * speed);
      p.scale = 25.0f + (float)(rand() % 15);
      p.maxLifetime = 0.35f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(0.5f, 0.7f, 1.0f); // Blue-white
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SPELL_FIRE: {
      // Orange-yellow fire burst — fast upward, wide spread
      float speed = 50.0f + (float)(rand() % 60);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 100.0f + (float)(rand() % 80),
                    std::sin(angle) * speed);
      p.scale = 35.0f + (float)(rand() % 25);
      p.maxLifetime = 0.4f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(1.0f, 0.6f, 0.15f); // Orange-yellow
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SPELL_ICE: {
      // Cyan-white ice shards — fast outward, slight rise, quick fade
      float speed = 100.0f + (float)(rand() % 80);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 30.0f + (float)(rand() % 40),
                    std::sin(angle) * speed);
      p.scale = 12.0f + (float)(rand() % 10);
      p.maxLifetime = 0.3f + (float)(rand() % 15) / 100.0f;
      p.color = glm::vec3(0.7f, 0.95f, 1.0f); // Cyan-white
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SPELL_LIGHTNING: {
      // Bright white-blue electric sparks — very fast, erratic, short life
      float speed = 180.0f + (float)(rand() % 120);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 60.0f + (float)(rand() % 80),
                    std::sin(angle) * speed);
      p.scale = 10.0f + (float)(rand() % 8);
      p.maxLifetime = 0.15f + (float)(rand() % 10) / 100.0f;
      p.color = glm::vec3(0.8f, 0.9f, 1.0f); // White-blue
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SPELL_POISON: {
      // Green toxic cloud — slow drift, long lifetime, large particles
      float speed = 20.0f + (float)(rand() % 30);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 15.0f + (float)(rand() % 20),
                    std::sin(angle) * speed);
      p.scale = 30.0f + (float)(rand() % 20);
      p.maxLifetime = 0.6f + (float)(rand() % 30) / 100.0f;
      p.color = glm::vec3(0.2f, 0.8f, 0.15f); // Toxic green
      p.alpha = 0.7f;
      break;
    }
    case ParticleType::SPELL_METEOR: {
      // Dark orange falling sparks — fast downward + outward
      float speed = 80.0f + (float)(rand() % 60);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 180.0f + (float)(rand() % 100),
                    std::sin(angle) * speed);
      p.scale = 20.0f + (float)(rand() % 15);
      p.maxLifetime = 0.5f + (float)(rand() % 25) / 100.0f;
      p.color = glm::vec3(1.0f, 0.4f, 0.1f); // Dark orange
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SPELL_DARK: {
      // Purple-black dark energy — medium speed, swirling
      float ringAngle =
          (float)i / (float)std::max(count, 1) * 6.2832f + angle * 0.5f;
      float speed = 50.0f + (float)(rand() % 60);
      p.velocity =
          glm::vec3(std::cos(ringAngle) * speed,
                    40.0f + (float)(rand() % 50),
                    std::sin(ringAngle) * speed);
      p.scale = 25.0f + (float)(rand() % 20);
      p.maxLifetime = 0.45f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(0.6f, 0.2f, 0.9f); // Purple
      p.alpha = 0.9f;
      break;
    }
    case ParticleType::SPELL_WATER: {
      // Blue water spray — medium outward, moderate rise
      float speed = 70.0f + (float)(rand() % 80);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 50.0f + (float)(rand() % 40),
                    std::sin(angle) * speed);
      p.scale = 18.0f + (float)(rand() % 12);
      p.maxLifetime = 0.35f + (float)(rand() % 15) / 100.0f;
      p.color = glm::vec3(0.2f, 0.5f, 1.0f); // Deep blue
      p.alpha = 0.9f;
      break;
    }
    case ParticleType::SPELL_TELEPORT: {
      // Bright white flash ring — evenly spaced around character, rising
      float ringAngle =
          (float)i / (float)std::max(count, 1) * 6.2832f;
      float speed = 100.0f + (float)(rand() % 40);
      p.velocity =
          glm::vec3(std::cos(ringAngle) * speed,
                    120.0f + (float)(rand() % 60),
                    std::sin(ringAngle) * speed);
      p.scale = 30.0f + (float)(rand() % 20);
      p.maxLifetime = 0.5f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(1.0f, 1.0f, 1.0f); // Bright white
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::SPELL_ENERGY_ORB: {
      // Main 5.2: BITMAP_ENERGY (Thunder01.jpg) — energy ball orb/swirl
      // Created directly with specific params in updateSpellProjectiles,
      // but this handles SpawnBurst fallback
      p.velocity = glm::vec3(0.0f);
      p.scale = 80.0f + (float)(rand() % 40);
      p.maxLifetime = 0.2f + (float)(rand() % 10) / 100.0f;
      p.color = glm::vec3(0.4f, 0.6f, 1.0f); // Blue
      p.alpha = 1.0f;
      p.frame = -1.0f; // Full texture UV
      break;
    }
    }

    p.lifetime = p.maxLifetime;
    m_particles.push_back(p);
  }
}

void VFXManager::SpawnSkillCast(uint8_t skillId, const glm::vec3 &heroPos,
                                float facing) {
  glm::vec3 castPos = heroPos + glm::vec3(0, 50, 0); // Chest height
  switch (skillId) {
  case 19:
  case 20:
  case 21:
  case 23: // Sword skills (Falling Slash, Lunge, Uppercut, Slash)
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    SpawnBurst(ParticleType::HIT_SPARK, castPos, 8);
    break;
  case 22: // Cyclone
    SpawnBurst(ParticleType::SKILL_CYCLONE, heroPos + glm::vec3(0, 30, 0), 20);
    break;
  case 41: // Twisting Slash
    SpawnBurst(ParticleType::SKILL_CYCLONE, heroPos + glm::vec3(0, 30, 0), 30);
    SpawnBurst(ParticleType::FLARE, castPos, 3);
    break;
  case 42: // Rageful Blow
    SpawnBurst(ParticleType::SKILL_FURY, heroPos, 20);
    SpawnBurst(ParticleType::FLARE, castPos, 4);
    break;
  case 43: // Death Stab
    SpawnBurst(ParticleType::SKILL_STAB, castPos, 12);
    break;
  // DW Spells
  case 17: // Energy Ball
    SpawnBurst(ParticleType::SPELL_ENERGY, castPos, 12);
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    break;
  case 4: // Fire Ball
    SpawnBurst(ParticleType::SPELL_FIRE, castPos, 15);
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    break;
  case 1: // Poison — Main 5.2: no caster-side VFX, cloud spawns at target
    break;
  case 3: // Lightning
    SpawnBurst(ParticleType::SPELL_LIGHTNING, castPos, 20);
    SpawnBurst(ParticleType::FLARE, castPos, 3);
    break;
  case 2: // Meteorite
    SpawnBurst(ParticleType::SPELL_METEOR, castPos + glm::vec3(0, 100, 0), 15);
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    break;
  case 7: // Ice
    SpawnBurst(ParticleType::SPELL_ICE, castPos, 15);
    SpawnBurst(ParticleType::FLARE, castPos, 1);
    break;
  case 5: // Flame (AoE)
    SpawnBurst(ParticleType::SPELL_FIRE, heroPos + glm::vec3(0, 30, 0), 25);
    SpawnBurst(ParticleType::FLARE, castPos, 3);
    break;
  case 8: // Twister (AoE)
    SpawnBurst(ParticleType::SPELL_DARK, heroPos + glm::vec3(0, 30, 0), 20);
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    break;
  case 6: // Teleport
    SpawnBurst(ParticleType::SPELL_TELEPORT, heroPos, 25);
    SpawnBurst(ParticleType::FLARE, castPos, 4);
    break;
  case 9: // Evil Spirit (AoE)
    SpawnBurst(ParticleType::SPELL_DARK, castPos, 25);
    SpawnBurst(ParticleType::SPELL_ENERGY, castPos, 10);
    SpawnBurst(ParticleType::FLARE, castPos, 3);
    break;
  case 12: // Aqua Beam
    SpawnBurst(ParticleType::SPELL_WATER, castPos, 20);
    SpawnBurst(ParticleType::FLARE, castPos, 3);
    break;
  case 10: // Hellfire (large AoE)
    SpawnBurst(ParticleType::SPELL_FIRE, heroPos + glm::vec3(0, 20, 0), 30);
    SpawnBurst(ParticleType::SPELL_METEOR, heroPos + glm::vec3(0, 80, 0), 15);
    SpawnBurst(ParticleType::FLARE, castPos, 5);
    break;
  }
}

void VFXManager::SpawnSkillImpact(uint8_t skillId,
                                  const glm::vec3 &monsterPos) {
  glm::vec3 hitPos = monsterPos + glm::vec3(0, 50, 0);
  switch (skillId) {
  case 19:
  case 20:
  case 21:
  case 23: // Basic sword skills
    SpawnBurst(ParticleType::SKILL_SLASH, hitPos, 15);
    SpawnBurst(ParticleType::FLARE, hitPos, 1);
    break;
  case 22: // Cyclone
    SpawnBurst(ParticleType::SKILL_CYCLONE, hitPos, 15);
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 10);
    break;
  case 41: // Twisting Slash
    SpawnBurst(ParticleType::SKILL_CYCLONE, hitPos, 20);
    SpawnBurst(ParticleType::FLARE, hitPos, 2);
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 15);
    break;
  case 42: // Rageful Blow
    SpawnBurst(ParticleType::SKILL_FURY, hitPos, 25);
    SpawnBurst(ParticleType::FLARE, hitPos, 3);
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 20);
    break;
  case 43: // Death Stab
    SpawnBurst(ParticleType::SKILL_STAB, hitPos, 15);
    SpawnBurst(ParticleType::FLARE, hitPos, 2);
    break;
  // DW Spell impacts
  case 17: // Energy Ball
    SpawnBurst(ParticleType::SPELL_ENERGY, hitPos, 15);
    SpawnBurst(ParticleType::FLARE, hitPos, 2);
    break;
  case 4: // Fire Ball
    SpawnBurst(ParticleType::SPELL_FIRE, hitPos, 18);
    SpawnBurst(ParticleType::FLARE, hitPos, 3);
    break;
  case 1: // Poison — cloud already spawned at cast time, skip duplicate
    break;
  case 3: // Lightning
    SpawnBurst(ParticleType::SPELL_LIGHTNING, hitPos, 25);
    SpawnBurst(ParticleType::FLARE, hitPos, 3);
    break;
  case 2: // Meteorite — falling fireball (impact particles handled by bolt system)
    SpawnMeteorStrike(hitPos);
    break;
  case 7: // Ice
    SpawnBurst(ParticleType::SPELL_ICE, hitPos, 20);
    SpawnBurst(ParticleType::FLARE, hitPos, 2);
    break;
  case 5: // Flame (AoE)
    SpawnBurst(ParticleType::SPELL_FIRE, hitPos, 25);
    SpawnBurst(ParticleType::FLARE, hitPos, 4);
    break;
  case 8: // Twister (AoE)
    SpawnBurst(ParticleType::SPELL_DARK, hitPos, 20);
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 10);
    break;
  case 9: // Evil Spirit (AoE)
    SpawnBurst(ParticleType::SPELL_DARK, hitPos, 25);
    SpawnBurst(ParticleType::SPELL_ENERGY, hitPos, 12);
    SpawnBurst(ParticleType::FLARE, hitPos, 4);
    break;
  case 12: // Aqua Beam
    SpawnBurst(ParticleType::SPELL_WATER, hitPos, 25);
    SpawnBurst(ParticleType::FLARE, hitPos, 3);
    break;
  case 10: // Hellfire (large AoE)
    SpawnBurst(ParticleType::SPELL_FIRE, hitPos, 30);
    SpawnBurst(ParticleType::SPELL_METEOR, hitPos, 15);
    SpawnBurst(ParticleType::FLARE, hitPos, 5);
    break;
  }
}

void VFXManager::SpawnSpellProjectile(uint8_t skillId, const glm::vec3 &start,
                                      const glm::vec3 &target) {
  SpellProjectile proj;
  // Main 5.2: Position[2] += 100 — start 100 units above caster
  proj.position = start + glm::vec3(0, 100, 0);
  proj.target = target + glm::vec3(0, 50, 0); // Aim at chest height

  glm::vec3 delta = proj.target - proj.position;
  float dist = glm::length(delta);
  if (dist < 1.0f)
    return;

  proj.direction = delta / dist;
  // Main 5.2: Direction=(0,-60,0) = 60 units/frame × 25fps = 1500 units/sec
  proj.speed = 1500.0f;
  proj.rotation = 0.0f;
  proj.rotSpeed = 500.0f * 3.14159f / 180.0f; // 20 deg/tick visual spin
  // Heading angles for 3D model orientation
  proj.yaw = std::atan2(proj.direction.x, proj.direction.z);
  proj.pitch = std::asin(glm::clamp(-proj.direction.y, -1.0f, 1.0f));
  // Main 5.2: LifeTime=20 ticks (0.8s), distance-based for variable range
  proj.maxLifetime = std::min(1.2f, dist / proj.speed + 0.05f);
  proj.lifetime = proj.maxLifetime;
  proj.trailTimer = 0.0f;
  proj.alpha = 1.0f;
  proj.skillId = skillId;

  // Main 5.2: Only Energy Ball + Fire Ball are actual traveling projectiles.
  // Poison/Ice/Meteorite create instant effects at target.
  switch (skillId) {
  case 17: // Energy Ball — Main 5.2: BITMAP_ENERGY, blue-dominant light
    proj.scale = 40.0f;
    proj.color = glm::vec3(0.4f, 0.6f, 1.0f); // Blue-dominant (0.2R, 0.4G, 1.0B)
    proj.trailType = ParticleType::SPELL_ENERGY;
    break;
  case 4: { // Fire Ball — Main 5.2: MODEL_FIRE 3D model + particle trail
    // Main 5.2: Scale = (rand()%4+8)*0.1f = 0.8-1.1 random per cast
    float rndScale = (float)(rand() % 4 + 8) * 0.1f;
    proj.scale = m_fireMeshes.empty() ? 45.0f : rndScale;
    // Slight color variation: orange shifts between warm/hot
    float rG = 0.5f + (float)(rand() % 20) * 0.01f;  // 0.50-0.69
    float rB = 0.10f + (float)(rand() % 10) * 0.01f;  // 0.10-0.19
    proj.color = glm::vec3(1.0f, rG, rB);
    proj.trailType = ParticleType::SPELL_FIRE;
    // Random spin speed & direction (Main 5.2: 20 deg/tick base)
    float spinSign = (rand() % 2 == 0) ? 1.0f : -1.0f;
    float spinVar = 400.0f + (float)(rand() % 200); // 400-600 deg/sec
    proj.rotSpeed = spinSign * spinVar * 3.14159f / 180.0f;
    // Random initial rotation so they start at different angles
    proj.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
    break;
  }
  default:
    proj.scale = 35.0f;
    proj.color = glm::vec3(0.4f, 0.6f, 1.0f);
    proj.trailType = ParticleType::SPELL_ENERGY;
    break;
  }

  m_spellProjectiles.push_back(proj);
}

void VFXManager::SpawnRibbon(const glm::vec3 &start, const glm::vec3 &target,
                             float scale, const glm::vec3 &color,
                             float duration) {
  Ribbon r;
  r.headPos = start;
  r.targetPos = target;
  r.scale = scale;
  r.color = color;
  r.lifetime = duration;
  r.maxLifetime = duration;
  r.velocity = 1500.0f; // Fast travel speed (world units/sec)
  r.uvScroll = 0.0f;

  // Initialize heading toward target
  glm::vec3 dir = target - start;
  float dist = glm::length(dir);
  if (dist > 0.01f) {
    dir /= dist;
    r.headYaw = std::atan2(dir.x, dir.z);
    r.headPitch = std::asin(glm::clamp(dir.y, -1.0f, 1.0f));
  }

  m_ribbons.push_back(std::move(r));
}

// Main 5.2: AT_SKILL_BLAST — twin sky-strike bolts at target position
// Creates 2 Blast01.bmd orbs that fall from sky with gravity and explode on impact
void VFXManager::SpawnLightningStrike(const glm::vec3 &targetPos) {
  for (int i = 0; i < 2; ++i) {
    LightningBolt bolt;
    // Main 5.2: Position += (rand%100+200, rand%100-50, rand%500+300)
    // Map: Main5.2 X→our X, Main5.2 Y→our Z, Main5.2 Z→our Y
    float scatterX = (float)(rand() % 100 + 200); // +200 to +300 (asymmetric)
    float scatterZ = (float)(rand() % 100 - 50);  // -50 to +50
    float height = (float)(rand() % 500 + 300);   // +300 to +800 above target
    bolt.position = targetPos + glm::vec3(scatterX, height, scatterZ);

    // Main 5.2: Direction=(0,0,-50-rand%50), Angle=(0,20,0)
    // MoveParticle rotates Direction by 20° Y before applying.
    // In our Y-up coords: downward Z in Main5.2 = downward Y for us.
    // Rotate (0, -V, 0) by 20° around Y-axis:
    //   X' = -V * sin(20°), Y' = -V * cos(20°), Z' = 0
    float fallSpeed = (50.0f + (float)(rand() % 50)) * 25.0f; // 1250-2500 units/sec
    float angle20 = glm::radians(20.0f);
    bolt.velocity = glm::vec3(-fallSpeed * std::sin(angle20), // Slight horizontal drift
                              -fallSpeed * std::cos(angle20), // Main downward component
                              0.0f);

    // Main 5.2: Scale = (rand()%8+10)*0.1f = 1.0-1.8
    bolt.scale = (float)(rand() % 8 + 10) * 0.1f;
    bolt.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
    bolt.maxLifetime = 1.2f; // 30 ticks at 25fps
    bolt.lifetime = bolt.maxLifetime;
    bolt.impacted = false;
    bolt.impactTimer = 0.0f;
    bolt.numTrail = 0;
    bolt.trailTimer = 0.0f;
    m_lightningBolts.push_back(bolt);
  }
}

// Main 5.2: Meteorite — MODEL_FIRE SubType 0
// Position offset: X += 130+rand%32, Z(height) += 400
// Direction = (0, 0, -50) rotated by Angle(0, 20, 0) — diagonal fall
// Trail: per-tick BITMAP_FIRE SubType 5 billboard sprites (no ribbon)
void VFXManager::SpawnMeteorStrike(const glm::vec3 &targetPos) {
  MeteorBolt m;
  // Main 5.2: Position[0] += 130+rand%32, Position[2] += 400
  float offsetX = 130.0f + (float)(rand() % 32);
  float height = 400.0f;
  m.position = targetPos + glm::vec3(offsetX, height, 0.0f);

  // Main 5.2: Direction=(0,0,-50) rotated by Angle(0,20,0) — diagonal fall
  float fallSpeed = 50.0f * 25.0f; // 1250 units/sec
  float angleRad = glm::radians(20.0f);
  m.velocity = glm::vec3(-std::sin(angleRad) * fallSpeed,
                          -std::cos(angleRad) * fallSpeed,
                          0.0f);

  // Main 5.2: Scale = (rand%8+10)*0.1 = 1.0-1.7
  m.scale = (float)(rand() % 8 + 10) * 0.1f;
  m.maxLifetime = 1.6f; // 40 ticks at 25fps
  m.lifetime = m.maxLifetime;
  m.impacted = false;
  m.impactTimer = 0.0f;
  m.trailTimer = 0.0f;
  m_meteorBolts.push_back(m);
}

void VFXManager::updateLightningBolts(float dt) {
  for (auto &b : m_lightningBolts) {
    if (b.impacted) {
      b.impactTimer += dt;
      continue;
    }
    b.lifetime -= dt;
    b.position += b.velocity * dt;
    b.rotation += 5.0f * dt; // Lightning spins

    // BITMAP_JOINT_ENERGY trail — update at tick rate (~25fps)
    b.trailTimer += dt;
    if (b.trailTimer >= 0.04f) {
      b.trailTimer -= 0.04f;
      int newCount =
          std::min(b.numTrail + 1, (int)LightningBolt::MAX_TRAIL);
      for (int j = newCount - 1; j > 0; --j)
        b.trail[j] = b.trail[j - 1];
      b.trail[0] = b.position;
      b.numTrail = newCount;
    }

    // Check terrain collision
    float groundH = m_getTerrainHeight
                         ? m_getTerrainHeight(b.position.x, b.position.z)
                         : 0.0f;
    if (b.position.y <= groundH || b.lifetime <= 0.0f) {
      b.position.y = groundH;
      b.velocity = glm::vec3(0.0f); // Main 5.2: Vector(0,0,0,Direction)
      b.impacted = true;
      b.impactTimer = 0.0f;
      glm::vec3 impactAbove = b.position + glm::vec3(0, 80, 0);
      // Main 5.2 impact: 6x stone debris + BITMAP_SHINY+4 + BITMAP_EXPLOTION
      SpawnBurst(ParticleType::SPELL_LIGHTNING, impactAbove, 20);
      SpawnBurst(ParticleType::FLARE, impactAbove, 5);
      SpawnBurst(ParticleType::HIT_SPARK, b.position + glm::vec3(0, 30, 0), 8);
    }
  }
  // Remove expired bolts (impacted + aftermath faded)
  m_lightningBolts.erase(
      std::remove_if(m_lightningBolts.begin(), m_lightningBolts.end(),
                     [](const LightningBolt &b) {
                       return b.impacted && b.impactTimer > 0.5f;
                     }),
      m_lightningBolts.end());
}

void VFXManager::updateMeteorBolts(float dt) {
  for (auto &m : m_meteorBolts) {
    if (m.impacted) {
      m.impactTimer += dt;
      continue;
    }
    m.lifetime -= dt;
    m.position += m.velocity * dt;

    // Main 5.2: spawn BITMAP_FIRE SubType 5 every tick — fire trail particles
    m.trailTimer += dt;
    if (m.trailTimer >= 0.04f) {
      m.trailTimer -= 0.04f;
      SpawnBurst(ParticleType::SPELL_FIRE, m.position, 2);
    }

    // Terrain collision
    float groundH = m_getTerrainHeight
                         ? m_getTerrainHeight(m.position.x, m.position.z)
                         : 0.0f;
    if (m.position.y <= groundH || m.lifetime <= 0.0f) {
      m.position.y = groundH;
      m.velocity = glm::vec3(0.0f);
      m.impacted = true;
      m.impactTimer = 0.0f;
      // Main 5.2 impact: explosion + stone debris particles
      glm::vec3 impactAbove = m.position + glm::vec3(0, 80, 0);
      SpawnBurst(ParticleType::SPELL_METEOR, impactAbove, 25);
      SpawnBurst(ParticleType::SPELL_FIRE, impactAbove, 15);
      SpawnBurst(ParticleType::FLARE, impactAbove, 5);
      SpawnBurst(ParticleType::HIT_SPARK, m.position + glm::vec3(0, 30, 0), 10);
    }
  }
  m_meteorBolts.erase(
      std::remove_if(m_meteorBolts.begin(), m_meteorBolts.end(),
                     [](const MeteorBolt &m) {
                       return m.impacted && m.impactTimer > 0.5f;
                     }),
      m_meteorBolts.end());
}

void VFXManager::renderMeteorBolts(const glm::mat4 &view,
                                    const glm::mat4 &projection) {
  if (m_meteorBolts.empty() || m_fireMeshes.empty() || !m_modelShader)
    return;

  glm::mat4 invView = glm::inverse(view);

  glEnable(GL_BLEND);
  glDisable(GL_CULL_FACE);

  m_modelShader->use();
  m_modelShader->setMat4("view", view);
  m_modelShader->setMat4("projection", projection);
  m_modelShader->setFloat("luminosity", 1.0f);
  m_modelShader->setInt("numPointLights", 0);
  m_modelShader->setBool("useFog", false);
  m_modelShader->setVec2("texCoordOffset", glm::vec2(0.0f));
  m_modelShader->setFloat("outlineOffset", 0.0f);
  m_modelShader->setVec3("lightColor", glm::vec3(1.0f));
  m_modelShader->setVec3("lightPos", glm::vec3(0, 5000, 0));
  m_modelShader->setVec3("viewPos", glm::vec3(invView[3]));
  m_modelShader->setVec3("terrainLight", glm::vec3(1.0f));

  // Main 5.2: BlendMesh=1 → mesh with Texture==1 renders ADDITIVE
  glBlendFunc(GL_ONE, GL_ONE);
  glDepthMask(GL_FALSE);

  for (const auto &m : m_meteorBolts) {
    if (m.impacted)
      continue;

    float alpha = std::min(1.0f, m.lifetime / m.maxLifetime * 4.0f);
    m_modelShader->setFloat("objectAlpha", alpha);

    // Main 5.2: BlendMeshLight flickers 0.4-0.7
    float blendLight = (float)(rand() % 4 + 4) * 0.1f;
    m_modelShader->setFloat("blendMeshLight", blendLight);

    // Main 5.2: Angle=(0, 20, 0) — standard BMD orientation + 20 deg tilt
    glm::mat4 model = glm::translate(glm::mat4(1.0f), m.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(20.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(m.scale));
    m_modelShader->setMat4("model", model);

    for (const auto &mb : m_fireMeshes) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }

  glDepthMask(GL_TRUE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
}

void VFXManager::updateRibbon(Ribbon &r, float dt) {
  // Steer head toward target (Main 5.2: MoveHumming with max turn = 50 deg)
  glm::vec3 toTarget = r.targetPos - r.headPos;
  float dist = glm::length(toTarget);

  if (dist > 1.0f) {
    glm::vec3 desiredDir = toTarget / dist;
    float desiredYaw = std::atan2(desiredDir.x, desiredDir.z);
    float desiredPitch = std::asin(glm::clamp(desiredDir.y, -1.0f, 1.0f));

    // Max turn rate: 50 degrees/tick * 25 fps = 1250 deg/sec
    float maxTurn = 1250.0f * 3.14159f / 180.0f * dt;

    // Steer yaw
    float yawDiff = desiredYaw - r.headYaw;
    // Normalize to [-pi, pi]
    while (yawDiff > 3.14159f)
      yawDiff -= 2.0f * 3.14159f;
    while (yawDiff < -3.14159f)
      yawDiff += 2.0f * 3.14159f;
    r.headYaw += glm::clamp(yawDiff, -maxTurn, maxTurn);

    // Steer pitch
    float pitchDiff = desiredPitch - r.headPitch;
    r.headPitch += glm::clamp(pitchDiff, -maxTurn, maxTurn);
  }

  // Add random jitter (Main 5.2: rand()%256 - 128 on X and Z per tick)
  float jitterScale = dt * 25.0f; // Scale to tick rate
  float jitterX = ((float)(rand() % 256) - 128.0f) * jitterScale;
  float jitterZ = ((float)(rand() % 256) - 128.0f) * jitterScale;

  // Compute forward direction from yaw/pitch + jitter
  float cy = std::cos(r.headYaw), sy = std::sin(r.headYaw);
  float cp = std::cos(r.headPitch), sp = std::sin(r.headPitch);
  glm::vec3 forward(sy * cp, sp, cy * cp);

  // Move head forward
  r.headPos += forward * r.velocity * dt;
  r.headPos.x += jitterX;
  r.headPos.z += jitterZ;

  // Scroll UV (Main 5.2: WorldTime % 1000 / 1000)
  r.uvScroll += dt;

  // Build cross-section at head position
  // Main 5.2: 4 corners at ±Scale/2 in local X and Z, rotated by heading
  glm::vec3 right(cy, 0.0f, -sy); // Perpendicular to forward in XZ
  glm::vec3 up(0.0f, 1.0f, 0.0f); // Vertical

  RibbonSegment seg;
  seg.center = r.headPos;
  seg.right = right * (r.scale * 0.5f);
  seg.up = up * (r.scale * 0.5f);

  // Prepend new segment (newest at front)
  r.segments.insert(r.segments.begin(), seg);
  if ((int)r.segments.size() > Ribbon::MAX_SEGMENTS)
    r.segments.resize(Ribbon::MAX_SEGMENTS);

  // Decrease lifetime
  r.lifetime -= dt;
}

void VFXManager::updateSpellProjectiles(float dt) {
  for (int i = (int)m_spellProjectiles.size() - 1; i >= 0; --i) {
    auto &p = m_spellProjectiles[i];
    p.lifetime -= dt;

    // Impact when close to target (XZ distance)
    glm::vec3 toTarget = p.target - p.position;
    float distXZ = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    if (p.lifetime <= 0.0f || distXZ <= 30.0f) {
      // Impact VFX at TARGET position (monster), not projectile position
      glm::vec3 impactPos = p.target;
      // Impact energy burst — orbs expanding outward
      for (int j = 0; j < 10; j++) {
        Particle spark;
        spark.type = ParticleType::SPELL_ENERGY_ORB;
        spark.position = impactPos;
        float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
        float speed = 60.0f + (float)(rand() % 150);
        spark.velocity =
            glm::vec3(std::cos(angle) * speed, 60.0f + (float)(rand() % 120),
                      std::sin(angle) * speed);
        spark.scale = 35.0f + (float)(rand() % 30);
        spark.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
        spark.frame = -1.0f;
        spark.lifetime = 0.35f + (float)(rand() % 15) * 0.01f;
        spark.maxLifetime = spark.lifetime;
        spark.color = p.color;
        spark.alpha = 1.0f;
        m_particles.push_back(spark);
      }
      // Bright impact flash — large FLARE at monster position
      {
        Particle flash;
        flash.type = ParticleType::FLARE;
        flash.position = impactPos;
        flash.velocity = glm::vec3(0);
        flash.scale = 150.0f;
        flash.rotation = 0.0f;
        flash.frame = -1.0f;
        flash.lifetime = 0.3f;
        flash.maxLifetime = 0.3f;
        flash.color = p.color;
        flash.alpha = 1.0f;
        m_particles.push_back(flash);
      }
      // Secondary smaller flash with offset rotation
      {
        Particle flash2;
        flash2.type = ParticleType::FLARE;
        flash2.position = impactPos;
        flash2.velocity = glm::vec3(0);
        flash2.scale = 100.0f;
        flash2.rotation = 0.785f; // 45 degrees
        flash2.frame = -1.0f;
        flash2.lifetime = 0.2f;
        flash2.maxLifetime = 0.2f;
        flash2.color = glm::vec3(0.6f, 0.8f, 1.0f); // Brighter blue-white
        flash2.alpha = 1.0f;
        m_particles.push_back(flash2);
      }
      m_spellProjectiles[i] = m_spellProjectiles.back();
      m_spellProjectiles.pop_back();
      continue;
    }

    // Move toward target
    p.position += p.direction * p.speed * dt;

    // Visual spin rotation
    p.rotation += p.rotSpeed * dt;

    // Main 5.2: Luminosity = LifeTime * 0.2 (fades from ~4.0 to 0.0 over 20 ticks)
    float ticksRemaining = p.lifetime * 25.0f;
    if (p.skillId == 4) {
      // Fire Ball 3D model: stay bright for most of flight, quick fade at end
      float t = p.lifetime / p.maxLifetime; // 1.0 at start, 0.0 at end
      p.alpha = t < 0.1f ? t * 10.0f : 1.0f;
    } else {
      float luminosity = ticksRemaining * 0.2f;
      p.alpha = std::min(luminosity, 1.0f);
    }

    // Trail particles — behavior depends on whether we have a 3D model core
    bool has3DModel = (p.skillId == 4 && !m_fireMeshes.empty() && m_modelShader);
    glm::vec3 projVel = p.direction * p.speed;
    // For 3D model: backward drift so particles trail behind the fire ball
    glm::vec3 trailDrift = has3DModel ? -p.direction * (p.speed * 0.15f)
                                       : projVel; // billboard: match projectile

    p.trailTimer += dt;
    if (p.trailTimer >= 0.04f && m_particles.size() < (size_t)MAX_PARTICLES - 4) {
      p.trailTimer -= 0.04f;

      if (has3DModel) {
        // --- 3D model trail: particles stay behind, forming a fire tail ---

        // 1) Fire glow left behind — fades in place
        {
          Particle glow;
          glow.type = ParticleType::SPELL_FIRE;
          glow.position = p.position;
          glow.velocity = trailDrift + glm::vec3(0, 30.0f + (float)(rand() % 30), 0);
          glow.scale = 40.0f + (float)(rand() % 20);
          glow.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          glow.frame = -1.0f;
          glow.lifetime = 0.25f + (float)(rand() % 10) * 0.01f;
          glow.maxLifetime = glow.lifetime;
          glow.color = p.color;
          glow.alpha = 0.9f;
          m_particles.push_back(glow);
        }

        // 2) Bright core ember left behind
        {
          Particle ember;
          ember.type = ParticleType::FLARE;
          ember.position = p.position;
          ember.velocity = trailDrift * 0.5f;
          ember.scale = 50.0f + (float)(rand() % 20);
          ember.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          ember.frame = -1.0f;
          ember.lifetime = 0.18f;
          ember.maxLifetime = 0.18f;
          ember.color = p.color;
          ember.alpha = 0.7f;
          m_particles.push_back(ember);
        }

        // 3) Trailing spark — drifts outward from path
        {
          Particle sp;
          sp.type = ParticleType::HIT_SPARK;
          sp.position = p.position;
          float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
          float speed = 20.0f + (float)(rand() % 40);
          sp.velocity = glm::vec3(std::cos(angle) * speed,
                                  30.0f + (float)(rand() % 40),
                                  std::sin(angle) * speed);
          sp.scale = 15.0f + (float)(rand() % 10);
          sp.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          sp.frame = -1.0f;
          sp.lifetime = 0.30f;
          sp.maxLifetime = sp.lifetime;
          sp.color = p.color;
          sp.alpha = 1.0f;
          m_particles.push_back(sp);
        }
      } else {
        // --- Billboard-only trail (Energy Ball, fallback) ---

        // 1) Bright center glow — FLARE (core of the ball)
        {
          Particle glow;
          glow.type = ParticleType::FLARE;
          glow.position = p.position;
          glow.velocity = projVel;
          glow.scale = 70.0f;
          glow.rotation = p.rotation;
          glow.frame = -1.0f;
          glow.lifetime = 0.15f;
          glow.maxLifetime = 0.15f;
          glow.color = glm::vec3(0.5f, 0.7f, 1.0f);
          glow.alpha = 1.0f;
          m_particles.push_back(glow);
        }

        // 2) Thunder01 energy overlay — rotating, full UV
        {
          Particle orb;
          orb.type = ParticleType::SPELL_ENERGY_ORB;
          orb.position = p.position;
          orb.velocity = projVel;
          orb.scale = 80.0f * ((float)(rand() % 8 + 6) * 0.1f);
          orb.rotation = p.rotation;
          orb.frame = -1.0f;
          orb.lifetime = 0.20f;
          orb.maxLifetime = 0.20f;
          orb.color = p.color;
          orb.alpha = 1.0f;
          m_particles.push_back(orb);
        }

        // 3) Second energy overlay at 90-degree offset
        {
          Particle orb2;
          orb2.type = ParticleType::SPELL_ENERGY_ORB;
          orb2.position = p.position;
          orb2.velocity = projVel * 0.8f;
          orb2.scale = 60.0f * ((float)(rand() % 8 + 6) * 0.1f);
          orb2.rotation = p.rotation + 1.57f;
          orb2.frame = -1.0f;
          orb2.lifetime = 0.25f;
          orb2.maxLifetime = 0.25f;
          orb2.color = p.color;
          orb2.alpha = 0.8f;
          m_particles.push_back(orb2);
        }

        // 4) Trailing spark
        {
          Particle sp;
          sp.type = ParticleType::HIT_SPARK;
          sp.position = p.position;
          float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
          float speed = 30.0f + (float)(rand() % 60);
          sp.velocity = glm::vec3(std::cos(angle) * speed,
                                  40.0f + (float)(rand() % 40),
                                  std::sin(angle) * speed);
          sp.scale = 20.0f;
          sp.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          sp.frame = -1.0f;
          sp.lifetime = 0.30f;
          sp.maxLifetime = sp.lifetime;
          sp.color = p.color;
          sp.alpha = 1.0f;
          m_particles.push_back(sp);
        }
      }
    }
  }
}

void VFXManager::Update(float deltaTime) {
  // Update particles
  for (int i = (int)m_particles.size() - 1; i >= 0; --i) {
    auto &p = m_particles[i];
    p.lifetime -= deltaTime;
    if (p.lifetime <= 0.0f) {
      m_particles[i] = m_particles.back();
      m_particles.pop_back();
      continue;
    }

    p.position += p.velocity * deltaTime;

    switch (p.type) {
    case ParticleType::BLOOD:
      // Main 5.2: gravity pull, slight shrink
      p.velocity.y -= 400.0f * deltaTime;
      p.scale *= (1.0f - 0.5f * deltaTime);
      break;
    case ParticleType::HIT_SPARK:
      // Main 5.2: BITMAP_SPARK — gravity 6-22 units/frame² (avg ~350/s²)
      // Sparks arc outward and fall, slight scale shrink
      p.velocity.y -= 400.0f * deltaTime;
      p.scale *= (1.0f - 1.0f * deltaTime);
      break;
    case ParticleType::SMOKE:
      p.velocity *= (1.0f - 1.5f * deltaTime); // Slow deceleration
      p.scale *= (1.0f + 0.3f * deltaTime);    // Expand as it rises
      break;
    case ParticleType::FIRE:
      // Main 5.2: BITMAP_FIRE_CURSEDLICH — gravity 0.15-0.30, updraft
      p.velocity.y += 20.0f * deltaTime;
      p.velocity *= (1.0f - 3.0f * deltaTime);
      p.scale *= (1.0f - 1.5f * deltaTime);
      break;
    case ParticleType::ENERGY:
      p.velocity *= (1.0f - 5.0f * deltaTime);
      p.scale *= (1.0f - 2.0f * deltaTime);
      break;
    case ParticleType::FLARE:
      // Main 5.2: BITMAP_FLASH — stationary, rapid scale shrink + alpha fade
      p.scale *= (1.0f - 3.0f * deltaTime);
      break;
    case ParticleType::LEVEL_FLARE:
      // Main 5.2: BITMAP_FLARE level-up — gentle rise, slow fade
      p.velocity.y += 10.0f * deltaTime; // Slight updraft
      p.velocity.x *= (1.0f - 1.0f * deltaTime);
      p.velocity.z *= (1.0f - 1.0f * deltaTime);
      // Grow slightly in first half, then shrink
      if (p.lifetime > p.maxLifetime * 0.5f)
        p.scale *= (1.0f + 0.5f * deltaTime);
      else
        p.scale *= (1.0f - 1.5f * deltaTime);
      break;
    case ParticleType::SKILL_SLASH:
      // Fast horizontal spread with gravity, quick shrink
      p.velocity.y -= 300.0f * deltaTime;
      p.scale *= (1.0f - 2.0f * deltaTime);
      break;
    case ParticleType::SKILL_CYCLONE:
      // Orbital motion: slight centripetal + updraft
      p.velocity.y += 15.0f * deltaTime;
      p.velocity.x *= (1.0f - 2.0f * deltaTime);
      p.velocity.z *= (1.0f - 2.0f * deltaTime);
      p.scale *= (1.0f - 1.0f * deltaTime);
      break;
    case ParticleType::SKILL_FURY:
      // Strong gravity pull, large particles fall back down
      p.velocity.y -= 500.0f * deltaTime;
      p.scale *= (1.0f - 0.8f * deltaTime);
      break;
    case ParticleType::SKILL_STAB:
      // Fast directional, rapid fade, slight gravity
      p.velocity.y -= 150.0f * deltaTime;
      p.velocity *= (1.0f - 2.0f * deltaTime);
      p.scale *= (1.0f - 3.0f * deltaTime);
      break;
    // DW spell update behaviors
    case ParticleType::SPELL_ENERGY:
      // Main 5.2: Gravity=20 is ROTATION speed (Rotation += Gravity per tick)
      // Not actual gravity — particles drift with initial velocity and spin
      p.rotation += 500.0f * deltaTime; // 20 deg/tick × 25fps = 500 deg/sec
      p.velocity *= (1.0f - 2.0f * deltaTime);
      p.scale *= (1.0f - 1.2f * deltaTime);
      break;
    case ParticleType::SPELL_FIRE:
      // Strong updraft, shrink over time
      p.velocity.y += 30.0f * deltaTime;
      p.velocity.x *= (1.0f - 1.0f * deltaTime);
      p.velocity.z *= (1.0f - 1.0f * deltaTime);
      p.scale *= (1.0f - 1.0f * deltaTime);
      break;
    case ParticleType::SPELL_ICE:
      // Moderate gravity, fast shrink (sharp shards)
      p.velocity.y -= 200.0f * deltaTime;
      p.scale *= (1.0f - 2.5f * deltaTime);
      break;
    case ParticleType::SPELL_LIGHTNING:
      // Very fast decay, erratic (already short lifetime)
      p.velocity *= (1.0f - 4.0f * deltaTime);
      p.scale *= (1.0f - 3.0f * deltaTime);
      break;
    case ParticleType::SPELL_POISON:
      // Slow drift upward, grow slightly then fade
      p.velocity.y += 5.0f * deltaTime;
      p.velocity.x *= (1.0f - 0.5f * deltaTime);
      p.velocity.z *= (1.0f - 0.5f * deltaTime);
      if (p.lifetime > p.maxLifetime * 0.5f)
        p.scale *= (1.0f + 0.5f * deltaTime);
      else
        p.scale *= (1.0f - 1.5f * deltaTime);
      break;
    case ParticleType::SPELL_METEOR:
      // Strong gravity (falling effect), moderate drag
      p.velocity.y -= 400.0f * deltaTime;
      p.scale *= (1.0f - 0.8f * deltaTime);
      break;
    case ParticleType::SPELL_DARK:
      // Orbital swirl, gentle rise
      p.velocity.y += 10.0f * deltaTime;
      p.velocity.x *= (1.0f - 1.5f * deltaTime);
      p.velocity.z *= (1.0f - 1.5f * deltaTime);
      p.scale *= (1.0f - 1.0f * deltaTime);
      break;
    case ParticleType::SPELL_WATER:
      // Gravity pull, moderate shrink
      p.velocity.y -= 180.0f * deltaTime;
      p.velocity.x *= (1.0f - 1.0f * deltaTime);
      p.velocity.z *= (1.0f - 1.0f * deltaTime);
      p.scale *= (1.0f - 1.5f * deltaTime);
      break;
    case ParticleType::SPELL_TELEPORT:
      // Fast rise, expand outward then fade
      p.velocity.y += 40.0f * deltaTime;
      p.velocity.x *= (1.0f - 2.0f * deltaTime);
      p.velocity.z *= (1.0f - 2.0f * deltaTime);
      p.scale *= (1.0f - 1.5f * deltaTime);
      break;
    case ParticleType::SPELL_ENERGY_ORB:
      // Main 5.2: BITMAP_ENERGY — Gravity=20 is rotation speed
      // No velocity decay: core orb particles must track projectile at full speed
      p.rotation += 500.0f * deltaTime;
      break;
    }

    p.alpha = p.lifetime / p.maxLifetime;
  }

  // Update spell projectiles
  updateSpellProjectiles(deltaTime);

  // Update lightning sky-strike bolts
  updateLightningBolts(deltaTime);
  // Update meteor fireballs
  updateMeteorBolts(deltaTime);

  // Update ribbons
  for (int i = (int)m_ribbons.size() - 1; i >= 0; --i) {
    updateRibbon(m_ribbons[i], deltaTime);
    if (m_ribbons[i].lifetime <= 0.0f) {
      m_ribbons[i] = std::move(m_ribbons.back());
      m_ribbons.pop_back();
    }
  }

  // Update level-up orbiting sprite effects (tick-based, Main 5.2: 25fps)
  for (int i = (int)m_levelUpEffects.size() - 1; i >= 0; --i) {
    auto &effect = m_levelUpEffects[i];
    effect.tickAccum += deltaTime * 25.0f; // Convert to ticks

    // Process whole ticks
    while (effect.tickAccum >= 1.0f && effect.lifeTime > 0) {
      effect.tickAccum -= 1.0f;
      effect.lifeTime--;

      for (auto &sp : effect.sprites) {
        // Main 5.2: count = (Direction[1] + LifeTime) / PKKey, PKKey=2
        float count = (sp.phase + (float)effect.lifeTime) / 2.0f;
        float ox = std::cos(count) * effect.radius;
        float oz = -std::sin(count) * effect.radius;
        sp.height += sp.riseSpeed; // Direction[2] per tick

        glm::vec3 pos = effect.center + glm::vec3(ox, sp.height, oz);

        // Shift tails down (Main 5.2: CreateTail shifts array)
        int maxT = LEVEL_UP_MAX_TAILS;
        if (sp.numTails < maxT)
          sp.numTails++;
        for (int t = sp.numTails - 1; t > 0; --t)
          sp.tails[t] = sp.tails[t - 1];
        sp.tails[0] = pos;
      }
    }

    if (effect.lifeTime <= 0) {
      m_levelUpEffects[i] = std::move(m_levelUpEffects.back());
      m_levelUpEffects.pop_back();
    }
  }

  // Update ground circles (Main 5.2: spinning magic decals)
  for (int i = (int)m_groundCircles.size() - 1; i >= 0; --i) {
    m_groundCircles[i].lifetime -= deltaTime;
    if (m_groundCircles[i].lifetime <= 0.0f) {
      m_groundCircles[i] = m_groundCircles.back();
      m_groundCircles.pop_back();
      continue;
    }
    // Main 5.2: rotation ~3 rad/sec
    m_groundCircles[i].rotation += 3.0f * deltaTime;
  }

  // Update poison clouds
  updatePoisonClouds(deltaTime);
}

void VFXManager::renderRibbons(const glm::mat4 &view,
                               const glm::mat4 &projection) {
  if (m_ribbons.empty() || !m_lineShader)
    return;

  m_lineShader->use();
  m_lineShader->setMat4("view", view);
  m_lineShader->setMat4("projection", projection);

  // Bind JointThunder01 texture
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_lightningTexture);
  m_lineShader->setInt("ribbonTex", 0);
  m_lineShader->setBool("useTexture", m_lightningTexture != 0);

  // Additive blend (Main 5.2: glBlendFunc(GL_ONE, GL_ONE))
  glBlendFunc(GL_ONE, GL_ONE);

  glBindVertexArray(m_ribbonVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVBO);
  glDisable(GL_CULL_FACE);

  // Draw each ribbon separately with its own color/alpha (fixes per-ribbon
  // color bug)
  for (const auto &r : m_ribbons) {
    if (r.segments.size() < 2)
      continue;

    // Main 5.2: Thunder light flicker — Vector(0.1f + rand()/15.f, ...)
    float flicker = 0.7f + 0.3f * ((float)(rand() % 100) / 100.0f);
    glm::vec3 flickerColor = r.color * flicker;
    float ribbonAlpha = r.lifetime / r.maxLifetime;

    m_lineShader->setVec3("color", flickerColor);
    m_lineShader->setFloat("alpha", ribbonAlpha);

    std::vector<RibbonVertex> verts;
    verts.reserve(Ribbon::MAX_SEGMENTS * 12);

    float uvScroll = std::fmod(r.uvScroll, 1.0f);

    // Draw two faces per segment pair (+ cross-section, Main 5.2 pattern)
    for (int j = 0; j < (int)r.segments.size() - 1; ++j) {
      const auto &s0 = r.segments[j];
      const auto &s1 = r.segments[j + 1];

      // UV along ribbon: 0..2 range, scrolling
      float u0 =
          ((float)(r.segments.size() - j) / (float)(Ribbon::MAX_SEGMENTS - 1)) *
              2.0f -
          uvScroll;
      float u1 = ((float)(r.segments.size() - (j + 1)) /
                  (float)(Ribbon::MAX_SEGMENTS - 1)) *
                     2.0f -
                 uvScroll;

      // Face 1: horizontal (using right offsets) — GL_TRIANGLE_STRIP as 2 tris
      RibbonVertex v;

      // Quad: s0-right, s0+right, s1-right, s1+right → 2 triangles
      // Tri 1
      v.pos = s0.center - s0.right;
      v.uv = glm::vec2(u0, 0.0f);
      verts.push_back(v);
      v.pos = s0.center + s0.right;
      v.uv = glm::vec2(u0, 1.0f);
      verts.push_back(v);
      v.pos = s1.center + s1.right;
      v.uv = glm::vec2(u1, 1.0f);
      verts.push_back(v);
      // Tri 2
      v.pos = s0.center - s0.right;
      v.uv = glm::vec2(u0, 0.0f);
      verts.push_back(v);
      v.pos = s1.center + s1.right;
      v.uv = glm::vec2(u1, 1.0f);
      verts.push_back(v);
      v.pos = s1.center - s1.right;
      v.uv = glm::vec2(u1, 0.0f);
      verts.push_back(v);

      // Face 2: vertical (using up offsets) — offset UV for visual variety
      float u0b = u0 + uvScroll * 2.0f;
      float u1b = u1 + uvScroll * 2.0f;

      // Tri 1
      v.pos = s0.center - s0.up;
      v.uv = glm::vec2(u0b, 0.0f);
      verts.push_back(v);
      v.pos = s0.center + s0.up;
      v.uv = glm::vec2(u0b, 1.0f);
      verts.push_back(v);
      v.pos = s1.center + s1.up;
      v.uv = glm::vec2(u1b, 1.0f);
      verts.push_back(v);
      // Tri 2
      v.pos = s0.center - s0.up;
      v.uv = glm::vec2(u0b, 0.0f);
      verts.push_back(v);
      v.pos = s1.center + s1.up;
      v.uv = glm::vec2(u1b, 1.0f);
      verts.push_back(v);
      v.pos = s1.center - s1.up;
      v.uv = glm::vec2(u1b, 0.0f);
      verts.push_back(v);
    }

    if (verts.empty())
      continue;

    // Clamp to buffer size
    if ((int)verts.size() > MAX_RIBBON_VERTS)
      verts.resize(MAX_RIBBON_VERTS);

    glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(RibbonVertex),
                    verts.data());
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
  }

  glEnable(GL_CULL_FACE);
}

void VFXManager::Render(const glm::mat4 &view, const glm::mat4 &projection) {
  if (!m_shader)
    return;

  m_shader->use();
  m_shader->setMat4("view", view);
  m_shader->setMat4("projection", projection);

  glEnable(GL_BLEND);
  glDepthMask(GL_FALSE);

  // Batch draw helper for billboard particles
  auto drawBatch = [&](ParticleType type, GLuint texture) {
    if (texture == 0)
      return;
    std::vector<InstanceData> data;
    for (const auto &p : m_particles) {
      if (p.type == type) {
        InstanceData d;
        d.worldPos = p.position;
        d.scale = p.scale;
        d.rotation = p.rotation;
        d.frame = p.frame;
        d.color = p.color;
        d.alpha = p.alpha;
        data.push_back(d);
        if (data.size() >= (size_t)MAX_PARTICLES)
          break;
      }
    }
    if (data.empty())
      return;

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, data.size() * sizeof(InstanceData),
                    data.data());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    m_shader->setInt("fireTexture", 0);

    glBindVertexArray(m_quadVAO);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0,
                            (GLsizei)data.size());
  };

  // Normal alpha blend particles
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  drawBatch(ParticleType::BLOOD, m_bloodTexture);
  drawBatch(ParticleType::SMOKE, m_smokeTexture);
  drawBatch(ParticleType::SPELL_POISON,
            m_smokeTexture ? m_smokeTexture : m_flareTexture);

  // Additive blend particles
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  drawBatch(ParticleType::HIT_SPARK,
            m_sparkTexture ? m_sparkTexture : m_hitTexture);
  drawBatch(ParticleType::FIRE, m_fireTexture);
  drawBatch(ParticleType::ENERGY, m_energyTexture);
  drawBatch(ParticleType::FLARE,
            m_flareTexture ? m_flareTexture : m_hitTexture);

  // DK Skill effect particles (additive)
  drawBatch(ParticleType::SKILL_SLASH,
            m_sparkTexture ? m_sparkTexture : m_hitTexture);
  drawBatch(ParticleType::SKILL_CYCLONE,
            m_energyTexture ? m_energyTexture : m_sparkTexture);
  drawBatch(ParticleType::SKILL_FURY,
            m_flareTexture ? m_flareTexture : m_hitTexture);
  drawBatch(ParticleType::SKILL_STAB,
            m_sparkTexture ? m_sparkTexture : m_hitTexture);

  // DW Spell effect particles (additive)
  drawBatch(ParticleType::SPELL_ENERGY,
            m_energyTexture ? m_energyTexture : m_flareTexture);
  drawBatch(ParticleType::SPELL_FIRE,
            m_fireTexture ? m_fireTexture : m_flareTexture);
  drawBatch(ParticleType::SPELL_ICE,
            m_sparkTexture ? m_sparkTexture : m_flareTexture);
  drawBatch(ParticleType::SPELL_LIGHTNING,
            m_sparkTexture ? m_sparkTexture : m_flareTexture);
  drawBatch(ParticleType::SPELL_METEOR,
            m_flareTexture ? m_flareTexture : m_hitTexture);
  drawBatch(ParticleType::SPELL_DARK,
            m_energyTexture ? m_energyTexture : m_flareTexture);
  drawBatch(ParticleType::SPELL_WATER,
            m_energyTexture ? m_energyTexture : m_flareTexture);
  drawBatch(ParticleType::SPELL_TELEPORT,
            m_flareTexture ? m_flareTexture : m_hitTexture);

  // Main 5.2: BITMAP_ENERGY orb (Thunder01.jpg) — full-texture rotating glow
  drawBatch(ParticleType::SPELL_ENERGY_ORB,
            m_thunderTexture ? m_thunderTexture : m_energyTexture);

  // Spell projectile 3D models (Fire Ball: Fire01.bmd) + billboard fallback
  // Trail particles already rendered via drawBatch above.
  glDepthMask(GL_TRUE); // Re-enable depth for 3D model rendering
  renderSpellProjectiles(view, projection);
  renderLightningBolts(view, projection);
  renderMeteorBolts(view, projection);
  renderPoisonClouds(view, projection);
  glDepthMask(GL_FALSE); // Back to billboard mode for remaining effects

  // Render level-up orbiting flares (Main 5.2: 15 BITMAP_FLARE joints)
  renderLevelUpEffects(view, projection);

  // Render ground circles (Main 5.2: BITMAP_MAGIC level-up decal)
  renderGroundCircles(view, projection);

  // Render textured ribbons (Lich Joint Thunder)
  renderRibbons(view, projection);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_TRUE);
}

void VFXManager::UpdateLevelUpCenter(const glm::vec3 &position) {
  for (auto &effect : m_levelUpEffects) {
    glm::vec3 delta = position - effect.center;
    effect.center = position;
    // Shift all existing tail positions so the trail follows the character
    for (auto &sp : effect.sprites) {
      for (int t = 0; t < sp.numTails; ++t)
        sp.tails[t] += delta;
    }
  }
  // Ground circles also follow the character
  for (auto &gc : m_groundCircles) {
    gc.position = position;
  }
}

void VFXManager::SpawnLevelUpEffect(const glm::vec3 &position) {
  // Main 5.2 WSclient.cpp: 15 CreateJoint(BITMAP_FLARE, ..., 0, Target, 40, 2)
  // ZzzEffectJoint.cpp SubType=0: random phase, random upward speed, orbit=40

  LevelUpEffect effect;
  effect.center = position;
  effect.lifeTime = 50;        // Main 5.2: LifeTime = 50 (when Scale > 10)
  effect.tickAccum = 0.0f;
  effect.radius = 40.0f;       // Main 5.2: Velocity = 40
  effect.spriteScale = 40.0f;  // Main 5.2: Scale = 40

  // 15 sprites with random phases and rise speeds (Main 5.2)
  for (int i = 0; i < 15; ++i) {
    LevelUpSprite sp;
    sp.phase = (float)(rand() % 500 - 250); // Main 5.2: Direction[1]
    // Main 5.2: When Scale > 10: Direction[2] = (rand()%250+200)/100.f = 2.0-4.49
    sp.riseSpeed = (float)(rand() % 250 + 200) / 100.0f;
    sp.height = 0.0f;
    sp.numTails = 0;
    effect.sprites.push_back(sp);
  }

  // Pre-process initial ticks so trails render immediately (no stutter)
  for (int t = 0; t < 4 && effect.lifeTime > 0; ++t) {
    effect.lifeTime--;
    for (auto &sp : effect.sprites) {
      float count = (sp.phase + (float)effect.lifeTime) / 2.0f;
      float ox = std::cos(count) * effect.radius;
      float oz = -std::sin(count) * effect.radius;
      sp.height += sp.riseSpeed;
      glm::vec3 pos = effect.center + glm::vec3(ox, sp.height, oz);
      if (sp.numTails < LEVEL_UP_MAX_TAILS)
        sp.numTails++;
      for (int j = sp.numTails - 1; j > 0; --j)
        sp.tails[j] = sp.tails[j - 1];
      sp.tails[0] = pos;
    }
  }

  m_levelUpEffects.push_back(std::move(effect));

  // Main 5.2: CreateEffect(BITMAP_MAGIC+1, ...) — ground magic circle
  GroundCircle gc;
  gc.position = position;
  gc.rotation = 0.0f;
  gc.maxLifetime = 2.0f; // Main 5.2: LifeTime=20 ticks at 25fps = 0.8s, extended for visual
  gc.lifetime = gc.maxLifetime;
  gc.color = glm::vec3(1.0f, 0.75f, 0.2f); // Golden-orange (regular level-up)
  m_groundCircles.push_back(gc);
}

void VFXManager::renderLevelUpEffects(const glm::mat4 &view,
                                      const glm::mat4 &projection) {
  if (m_levelUpEffects.empty())
    return;

  // ── Pass 1: Trail ribbons (line shader) ──────────────────────────────────
  if (m_lineShader) {
    m_lineShader->use();
    m_lineShader->setMat4("view", view);
    m_lineShader->setMat4("projection", projection);

    GLuint tex = m_bitmapFlareTexture ? m_bitmapFlareTexture : m_flareTexture;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    m_lineShader->setInt("ribbonTex", 0);
    m_lineShader->setBool("useTexture", true);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDisable(GL_CULL_FACE);

    glBindVertexArray(m_ribbonVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVBO);

    for (const auto &effect : m_levelUpEffects) {
      // Main 5.2: Light fades last 10 ticks (Light /= 1.3 per tick)
      float effectAlpha = 1.0f;
      if (effect.lifeTime < 10)
        effectAlpha = std::pow(1.0f / 1.3f, (float)(10 - effect.lifeTime));

      m_lineShader->setVec3("color", 1.0f, 0.85f, 0.35f); // Warm golden
      m_lineShader->setFloat("alpha", effectAlpha);

      float hw = effect.spriteScale * 0.5f; // Half-width = Scale/2 = 20

      for (const auto &sp : effect.sprites) {
        if (sp.numTails < 2)
          continue;

        // Sub-tick interpolation: compute smooth head position between ticks
        float frac = effect.tickAccum;
        float curCount = (sp.phase + (float)effect.lifeTime) / 2.0f;
        float nextCount = curCount - 0.5f; // Next tick angle
        float interpCount = curCount + (nextCount - curCount) * frac;
        float interpHeight = sp.height + sp.riseSpeed * frac;
        glm::vec3 interpHead =
            effect.center +
            glm::vec3(std::cos(interpCount) * effect.radius, interpHeight,
                      -std::sin(interpCount) * effect.radius);

        int nSegs = sp.numTails - 1;
        constexpr int MAX_VERTS = LEVEL_UP_MAX_TAILS * 12;
        RibbonVertex verts[MAX_VERTS];
        int nVerts = 0;

        int maxTails = LEVEL_UP_MAX_TAILS;
        for (int j = 0; j < nSegs && nVerts + 12 <= MAX_VERTS; ++j) {
          // Use interpolated head for newest segment
          glm::vec3 p0 = (j == 0) ? interpHead : sp.tails[j];
          glm::vec3 p1 = sp.tails[j + 1];

          // Main 5.2 UV: fades head→tail
          float L1 = (float)(sp.numTails - j) / (float)(maxTails - 1);
          float L2 = (float)(sp.numTails - (j + 1)) / (float)(maxTails - 1);

          // Trail tapering: full width at head, narrows to 30% at tail
          float taper0 = 0.3f + 0.7f * L1;
          float taper1 = 0.3f + 0.7f * L2;
          float hw0 = hw * taper0;
          float hw1 = hw * taper1;

          // Face 1 (horizontal): offset along world X
          verts[nVerts++] = {p0 + glm::vec3(-hw0, 0, 0), {L1, 0.0f}};
          verts[nVerts++] = {p0 + glm::vec3(+hw0, 0, 0), {L1, 1.0f}};
          verts[nVerts++] = {p1 + glm::vec3(+hw1, 0, 0), {L2, 1.0f}};

          verts[nVerts++] = {p0 + glm::vec3(-hw0, 0, 0), {L1, 0.0f}};
          verts[nVerts++] = {p1 + glm::vec3(+hw1, 0, 0), {L2, 1.0f}};
          verts[nVerts++] = {p1 + glm::vec3(-hw1, 0, 0), {L2, 0.0f}};

          // Face 2 (vertical): offset along world Y (up)
          verts[nVerts++] = {p0 + glm::vec3(0, -hw0, 0), {L1, 1.0f}};
          verts[nVerts++] = {p0 + glm::vec3(0, +hw0, 0), {L1, 0.0f}};
          verts[nVerts++] = {p1 + glm::vec3(0, +hw1, 0), {L2, 0.0f}};

          verts[nVerts++] = {p0 + glm::vec3(0, -hw0, 0), {L1, 1.0f}};
          verts[nVerts++] = {p1 + glm::vec3(0, +hw1, 0), {L2, 0.0f}};
          verts[nVerts++] = {p1 + glm::vec3(0, -hw1, 0), {L2, 1.0f}};
        }

        if (nVerts > 0) {
          glBufferSubData(GL_ARRAY_BUFFER, 0, nVerts * sizeof(RibbonVertex),
                          verts);
          glDrawArrays(GL_TRIANGLES, 0, nVerts);
        }
      }
    }
    glEnable(GL_CULL_FACE);
  }

  // ── Pass 2: Head glow billboards (billboard shader) ──────────────────────
  if (m_shader) {
    m_shader->use();
    m_shader->setMat4("view", view);
    m_shader->setMat4("projection", projection);

    GLuint tex = m_bitmapFlareTexture ? m_bitmapFlareTexture : m_flareTexture;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    m_shader->setInt("fireTexture", 0);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    std::vector<InstanceData> heads;
    for (const auto &effect : m_levelUpEffects) {
      float effectAlpha = 1.0f;
      if (effect.lifeTime < 10)
        effectAlpha = std::pow(1.0f / 1.3f, (float)(10 - effect.lifeTime));

      float frac = effect.tickAccum;
      for (const auto &sp : effect.sprites) {
        if (sp.numTails < 1)
          continue;
        // Interpolated head position for smooth glow
        float curCount = (sp.phase + (float)effect.lifeTime) / 2.0f;
        float interpCount = curCount - 0.5f * frac;
        float interpHeight = sp.height + sp.riseSpeed * frac;
        glm::vec3 headPos =
            effect.center +
            glm::vec3(std::cos(interpCount) * effect.radius, interpHeight,
                      -std::sin(interpCount) * effect.radius);

        InstanceData d;
        d.worldPos = headPos;
        d.scale = effect.spriteScale * 1.2f; // Slightly larger glow
        d.rotation = interpCount;             // Rotate with orbit
        d.frame = 0.0f;
        d.color = glm::vec3(1.0f, 0.9f, 0.5f); // Bright golden-white
        d.alpha = effectAlpha * 0.8f;
        heads.push_back(d);
      }
    }

    if (!heads.empty()) {
      glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
      glBufferSubData(GL_ARRAY_BUFFER, 0,
                      heads.size() * sizeof(InstanceData), heads.data());
      glBindVertexArray(m_quadVAO);
      glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0,
                              (GLsizei)heads.size());
    }
  }
}

void VFXManager::renderGroundCircles(const glm::mat4 &view,
                                     const glm::mat4 &projection) {
  if (m_groundCircles.empty() || !m_lineShader || m_magicGroundTexture == 0)
    return;

  m_lineShader->use();
  m_lineShader->setMat4("view", view);
  m_lineShader->setMat4("projection", projection);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_magicGroundTexture);
  m_lineShader->setInt("ribbonTex", 0);
  m_lineShader->setBool("useTexture", true);

  // Additive blend for magic glow
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glDisable(GL_CULL_FACE);

  glBindVertexArray(m_ribbonVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVBO);

  for (const auto &gc : m_groundCircles) {
    // Main 5.2: Scale = (20-LifeTime)*0.15 → 0..3 terrain cells → 0..300 units
    float t = 1.0f - gc.lifetime / gc.maxLifetime; // 0..1
    float halfSize = t * 150.0f; // Grows from 0 to 150 world units

    // Alpha: full brightness, fade in last 25% of lifetime
    float alpha = 1.0f;
    if (gc.lifetime < gc.maxLifetime * 0.25f)
      alpha = gc.lifetime / (gc.maxLifetime * 0.25f);

    m_lineShader->setVec3("color", gc.color);
    m_lineShader->setFloat("alpha", alpha);

    // Build XZ-plane quad rotated around Y axis at gc.position
    float c = std::cos(gc.rotation), s = std::sin(gc.rotation);
    // Rotated right and forward vectors in XZ plane
    glm::vec3 right(c * halfSize, 0.0f, s * halfSize);
    glm::vec3 fwd(-s * halfSize, 0.0f, c * halfSize);
    // Slight Y offset to avoid z-fighting with terrain
    glm::vec3 pos = gc.position + glm::vec3(0.0f, 2.0f, 0.0f);

    RibbonVertex verts[6];
    // Triangle 1
    verts[0].pos = pos - right - fwd;
    verts[0].uv = glm::vec2(0.0f, 0.0f);
    verts[1].pos = pos + right - fwd;
    verts[1].uv = glm::vec2(1.0f, 0.0f);
    verts[2].pos = pos + right + fwd;
    verts[2].uv = glm::vec2(1.0f, 1.0f);
    // Triangle 2
    verts[3].pos = pos - right - fwd;
    verts[3].uv = glm::vec2(0.0f, 0.0f);
    verts[4].pos = pos + right + fwd;
    verts[4].uv = glm::vec2(1.0f, 1.0f);
    verts[5].pos = pos - right + fwd;
    verts[5].uv = glm::vec2(0.0f, 1.0f);

    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
  }

  glEnable(GL_CULL_FACE);
}

void VFXManager::renderSpellProjectiles(const glm::mat4 &view,
                                        const glm::mat4 &projection) {
  if (m_spellProjectiles.empty())
    return;

  // Pass 1: Render 3D model fire balls (Main 5.2: MODEL_FIRE = Fire01.bmd)
  bool hasFireModel = !m_fireMeshes.empty() && m_modelShader;
  for (const auto &p : m_spellProjectiles) {
    if (p.skillId == 4 && hasFireModel) {
      renderFireModel(p, view, projection);
    }
  }

  // Pass 2: Billboard projectiles (non-fire-ball spells, or fallback)
  if (!m_shader)
    return;

  m_shader->use();
  m_shader->setMat4("view", view);
  m_shader->setMat4("projection", projection);

  // Render projectile orbs as large additive billboards
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);

  // Core glow pass — Main 5.2: BITMAP_ENERGY = Thunder01.jpg
  GLuint orbTex = m_thunderTexture ? m_thunderTexture
                  : (m_energyTexture ? m_energyTexture : m_flareTexture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, orbTex);
  m_shader->setInt("fireTexture", 0);

  std::vector<InstanceData> orbData;
  for (const auto &p : m_spellProjectiles) {
    // Skip fire balls that use 3D model
    if (p.skillId == 4 && hasFireModel)
      continue;
    InstanceData d;
    d.worldPos = p.position;
    d.scale = p.scale;
    d.rotation = p.rotation;
    d.frame = -1.0f; // Full texture UV (not sprite sheet)
    d.color = p.color;
    d.alpha = p.alpha * 0.9f;
    orbData.push_back(d);
  }

  if (!orbData.empty()) {
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    orbData.size() * sizeof(InstanceData), orbData.data());
    glBindVertexArray(m_quadVAO);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0,
                            (GLsizei)orbData.size());
  }

  // Outer halo pass (flare texture, larger, more transparent)
  GLuint haloTex = m_flareTexture ? m_flareTexture : m_energyTexture;
  glBindTexture(GL_TEXTURE_2D, haloTex);

  std::vector<InstanceData> haloData;
  for (const auto &p : m_spellProjectiles) {
    if (p.skillId == 4 && hasFireModel)
      continue;
    InstanceData d;
    d.worldPos = p.position;
    d.scale = p.scale * 1.8f; // Larger halo
    d.rotation = -p.rotation * 0.5f; // Counter-rotate for visual interest
    d.frame = -1.0f; // Full texture UV (not sprite sheet)
    d.color = p.color;
    d.alpha = p.alpha * 0.4f;
    haloData.push_back(d);
  }

  if (!haloData.empty()) {
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    haloData.size() * sizeof(InstanceData), haloData.data());
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0,
                            (GLsizei)haloData.size());
  }
}

void VFXManager::renderFireModel(const SpellProjectile &p,
                                  const glm::mat4 &view,
                                  const glm::mat4 &projection) {
  m_modelShader->use();
  m_modelShader->setMat4("view", view);
  m_modelShader->setMat4("projection", projection);
  m_modelShader->setFloat("luminosity", 1.0f); // Self-lit fire
  m_modelShader->setFloat("blendMeshLight", 1.0f);
  m_modelShader->setInt("numPointLights", 0);
  m_modelShader->setBool("useFog", false); // Fire doesn't fog out
  m_modelShader->setVec2("texCoordOffset", glm::vec2(0.0f));
  m_modelShader->setFloat("outlineOffset", 0.0f);
  m_modelShader->setVec3("lightColor", glm::vec3(1.0f));
  m_modelShader->setVec3("lightPos", glm::vec3(0, 5000, 0));
  // Extract camera position from view matrix
  glm::mat4 invView = glm::inverse(view);
  m_modelShader->setVec3("viewPos", glm::vec3(invView[3]));

  // Model matrix: translate → BMD base rotation → heading → spin → scale
  glm::mat4 model = glm::translate(glm::mat4(1.0f), p.position);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, p.yaw, glm::vec3(0, 0, 1));      // Heading
  model = glm::rotate(model, p.rotation, glm::vec3(0, 1, 0));  // Spin
  model = glm::scale(model, glm::vec3(p.scale));

  m_modelShader->setMat4("model", model);
  m_modelShader->setVec3("terrainLight", glm::vec3(1.0f)); // Self-lit
  m_modelShader->setFloat("objectAlpha", p.alpha);

  // Main 5.2: BlendMesh=1 — mesh 0 = solid fire, mesh 1 = additive glow
  // BlendMeshLight flicker: (rand()%4+4)*0.1f = 0.4-0.7 (Main 5.2 ZzzEffect.cpp)
  float glowIntensity = (float)(rand() % 4 + 4) * 0.1f;

  glEnable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  for (const auto &mb : m_fireMeshes) {
    if (mb.indexCount == 0 || mb.hidden)
      continue;
    bool isGlow = (mb.bmdTextureId == 1);
    if (isGlow) {
      m_modelShader->setFloat("blendMeshLight", glowIntensity);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Additive
      glDepthMask(GL_FALSE);
    } else {
      m_modelShader->setFloat("blendMeshLight", 1.0f);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glBindTexture(GL_TEXTURE_2D, mb.texture);
    glBindVertexArray(mb.vao);
    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    if (isGlow) {
      glDepthMask(GL_TRUE);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
  }
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
}

// Main 5.2: MODEL_SKILL_BLAST — render falling Blast01.bmd orbs + vertical beam
void VFXManager::renderLightningBolts(const glm::mat4 &view,
                                       const glm::mat4 &projection) {
  if (m_lightningBolts.empty() || !m_modelShader)
    return;

  bool hasBlastModel = !m_blastMeshes.empty();
  bool hasFireModel = !m_fireMeshes.empty();
  glm::mat4 invView = glm::inverse(view);

  glEnable(GL_BLEND);
  glDisable(GL_CULL_FACE);

  // Pass 1: Render 3D models (Blast01 for lightning, Fire01 for meteor)
  if (hasBlastModel || hasFireModel) {
    m_modelShader->use();
    m_modelShader->setMat4("view", view);
    m_modelShader->setMat4("projection", projection);
    m_modelShader->setFloat("luminosity", 1.0f);
    m_modelShader->setFloat("blendMeshLight", 1.0f);
    m_modelShader->setInt("numPointLights", 0);
    m_modelShader->setBool("useFog", false);
    m_modelShader->setVec2("texCoordOffset", glm::vec2(0.0f));
    m_modelShader->setFloat("outlineOffset", 0.0f);
    m_modelShader->setVec3("lightColor", glm::vec3(1.0f));
    m_modelShader->setVec3("lightPos", glm::vec3(0, 5000, 0));
    m_modelShader->setVec3("viewPos", glm::vec3(invView[3]));
    m_modelShader->setVec3("terrainLight", glm::vec3(1.0f));

    // Main 5.2: BlendMesh=0 → mesh with Texture==0 renders ADDITIVE
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);

    for (const auto &b : m_lightningBolts) {
      if (b.impacted)
        continue;

      const auto &meshes = m_blastMeshes;
      if (meshes.empty())
        continue;

      float alpha = std::min(1.0f, b.lifetime / b.maxLifetime * 4.0f);
      m_modelShader->setFloat("objectAlpha", alpha);

      // Lightning: standard BMD orientation + spin
      glm::mat4 model = glm::translate(glm::mat4(1.0f), b.position);
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      model = glm::rotate(model, b.rotation, glm::vec3(0, 1, 0));
      model = glm::scale(model, glm::vec3(b.scale));
      m_modelShader->setMat4("model", model);

      for (const auto &mb : meshes) {
        if (mb.indexCount == 0 || mb.hidden)
          continue;
        glBindTexture(GL_TEXTURE_2D, mb.texture);
        glBindVertexArray(mb.vao);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      }
    }
    glDepthMask(GL_TRUE);
  }

  // Pass 2: Energy trail (Main 5.2: BITMAP_JOINT_ENERGY SubType 5)
  // 10-segment trailing ribbon following the bolt, two cross-plane faces
  if (m_lightningTexture && m_ribbonVAO && m_lineShader) {
    m_lineShader->use();
    m_lineShader->setMat4("view", view);
    m_lineShader->setMat4("projection", projection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_lightningTexture);
    m_lineShader->setInt("ribbonTex", 0);
    m_lineShader->setBool("useTexture", true);

    glBlendFunc(GL_ONE, GL_ONE); // Additive
    glDepthMask(GL_FALSE);

    for (const auto &b : m_lightningBolts) {
      if (b.numTrail < 2)
        continue;

      float alpha = b.impacted
                        ? std::max(0.0f, 1.0f - b.impactTimer * 4.0f)
                        : std::min(1.0f, b.lifetime / b.maxLifetime * 4.0f);
      if (alpha <= 0.01f)
        continue;

      float flicker = 0.7f + 0.3f * ((float)(rand() % 100) / 100.0f);
      glm::vec3 trailColor = glm::vec3(0.4f, 0.6f, 1.0f);
      m_lineShader->setVec3("ribbonColor", trailColor * flicker);
      m_lineShader->setFloat("ribbonAlpha", alpha);

      // Build quad strips between consecutive trail positions
      // Two perpendicular faces (cross-plane) per segment, Scale=100 (±50)
      float hw = 50.0f; // Half-width

      // Collect all vertices for batch rendering
      std::vector<RibbonVertex> verts;
      verts.reserve((b.numTrail) * 8);

      for (int j = 0; j < b.numTrail - 1; ++j) {
        glm::vec3 p0 = b.trail[j];
        glm::vec3 p1 = b.trail[j + 1];

        // UV: fade from bright (newest) to dark (oldest)
        float u0 = (float)(b.numTrail - j) / (float)(LightningBolt::MAX_TRAIL);
        float u1 =
            (float)(b.numTrail - j - 1) / (float)(LightningBolt::MAX_TRAIL);

        // Direction between trail points for perpendicular orientation
        glm::vec3 seg = p1 - p0;
        float segLen = glm::length(seg);
        if (segLen < 0.01f)
          continue;
        glm::vec3 dir = seg / segLen;

        // Two perpendicular axes for cross-plane geometry (lightning only)
        glm::vec3 right =
            glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
        if (glm::length(glm::cross(dir, glm::vec3(0, 1, 0))) < 0.01f)
          right = glm::vec3(1, 0, 0);
        glm::vec3 up = glm::normalize(glm::cross(right, dir));

        // Face 1: horizontal cross-section (left/right)
        verts.push_back({p0 - right * hw, {u0, 0.0f}});
        verts.push_back({p0 + right * hw, {u0, 1.0f}});
        verts.push_back({p1 - right * hw, {u1, 0.0f}});
        verts.push_back({p1 + right * hw, {u1, 1.0f}});

        // Face 2: vertical cross-section (up/down)
        verts.push_back({p0 - up * hw, {u0, 0.0f}});
        verts.push_back({p0 + up * hw, {u0, 1.0f}});
        verts.push_back({p1 - up * hw, {u1, 0.0f}});
        verts.push_back({p1 + up * hw, {u1, 1.0f}});
      }

      if (!verts.empty()) {
        int totalVerts = (int)verts.size();
        int maxVerts = MAX_RIBBON_VERTS;
        if (totalVerts > maxVerts)
          totalVerts = maxVerts;

        glBindVertexArray(m_ribbonVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                         totalVerts * sizeof(RibbonVertex), verts.data());

        // Draw each quad as a triangle strip (4 verts per quad)
        for (int q = 0; q + 3 < totalVerts; q += 4) {
          glDrawArrays(GL_TRIANGLE_STRIP, q, 4);
        }
      }
    }

    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
}

// Main 5.2: AddTerrainLight — each spell projectile emits a dynamic point light
// Fire Ball: color (L*1.0, L*0.1, 0.0), range=300 world units (3 grid cells)
// Energy Ball: color (0.0, L*0.3, L*1.0), range=200 world units (2 grid cells)
// Other spells: color derived from projectile color, range=200
void VFXManager::GetActiveSpellLights(std::vector<glm::vec3> &positions,
                                      std::vector<glm::vec3> &colors,
                                      std::vector<float> &ranges,
                                      std::vector<int> &objectTypes) const {
  for (const auto &p : m_spellProjectiles) {
    if (p.alpha <= 0.01f)
      continue;

    // Main 5.2: Luminosity = (rand()%4+7)*0.1f = 0.7-1.0, flickering
    float L = 0.7f + (float)(rand() % 4) * 0.1f;
    L *= p.alpha; // Fade with projectile

    glm::vec3 lightColor;
    float lightRange;

    switch (p.skillId) {
    case 4: // Fire Ball — Main 5.2: (L*1.0, L*0.1, 0.0)
      lightColor = glm::vec3(L * 1.0f, L * 0.1f, 0.0f);
      lightRange = 300.0f; // 3 grid cells
      break;
    case 1: // Poison — green (no projectile, but keep for completeness)
      lightColor = glm::vec3(L * 0.3f, L * 1.0f, L * 0.6f);
      lightRange = 200.0f;
      break;
    case 3: // Lightning — blue-white (Main 5.2: L*0.2, L*0.4, L*1.0)
      lightColor = glm::vec3(L * 0.2f, L * 0.4f, L * 1.0f);
      lightRange = 200.0f;
      break;
    default: // Generic: use projectile color scaled by luminosity
      lightColor = p.color * L * 0.5f;
      lightRange = 200.0f;
      break;
    }

    positions.push_back(p.position);
    colors.push_back(lightColor);
    ranges.push_back(lightRange);
    objectTypes.push_back(-1); // -1 = spell light (no object type flicker)
  }

  // Ribbon lights (Lightning beams, Lich bolts)
  // Main 5.2: Lightning terrain light = (L*0.2, L*0.4, L*1.0), range=2 cells
  for (const auto &r : m_ribbons) {
    if (r.lifetime <= 0.0f || r.segments.empty())
      continue;
    float t = r.lifetime / r.maxLifetime;
    float L = (0.7f + (float)(rand() % 4) * 0.1f) * t;
    // Light at ribbon head position
    glm::vec3 lightColor = r.color * L;
    positions.push_back(r.headPos);
    colors.push_back(lightColor);
    ranges.push_back(200.0f);
    objectTypes.push_back(-1);
  }

  // Lightning sky-strike bolt lights
  // Main 5.2: AddTerrainLight(pos, (L*0.2, L*0.4, L*1.0), 2, PrimaryTerrainLight)
  for (const auto &b : m_lightningBolts) {
    float L = 0.7f + (float)(rand() % 4) * 0.1f;
    if (b.impacted)
      L *= std::max(0.0f, 1.0f - b.impactTimer * 4.0f); // Quick fade after impact
    if (L <= 0.01f)
      continue;
    positions.push_back(b.position);
    colors.push_back(glm::vec3(L * 0.2f, L * 0.4f, L * 1.0f));
    ranges.push_back(200.0f);
    objectTypes.push_back(-1);
  }

  // Poison cloud lights — Main 5.2 MoveEffect:
  // Luminosity = Luminosity (from o->Luminosity, set elsewhere as LifeTime*0.2f typically)
  // Vector(Luminosity*0.3f, Luminosity*1.f, Luminosity*0.6f, Light);
  // AddTerrainLight(pos, Light, 2, PrimaryTerrainLight);
  for (const auto &pc : m_poisonClouds) {
    float ticksRemaining = pc.lifetime / 0.04f;
    float L = ticksRemaining * 0.1f; // Matches BlendMeshLight fade
    if (L <= 0.01f)
      continue;
    L = std::min(L, 1.5f); // Clamp so it doesn't overpower
    positions.push_back(pc.position);
    colors.push_back(glm::vec3(L * 0.3f, L * 1.0f, L * 0.6f));
    ranges.push_back(200.0f); // range=2 grid cells
    objectTypes.push_back(-1);
  }
}

// Main 5.2: MODEL_POISON — spawn green cloud at target position
// ZzzCharacter.cpp AT_SKILL_POISON: CreateEffect(MODEL_POISON, to->Position, o->Angle, o->Light)
// + 10x BITMAP_SMOKE particles with light (0.4, 0.6, 1.0)
void VFXManager::SpawnPoisonCloud(const glm::vec3 &targetPos) {
  PoisonCloud pc;
  pc.position = targetPos;
  pc.rotation = 0.0f;      // Main 5.2: no rotation set
  pc.lifetime = 1.6f;      // 40 ticks @ 25fps
  pc.maxLifetime = 1.6f;
  pc.alpha = 1.0f;
  pc.scale = 1.0f;         // Main 5.2: Scale = 1.f
  m_poisonClouds.push_back(pc);

  // Main 5.2: 10x BITMAP_SMOKE at target, Light=(0.4, 0.6, 1.0), SubType=1
  SpawnBurst(ParticleType::SMOKE, targetPos + glm::vec3(0, 30, 0), 10);
}

void VFXManager::updatePoisonClouds(float dt) {
  for (int i = (int)m_poisonClouds.size() - 1; i >= 0; --i) {
    auto &pc = m_poisonClouds[i];
    pc.lifetime -= dt;
    if (pc.lifetime <= 0.0f) {
      m_poisonClouds[i] = m_poisonClouds.back();
      m_poisonClouds.pop_back();
      continue;
    }
    // Main 5.2 MoveEffect: Alpha = LifeTime * 0.1, BlendMeshLight = LifeTime * 0.1
    // LifeTime counts down from 40. Convert: ticksRemaining = lifetime / 0.04
    float ticksRemaining = pc.lifetime / 0.04f;
    pc.alpha = std::min(1.0f, ticksRemaining * 0.1f);
    // No rotation in Main 5.2 — poison cloud is stationary
  }
}

void VFXManager::renderPoisonClouds(const glm::mat4 &view,
                                     const glm::mat4 &projection) {
  if (m_poisonClouds.empty() || m_poisonMeshes.empty() || !m_modelShader)
    return;

  m_modelShader->use();
  m_modelShader->setMat4("view", view);
  m_modelShader->setMat4("projection", projection);
  m_modelShader->setFloat("luminosity", 1.0f);
  m_modelShader->setInt("numPointLights", 0);
  m_modelShader->setBool("useFog", false);
  m_modelShader->setVec2("texCoordOffset", glm::vec2(0.0f));
  m_modelShader->setFloat("outlineOffset", 0.0f);
  m_modelShader->setVec3("lightColor", glm::vec3(1.0f));
  m_modelShader->setVec3("lightPos", glm::vec3(0, 5000, 0));
  glm::mat4 invView = glm::inverse(view);
  m_modelShader->setVec3("viewPos", glm::vec3(invView[3]));

  glEnable(GL_BLEND);
  glDisable(GL_CULL_FACE);

  for (const auto &pc : m_poisonClouds) {
    // Main 5.2 MoveEffect: BlendMeshLight = LifeTime * 0.1 (fades from 4.0 to 0)
    float ticksRemaining = pc.lifetime / 0.04f;
    float blendMeshLight = ticksRemaining * 0.1f; // 4.0 at start, 0 at end

    // Model matrix: translate → BMD base rotation → scale (no spin)
    glm::mat4 model = glm::translate(glm::mat4(1.0f), pc.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(pc.scale));

    m_modelShader->setMat4("model", model);
    m_modelShader->setVec3("terrainLight", glm::vec3(1.0f));
    m_modelShader->setFloat("objectAlpha", pc.alpha);

    // Main 5.2: BlendMesh=1 — mesh with textureId==1 renders additive
    // BlendMeshLight from MoveEffect fades with lifetime (not random flicker)
    for (const auto &mb : m_poisonMeshes) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      bool isGlow = (mb.bmdTextureId == 1);
      if (isGlow) {
        m_modelShader->setFloat("blendMeshLight", blendMeshLight);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Additive
        glDepthMask(GL_FALSE);
      } else {
        m_modelShader->setFloat("blendMeshLight", 1.0f);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      }
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      if (isGlow) {
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      }
    }
  }
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
}

void VFXManager::Cleanup() {
  if (m_quadVAO)
    glDeleteVertexArrays(1, &m_quadVAO);
  if (m_quadVBO)
    glDeleteBuffers(1, &m_quadVBO);
  if (m_quadEBO)
    glDeleteBuffers(1, &m_quadEBO);
  if (m_instanceVBO)
    glDeleteBuffers(1, &m_instanceVBO);
  if (m_ribbonVAO)
    glDeleteVertexArrays(1, &m_ribbonVAO);
  if (m_ribbonVBO)
    glDeleteBuffers(1, &m_ribbonVBO);

  GLuint textures[] = {m_bloodTexture,       m_hitTexture,
                       m_sparkTexture,       m_flareTexture,
                       m_smokeTexture,       m_fireTexture,
                       m_energyTexture,      m_lightningTexture,
                       m_magicGroundTexture, m_ringTexture,
                       m_bitmapFlareTexture, m_thunderTexture};
  for (auto t : textures) {
    if (t)
      glDeleteTextures(1, &t);
  }

  // Clean up Fire Ball 3D model
  for (auto &mb : m_fireMeshes) {
    if (mb.vao) glDeleteVertexArrays(1, &mb.vao);
    if (mb.vbo) glDeleteBuffers(1, &mb.vbo);
    if (mb.ebo) glDeleteBuffers(1, &mb.ebo);
    if (mb.texture) glDeleteTextures(1, &mb.texture);
  }
  m_fireMeshes.clear();
  m_fireBmd.reset();
  m_modelShader.reset();

  // Clean up Lightning sky-strike model
  for (auto &mb : m_blastMeshes) {
    if (mb.vao) glDeleteVertexArrays(1, &mb.vao);
    if (mb.vbo) glDeleteBuffers(1, &mb.vbo);
    if (mb.ebo) glDeleteBuffers(1, &mb.ebo);
    if (mb.texture) glDeleteTextures(1, &mb.texture);
  }
  m_blastMeshes.clear();
  m_blastBmd.reset();

  // Clean up Poison cloud model
  for (auto &mb : m_poisonMeshes) {
    if (mb.vao) glDeleteVertexArrays(1, &mb.vao);
    if (mb.vbo) glDeleteBuffers(1, &mb.vbo);
    if (mb.ebo) glDeleteBuffers(1, &mb.ebo);
    if (mb.texture) glDeleteTextures(1, &mb.texture);
  }
  m_poisonMeshes.clear();
  m_poisonBmd.reset();

  m_particles.clear();
  m_ribbons.clear();
  m_groundCircles.clear();
  m_levelUpEffects.clear();
  m_spellProjectiles.clear();
  m_lightningBolts.clear();
  m_poisonClouds.clear();
}
