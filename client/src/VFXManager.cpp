#include "VFXManager.hpp"
#include "TextureLoader.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
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
  }
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
    }

    p.alpha = p.lifetime / p.maxLifetime;
  }

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
        d.frame = 0.0f;
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
                       m_bitmapFlareTexture};
  for (auto t : textures) {
    if (t)
      glDeleteTextures(1, &t);
  }

  m_particles.clear();
  m_ribbons.clear();
  m_groundCircles.clear();
  m_levelUpEffects.clear();
}
