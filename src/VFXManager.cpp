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
  m_hitTexture =
      TextureLoader::LoadOZT(effectDataPath + "/Interface/hit.OZT");

  // Lightning ribbon texture (Main 5.2: BITMAP_JOINT_THUNDER)
  m_lightningTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/JointThunder01.OZJ");

  // Monster ambient VFX textures
  m_smokeTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/smoke01.OZJ");
  m_fireTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Fire01.OZJ");
  m_energyTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/JointEnergy01.OZJ");

  if (m_bloodTexture == 0)
    std::cerr << "[VFX] Failed to load blood texture" << std::endl;
  if (m_sparkTexture == 0)
    std::cerr << "[VFX] Failed to load spark texture (Spark01.OZJ)" << std::endl;
  if (m_flareTexture == 0)
    std::cerr << "[VFX] Failed to load flare texture (flare01.OZJ)" << std::endl;
  if (m_lightningTexture == 0)
    std::cerr << "[VFX] Failed to load lightning texture" << std::endl;
  if (m_smokeTexture == 0)
    std::cerr << "[VFX] Failed to load smoke texture" << std::endl;
  if (m_fireTexture == 0)
    std::cerr << "[VFX] Failed to load fire texture" << std::endl;
  if (m_energyTexture == 0)
    std::cerr << "[VFX] Failed to load energy texture" << std::endl;

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
  // Matches line.vert layout: location 0 = aPos (vec3), location 1 = aTexCoord (vec2)
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
      p.velocity = glm::vec3(std::cos(angle) * speed,
                             100.0f + (float)(rand() % 60),
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
      p.velocity = glm::vec3(std::cos(angle) * speed,
                             100.0f + (float)(rand() % 100),
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
      p.velocity = glm::vec3(std::cos(angle) * speed,
                             20.0f + (float)(rand() % 30),
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
      p.velocity = glm::vec3(std::cos(angle) * speed,
                             60.0f + (float)(rand() % 40),
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
      p.velocity = glm::vec3(std::cos(angle) * speed,
                             50.0f + (float)(rand() % 30),
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
    }

    p.lifetime = p.maxLifetime;
    m_particles.push_back(p);
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

  // Draw each ribbon separately with its own color/alpha (fixes per-ribbon color bug)
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
      float u0 = ((float)(r.segments.size() - j) /
                  (float)(Ribbon::MAX_SEGMENTS - 1)) *
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
  drawBatch(ParticleType::HIT_SPARK, m_sparkTexture ? m_sparkTexture : m_hitTexture);
  drawBatch(ParticleType::FIRE, m_fireTexture);
  drawBatch(ParticleType::ENERGY, m_energyTexture);
  drawBatch(ParticleType::FLARE, m_flareTexture ? m_flareTexture : m_hitTexture);

  // Render textured ribbons (Lich Joint Thunder)
  renderRibbons(view, projection);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_TRUE);
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

  GLuint textures[] = {m_bloodTexture,  m_hitTexture,   m_sparkTexture,
                       m_flareTexture,  m_smokeTexture, m_fireTexture,
                       m_energyTexture, m_lightningTexture};
  for (auto t : textures) {
    if (t)
      glDeleteTextures(1, &t);
  }

  m_particles.clear();
  m_ribbons.clear();
}
