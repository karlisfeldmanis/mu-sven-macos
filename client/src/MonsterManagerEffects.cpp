#include "MonsterManager.hpp"
#include "TerrainUtils.hpp"
#include <cmath>
#include <cstdlib>

void MonsterManager::spawnDebris(int modelIdx, const glm::vec3 &pos,
                                 int count) {
  if (modelIdx < 0 || modelIdx >= (int)m_models.size())
    return;

  for (int i = 0; i < count; ++i) {
    DebrisInstance d;
    d.modelIdx = modelIdx;
    d.position = pos;
    float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
    float speed = 80.0f + (float)(rand() % 100);
    d.velocity =
        glm::vec3(std::cos(angle) * speed, 150.0f + (float)(rand() % 100),
                  std::sin(angle) * speed);
    d.rotation = glm::vec3((float)(rand() % 360), (float)(rand() % 360),
                           (float)(rand() % 360));
    d.rotVelocity =
        glm::vec3((float)(rand() % 200 - 100), (float)(rand() % 200 - 100),
                  (float)(rand() % 200 - 100));
    d.scale = m_models[modelIdx].scale * (0.8f + (float)(rand() % 40) / 100.0f);
    d.lifetime = 2.0f + (float)(rand() % 2000) / 1000.0f;
    m_debris.push_back(d);
  }
}

void MonsterManager::updateDebris(float dt) {
  for (int i = (int)m_debris.size() - 1; i >= 0; --i) {
    auto &d = m_debris[i];
    d.lifetime -= dt;
    if (d.lifetime <= 0.0f) {
      m_debris[i] = m_debris.back();
      m_debris.pop_back();
      continue;
    }

    d.position += d.velocity * dt;
    d.rotation += d.rotVelocity * dt;

    float floorY = snapToTerrain(d.position.x, d.position.z);
    if (d.position.y < floorY) {
      d.position.y = floorY;
      d.velocity.y = -d.velocity.y * 0.4f; // Bounce
      d.velocity.x *= 0.6f;
      d.velocity.z *= 0.6f;
      d.rotVelocity *= 0.5f;
    } else {
      d.velocity.y -= 500.0f * dt; // Gravity
    }
  }
}

void MonsterManager::renderDebris(const glm::mat4 &view,
                                  const glm::mat4 &projection,
                                  const glm::vec3 &camPos) {
  if (m_debris.empty() || !m_shader)
    return;

  m_shader->use();
  m_shader->setMat4("view", view);
  m_shader->setMat4("projection", projection);
  m_shader->setFloat("luminosity", m_luminosity);
  m_shader->setFloat("blendMeshLight", 1.0f);
  m_shader->setInt("numPointLights", 0);
  m_shader->setBool("useFog", true);
  if (m_mapId == 1) {
    m_shader->setVec3("uFogColor", glm::vec3(0.0f, 0.0f, 0.0f));
    m_shader->setFloat("uFogNear", 800.0f);
    m_shader->setFloat("uFogFar", 2500.0f);
  } else {
    m_shader->setVec3("uFogColor", glm::vec3(0.117f, 0.078f, 0.039f));
    m_shader->setFloat("uFogNear", 1500.0f);
    m_shader->setFloat("uFogFar", 3500.0f);
  }
  m_shader->setVec3("viewPos", camPos);
  // We need camPos if we want fog to work correctly, passing it via Render
  // For now let's assume viewPos is already set or passed.
  // I'll update Render() to pass camPos to renderDebris.

  for (const auto &d : m_debris) {
    auto &mdl = m_models[d.modelIdx];
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, d.position);
    model = glm::rotate(model, glm::radians(d.rotation.z), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(d.rotation.y), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(d.rotation.x), glm::vec3(1, 0, 0));
    model = glm::scale(model, glm::vec3(d.scale));
    m_shader->setMat4("model", model);

    // Get terrain light
    glm::vec3 light = sampleTerrainLightAt(d.position);
    m_shader->setVec3("terrainLight", light);

    // Debris fade out
    float alpha = std::min(1.0f, d.lifetime * 2.0f);
    m_shader->setFloat("objectAlpha", alpha);

    // Draw pre-uploaded mesh buffers
    for (size_t i = 0; i < mdl.meshBuffers.size(); ++i) {
      auto &mb = mdl.meshBuffers[i];
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }
  glBindVertexArray(0);
}

void MonsterManager::SpawnArrow(const glm::vec3 &from, const glm::vec3 &to,
                                float speed) {
  ArrowProjectile a;
  a.position = from;
  glm::vec3 delta = to - from;
  float dist = glm::length(delta);
  if (dist < 1.0f)
    return;
  glm::vec3 dir = delta / dist;
  a.direction = dir;
  a.speed = speed;
  a.yaw = atan2f(dir.z, -dir.x); // Same as monster facing formula
  a.pitch = asinf(-dir.y); // Negative: pitch up when target is higher
  a.scale = 0.8f;
  a.lifetime = std::min(1.2f, dist / speed + 0.1f);
  m_arrows.push_back(a);
}

void MonsterManager::updateArrows(float dt) {
  for (int i = (int)m_arrows.size() - 1; i >= 0; --i) {
    auto &a = m_arrows[i];
    a.lifetime -= dt;
    if (a.lifetime <= 0.0f) {
      m_arrows[i] = m_arrows.back();
      m_arrows.pop_back();
      continue;
    }
    // Move along direction
    a.position += a.direction * a.speed * dt;
    // Gravity: arrow pitches down over time (Main 5.2: Angle[0] += Gravity)
    a.pitch += 0.4f * dt; // gentle pitch-down
    // Apply pitch to direction (subtle arc)
    a.direction.y -= 0.2f * dt;
    a.direction = glm::normalize(a.direction);
  }
}

void MonsterManager::renderArrows(const glm::mat4 &view,
                                  const glm::mat4 &projection,
                                  const glm::vec3 &camPos) {
  if (m_arrows.empty() || !m_shader || m_arrowModelIdx < 0)
    return;

  auto &mdl = m_models[m_arrowModelIdx];
  if (mdl.meshBuffers.empty())
    return;

  m_shader->use();
  m_shader->setMat4("view", view);
  m_shader->setMat4("projection", projection);
  m_shader->setFloat("luminosity", m_luminosity);
  m_shader->setFloat("blendMeshLight", 1.0f);
  m_shader->setInt("numPointLights", 0);
  m_shader->setBool("useFog", true);
  if (m_mapId == 1) {
    m_shader->setVec3("uFogColor", glm::vec3(0.0f, 0.0f, 0.0f));
    m_shader->setFloat("uFogNear", 800.0f);
    m_shader->setFloat("uFogFar", 2500.0f);
  } else {
    m_shader->setVec3("uFogColor", glm::vec3(0.117f, 0.078f, 0.039f));
    m_shader->setFloat("uFogNear", 1500.0f);
    m_shader->setFloat("uFogFar", 3500.0f);
  }
  m_shader->setVec3("viewPos", camPos);

  for (const auto &a : m_arrows) {
    // Arrow model matrix: position, then rotate to face direction, then scale
    // Main 5.2: Arrow uses angle-based rotation (yaw from direction, pitch from
    // gravity)
    glm::mat4 model = glm::translate(glm::mat4(1.0f), a.position);
    // Standard BMD rotation base
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    // Arrow heading (yaw) and pitch
    model = glm::rotate(model, a.yaw, glm::vec3(0, 0, 1));
    model = glm::rotate(model, a.pitch, glm::vec3(1, 0, 0));
    model = glm::scale(model, glm::vec3(a.scale));

    m_shader->setMat4("model", model);
    m_shader->setVec3("terrainLight", glm::vec3(1.0f));
    m_shader->setFloat("objectAlpha", 1.0f);

    // Main 5.2: BlendMesh=1 — mesh 0 (arrow shaft) renders normally,
    // mesh 1 (fire glow) renders with additive blend
    for (int mi = 0; mi < (int)mdl.meshBuffers.size(); ++mi) {
      auto &mb = mdl.meshBuffers[mi];
      if (mb.indexCount == 0)
        continue;
      bool isGlowMesh = (mb.bmdTextureId == 1); // BlendMesh=1
      if (isGlowMesh) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
      }
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      if (isGlowMesh) {
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      }
    }
  }
  glBindVertexArray(0);
}
