#include "VFXManager.hpp"
#include "TextureLoader.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

void VFXManager::Init(const std::string &effectDataPath) {
  // Load blood and hit textures
  std::string bloodPath = effectDataPath + "/Effect/blood01.ozt";
  m_bloodTexture = TextureLoader::LoadOZT(bloodPath);

  std::string hitPath = effectDataPath + "/Interface/hit.OZT";
  m_hitTexture = TextureLoader::LoadOZT(hitPath);

  std::string lightningPath = effectDataPath + "/Effect/JointThunder01.jpg";
  m_lightningTexture = TextureLoader::LoadOZT(lightningPath);

  if (m_bloodTexture == 0)
    std::cerr << "[VFX] Failed to load blood texture: " << bloodPath
              << std::endl;
  if (m_hitTexture == 0)
    std::cerr << "[VFX] Failed to load hit texture: " << hitPath << std::endl;

  // Use the same shader as billboard/fire but could be customized
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

  // Buffers for lightning lines
  glGenVertexArrays(1, &m_lineVAO);
  glGenBuffers(1, &m_lineVBO);
  glBindVertexArray(m_lineVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
  // Max points: 20 per lightning, 10 lightnings = 200 points
  glBufferData(GL_ARRAY_BUFFER, 1000 * sizeof(glm::vec3), nullptr,
               GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void *)0);
  glEnableVertexAttribArray(0);

  glBindVertexArray(0);
}

void VFXManager::SpawnBurst(ParticleType type, const glm::vec3 &position,
                            int count) {
  for (int i = 0; i < count; ++i) {
    if (m_particles.size() >= MAX_PARTICLES)
      break;

    Particle p;
    p.type = type;
    p.position = position;

    float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
    float speed = 50.0f + (float)(rand() % 100);
    p.velocity =
        glm::vec3(std::cos(angle) * speed, 100.0f + (float)(rand() % 50),
                  std::sin(angle) * speed);

    if (type == ParticleType::BLOOD) {
      p.scale = 30.0f + (float)(rand() % 20);
      p.maxLifetime = 0.6f + (float)(rand() % 40) / 100.0f;
      p.color = glm::vec3(1.0f, 0.0f, 0.0f);
    } else { // HIT_SPARK
      p.scale = 40.0f + (float)(rand() % 30);
      p.maxLifetime = 0.2f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(1.0f, 1.0f, 0.8f);
    }

    p.lifetime = p.maxLifetime;
    p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
    p.alpha = 1.0f;

    m_particles.push_back(p);
  }
}

void VFXManager::SpawnLightning(const glm::vec3 &start, const glm::vec3 &end,
                                float duration) {
  Lightning ln;
  ln.start = start;
  ln.end = end;
  ln.lifetime = duration;
  ln.maxLifetime = duration;
  generateLightningPoints(ln);
  m_lightnings.push_back(std::move(ln));
}

void VFXManager::generateLightningPoints(Lightning &ln) {
  ln.points.clear();
  ln.points.push_back(ln.start);
  glm::vec3 dir = ln.end - ln.start;
  float dist = glm::length(dir);
  int segments = 8;
  if (dist > 1000.0f)
    segments = 12;

  for (int i = 1; i < segments; ++i) {
    float t = (float)i / (float)segments;
    glm::vec3 p = ln.start + dir * t;
    // Add jitter
    p.x += (float)(rand() % 100 - 50);
    p.y += (float)(rand() % 100 - 50);
    p.z += (float)(rand() % 100 - 50);
    ln.points.push_back(p);
  }
  ln.points.push_back(ln.end);
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

    if (p.type == ParticleType::BLOOD) {
      p.velocity.y -= 300.0f * deltaTime; // Gravity
      p.scale *= (1.0f - 0.5f * deltaTime);
    } else {
      p.velocity *= (1.0f - 5.0f * deltaTime); // Fast deceleration
      p.scale *= (1.0f - 2.0f * deltaTime);
    }

    p.alpha = p.lifetime / p.maxLifetime;
  }

  // Update lightnings
  for (int i = (int)m_lightnings.size() - 1; i >= 0; --i) {
    auto &ln = m_lightnings[i];
    ln.lifetime -= deltaTime;
    if (ln.lifetime <= 0.0f) {
      m_lightnings[i] = m_lightnings.back();
      m_lightnings.pop_back();
    }
  }
}

void VFXManager::Render(const glm::mat4 &view, const glm::mat4 &projection) {
  if (!m_shader)
    return;

  m_shader->use();
  m_shader->setMat4("view", view);
  m_shader->setMat4("projection", projection);

  glEnable(GL_BLEND);
  glDepthMask(GL_FALSE);

  // 1. Render particles
  auto drawBatch = [&](ParticleType type, GLuint texture) {
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
        if (data.size() >= MAX_PARTICLES)
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

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  drawBatch(ParticleType::BLOOD, m_bloodTexture);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  drawBatch(ParticleType::HIT_SPARK, m_hitTexture);

  // 2. Render lightnings (as lines)
  if (!m_lightnings.empty() && m_lineShader) {
    m_lineShader->use();
    m_lineShader->setMat4("view", view);
    m_lineShader->setMat4("projection", projection);

    glDisable(GL_TEXTURE_2D);
    glLineWidth(2.5f);

    std::vector<glm::vec3> lineVerts;
    for (const auto &ln : m_lightnings) {
      if (ln.points.size() < 2)
        continue;
      for (size_t i = 0; i < ln.points.size() - 1; ++i) {
        lineVerts.push_back(ln.points[i]);
        lineVerts.push_back(ln.points[i + 1]);
      }
    }

    if (!lineVerts.empty()) {
      glBindVertexArray(m_lineVAO);
      glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
      glBufferSubData(GL_ARRAY_BUFFER, 0, lineVerts.size() * sizeof(glm::vec3),
                      lineVerts.data());

      m_lineShader->setVec3("color",
                            glm::vec3(0.6f, 0.7f, 1.0f)); // Bright blue
      m_lineShader->setFloat("alpha", 1.0f);
      glDrawArrays(GL_LINES, 0, (GLsizei)lineVerts.size());
    }
    glEnable(GL_TEXTURE_2D);
  }

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
  if (m_lineVAO)
    glDeleteVertexArrays(1, &m_lineVAO);
  if (m_lineVBO)
    glDeleteBuffers(1, &m_lineVBO);
  if (m_bloodTexture)
    glDeleteTextures(1, &m_bloodTexture);
  if (m_hitTexture)
    glDeleteTextures(1, &m_hitTexture);
  if (m_lightningTexture)
    glDeleteTextures(1, &m_lightningTexture);
  m_particles.clear();
  m_lightnings.clear();
}
