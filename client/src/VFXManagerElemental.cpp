#include "VFXManager.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

// Main 5.2: AT_SKILL_BLAST — twin sky-strike bolts at target position
// Creates 2 Blast01.bmd orbs that fall from sky with gravity and explode on impact
void VFXManager::SpawnLightningStrike(const glm::vec3 &targetPos) {
  for (int i = 0; i < 2; ++i) {
    LightningBolt bolt;
    // Main 5.2: Position += (rand%100+200, rand%100-50, rand%500+300)
    // Asymmetric X offset so bolts come from the side matching diagonal fall
    float scatterX = (float)(rand() % 100 + 200); // +200 to +300
    float scatterZ = (float)(rand() % 100 - 50);  // -50 to +50
    float height = (float)(rand() % 500 + 300);   // +300 to +800 above target
    bolt.position = targetPos + glm::vec3(scatterX, height, scatterZ);

    // Main 5.2: Direction=(0,0,-50-rand%50), Angle=(0,20,0) — 20deg diagonal fall
    float fallSpeed = (50.0f + (float)(rand() % 50)) * 25.0f; // 1250-2500 units/sec
    float angleRad = glm::radians(20.0f);
    bolt.velocity = glm::vec3(-fallSpeed * std::sin(angleRad),
                              -fallSpeed * std::cos(angleRad),
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

// Single vertical sky bolt for Lightning spell monster hit effect.
// Lighter than SpawnLightningStrike (1 bolt straight down vs 2 angled bolts).
void VFXManager::SpawnLightningImpactBolt(const glm::vec3 &targetPos) {
  LightningBolt bolt;
  // Minimal horizontal scatter — bolt comes from directly above
  float scatterX = (float)(rand() % 40 - 20); // -20 to +20
  float scatterZ = (float)(rand() % 40 - 20); // -20 to +20
  float height = (float)(rand() % 200 + 300);  // +300 to +500 above target
  bolt.position = targetPos + glm::vec3(scatterX, height, scatterZ);

  // Straight vertical drop (no 20-degree angle like Cometfall)
  float fallSpeed = (60.0f + (float)(rand() % 40)) * 25.0f; // 1500-2500 units/sec
  bolt.velocity = glm::vec3(0.0f, -fallSpeed, 0.0f);

  bolt.scale = (float)(rand() % 6 + 8) * 0.1f; // 0.8-1.3 (smaller than Cometfall)
  bolt.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
  bolt.maxLifetime = 0.8f; // 20 ticks
  bolt.lifetime = bolt.maxLifetime;
  bolt.impacted = false;
  bolt.impactTimer = 0.0f;
  bolt.numTrail = 0;
  bolt.trailTimer = 0.0f;
  m_lightningBolts.push_back(bolt);
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
    // Main 5.2: Angle fixed at (0,20,0), no rotation during flight

    // BITMAP_JOINT_ENERGY trail + scatter particles — update at tick rate (~25fps)
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
      b.velocity = glm::vec3(0.0f);
      b.impacted = true;
      b.impactTimer = 0.0f;
      // Main 5.2: particles at Position[2]+80 (80 above ground)
      glm::vec3 impactAbove = b.position + glm::vec3(0, 80, 0);
      // Subtle impact: small debris + single flash
      SpawnBurst(ParticleType::HIT_SPARK, b.position + glm::vec3(0, 20, 0), 3);
      SpawnBurst(ParticleType::FLARE, impactAbove, 1);
    }
  }
  // Remove expired bolts (impacted + trail fully faded)
  m_lightningBolts.erase(
      std::remove_if(m_lightningBolts.begin(), m_lightningBolts.end(),
                     [](const LightningBolt &b) {
                       return b.impacted && b.impactTimer > 0.8f;
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

// ============================================================
// Ice Spell (AT_SKILL_ICE, skill 7) — Main 5.2 MODEL_ICE + MODEL_ICE_SMALL
// ============================================================

void VFXManager::SpawnIceStrike(const glm::vec3 &targetPos) {
  // Main 5.2: 1x MODEL_ICE crystal at target
  // LifeTime=50 but ~25 ticks visible (5 growth + 20 fade)
  IceCrystal crystal;
  crystal.position = targetPos;
  crystal.scale = 0.8f;       // Main 5.2: Scale = 0.8
  crystal.alpha = 1.0f;
  crystal.maxLifetime = 1.0f; // ~25 ticks at 25fps (5 growth + 20 fade)
  crystal.lifetime = crystal.maxLifetime;
  crystal.fadePhase = false;
  crystal.smokeTimer = 0.0f;
  m_iceCrystals.push_back(crystal);

  // Main 5.2 spawns 5x MODEL_ICE_SMALL debris shards here
  // Disabled for now — focus on the ice crystal block only
}

void VFXManager::updateIceCrystals(float dt) {
  for (auto &c : m_iceCrystals) {
    c.lifetime -= dt;

    // Main 5.2: AnimationFrame >= 5 triggers fade phase (5 ticks = 0.2s)
    float elapsed = c.maxLifetime - c.lifetime;
    if (!c.fadePhase && elapsed >= 0.2f) {
      c.fadePhase = true;
    }

    if (c.fadePhase) {
      // Main 5.2: Alpha -= 0.05 per tick (25fps) → 1.25 per second
      c.alpha -= 1.25f * dt;

      // Subtle smoke wisps during crystal fade (~3 per second)
      c.smokeTimer += dt;
      if (c.smokeTimer >= 0.33f) {
        c.smokeTimer -= 0.33f;
        glm::vec3 smokePos = c.position + glm::vec3(
            (float)(rand() % 40 - 20), (float)(rand() % 60 + 20),
            (float)(rand() % 40 - 20));
        SpawnBurst(ParticleType::SMOKE, smokePos, 1);
      }
    }
  }
  m_iceCrystals.erase(
      std::remove_if(m_iceCrystals.begin(), m_iceCrystals.end(),
                     [](const IceCrystal &c) { return c.alpha <= 0.0f; }),
      m_iceCrystals.end());
}

void VFXManager::updateIceShards(float dt) {
  for (auto &s : m_iceShards) {
    s.lifetime -= dt;

    // Main 5.2: Position += Direction, Direction *= 0.9 (decay per tick)
    s.position += s.velocity * dt;
    s.velocity *= (1.0f - 2.5f * dt); // ~0.9 per tick at 25fps

    // Main 5.2: Gravity -= 3 per tick, Position[2] += Gravity
    // Gravity stored as per-second units (initial 200-575 upward)
    s.gravity -= 75.0f * dt; // 3/tick * 25tps = 75/sec
    s.position.y += s.gravity * dt;

    // Main 5.2: Tumble rotation (Angle[0] -= Scale * 32 in air)
    s.angleX -= s.scale * 800.0f * dt; // 32 * 25fps

    // Terrain collision — bounce
    float groundH = m_getTerrainHeight
                         ? m_getTerrainHeight(s.position.x, s.position.z)
                         : 0.0f;
    if (s.position.y < groundH) {
      s.position.y = groundH;
      // Main 5.2: Gravity = -Gravity * 0.5 (bounce at 50% restitution)
      s.gravity = -s.gravity * 0.5f;
      // Main 5.2: LifeTime -= 4
      s.lifetime -= 4.0f / 25.0f; // 4 ticks = 0.16s
      // Main 5.2: Angle[0] -= Scale * 128 (faster tumble on bounce)
      s.angleX -= s.scale * 128.0f;
    }

    // Main 5.2: ~10% chance per tick to spawn BITMAP_SMOKE (white)
    s.smokeTimer += dt;
    if (s.smokeTimer >= 0.4f) { // ~every 10 ticks
      s.smokeTimer -= 0.4f;
      SpawnBurst(ParticleType::SMOKE, s.position, 1);
    }
  }
  m_iceShards.erase(
      std::remove_if(m_iceShards.begin(), m_iceShards.end(),
                     [](const IceShard &s) { return s.lifetime <= 0.0f; }),
      m_iceShards.end());
}

void VFXManager::renderIceCrystals(const glm::mat4 &view,
                                    const glm::mat4 &projection) {
  if (m_iceCrystals.empty() || m_iceMeshes.empty() || !m_modelShader)
    return;

  glm::mat4 invView = glm::inverse(view);

  glEnable(GL_BLEND);
  glDisable(GL_CULL_FACE);

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

  // Main 5.2: BlendMesh=0 — mesh 0 renders additive
  glBlendFunc(GL_ONE, GL_ONE);
  glDepthMask(GL_FALSE);

  for (const auto &c : m_iceCrystals) {
    m_modelShader->setFloat("objectAlpha", c.alpha);

    // Standard BMD orientation
    glm::mat4 model = glm::translate(glm::mat4(1.0f), c.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(c.scale));
    m_modelShader->setMat4("model", model);

    for (const auto &mb : m_iceMeshes) {
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

void VFXManager::renderIceShards(const glm::mat4 &view,
                                  const glm::mat4 &projection) {
  if (m_iceShards.empty() || m_iceSmallMeshes.empty() || !m_modelShader)
    return;

  glm::mat4 invView = glm::inverse(view);

  glEnable(GL_BLEND);
  glDisable(GL_CULL_FACE);

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

  // Solid ice debris — normal alpha blending so they look like ice pieces, not glow
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_TRUE);

  for (const auto &s : m_iceShards) {
    float alpha = std::min(1.0f, s.lifetime * 2.0f); // Fade near end
    m_modelShader->setFloat("objectAlpha", alpha);

    // Position + tumble rotation
    glm::mat4 model = glm::translate(glm::mat4(1.0f), s.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, s.angleX, glm::vec3(1, 0, 0)); // Tumble
    model = glm::scale(model, glm::vec3(s.scale));
    m_modelShader->setMat4("model", model);

    for (const auto &mb : m_iceSmallMeshes) {
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

      m_modelShader->setFloat("blendMeshLight", 1.0f);

      glm::mat4 model = glm::translate(glm::mat4(1.0f), b.position);
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      model = glm::rotate(model, glm::radians(20.0f), glm::vec3(0, 1, 0)); // Fall angle tilt
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
  if (m_energyTexture && m_ribbonVAO && m_lineShader) {
    m_lineShader->use();
    m_lineShader->setMat4("view", view);
    m_lineShader->setMat4("projection", projection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_energyTexture);
    m_lineShader->setInt("ribbonTex", 0);
    m_lineShader->setBool("useTexture", true);
    m_lineShader->setBool("beamMode", false);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Main 5.2: RENDER_TYPE_ALPHA_BLEND
    glDepthMask(GL_FALSE);

    for (const auto &b : m_lightningBolts) {
      if (b.numTrail < 2)
        continue;

      float alpha = b.impacted
                        ? std::max(0.0f, 1.0f - b.impactTimer * 7.0f)
                        : std::min(1.0f, b.lifetime / b.maxLifetime * 4.0f);
      if (alpha <= 0.01f)
        continue;

      float flicker = 0.7f + 0.3f * ((float)(rand() % 100) / 100.0f);
      glm::vec3 trailColor = glm::vec3(0.3f, 0.5f, 1.0f);
      m_lineShader->setVec3("color", trailColor * flicker);
      m_lineShader->setFloat("alpha", alpha);

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

// Main 5.2: BITMAP_FLAME SubType 0 — persistent ground fire at target position
// ZzzEffect.cpp: CreateEffect(BITMAP_FLAME, Position=target, LifeTime=40)
// Every tick: 6 BITMAP_FLAME particles + AddTerrainLight(1.0, 0.4, 0.0, range=3)
void VFXManager::SpawnFlameGround(const glm::vec3 &targetPos) {
  FlameGround fg;
  fg.position = targetPos;
  fg.lifetime = 1.6f;     // 40 ticks @ 25fps
  fg.maxLifetime = 1.6f;
  fg.tickTimer = 0.0f;
  m_flameGrounds.push_back(fg);

  // Initial burst — Main 5.2: first tick spawns particles immediately
  SpawnBurst(ParticleType::SPELL_FLAME, targetPos + glm::vec3(0, 10, 0), 6);
  SpawnBurst(ParticleType::FLARE, targetPos + glm::vec3(0, 30, 0), 2);
}

void VFXManager::updateFlameGrounds(float dt) {
  for (int i = (int)m_flameGrounds.size() - 1; i >= 0; --i) {
    auto &fg = m_flameGrounds[i];
    fg.lifetime -= dt;
    if (fg.lifetime <= 0.0f) {
      m_flameGrounds[i] = m_flameGrounds.back();
      m_flameGrounds.pop_back();
      continue;
    }

    // Tick-based spawning: 6 BITMAP_FLAME particles per tick (25fps = 0.04s)
    fg.tickTimer += dt;
    while (fg.tickTimer >= 0.04f) {
      fg.tickTimer -= 0.04f;

      // Main 5.2: 6 BITMAP_FLAME particles with random +-25 offset
      SpawnBurst(ParticleType::SPELL_FLAME,
                 fg.position + glm::vec3(0, 10, 0), 6);

      // Main 5.2: 1-in-8 chance per tick — stone debris (substitute with sparks)
      if (rand() % 8 == 0) {
        SpawnBurst(ParticleType::HIT_SPARK,
                   fg.position + glm::vec3(0, 20, 0), 3);
      }
    }
  }
}

// Main 5.2: RenderTerrainAlphaBitmap(BITMAP_FLAME, pos, 2.f, 2.f, Light, rotation)
// Flat terrain-projected fire sprite, flickering luminosity 0.8-1.2
void VFXManager::renderFlameGrounds(const glm::mat4 &view,
                                     const glm::mat4 &projection) {
  if (m_flameGrounds.empty() || !m_lineShader || m_flameTexture == 0)
    return;

  m_lineShader->use();
  m_lineShader->setMat4("view", view);
  m_lineShader->setMat4("projection", projection);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_flameTexture);
  m_lineShader->setInt("ribbonTex", 0);
  m_lineShader->setBool("useTexture", true);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Additive blend
  glDisable(GL_CULL_FACE);

  glBindVertexArray(m_ribbonVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVBO);

  for (const auto &fg : m_flameGrounds) {
    // Main 5.2: Luminosity = (rand()%4+8)*0.1f = 0.8-1.2 flicker per frame
    float lum = (float)(rand() % 4 + 8) * 0.1f;
    // Fade alpha in last 25% of lifetime
    float alpha = 1.0f;
    if (fg.lifetime < fg.maxLifetime * 0.25f)
      alpha = fg.lifetime / (fg.maxLifetime * 0.25f);

    m_lineShader->setVec3("color", glm::vec3(lum, lum, lum));
    m_lineShader->setFloat("alpha", alpha);

    // Main 5.2: 2.0 x 2.0 terrain scale ≈ 200x200 world units
    float halfSize = 100.0f;

    // No rotation for SubType 0 (stationary ground fire)
    glm::vec3 right(halfSize, 0.0f, 0.0f);
    glm::vec3 fwd(0.0f, 0.0f, halfSize);
    glm::vec3 pos = fg.position + glm::vec3(0.0f, 2.0f, 0.0f); // Slight Y offset

    RibbonVertex verts[6];
    verts[0].pos = pos - right - fwd;
    verts[0].uv = glm::vec2(0.0f, 0.0f);
    verts[1].pos = pos + right - fwd;
    verts[1].uv = glm::vec2(1.0f, 0.0f);
    verts[2].pos = pos + right + fwd;
    verts[2].uv = glm::vec2(1.0f, 1.0f);
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

// Main 5.2: MODEL_STORM SubType 0 — Twister tornado at target
// ZzzEffect.cpp: CreateEffect(MODEL_STORM, Position=caster, LifeTime=59)
// Tornado travels from caster toward target direction
void VFXManager::SpawnTwisterStorm(const glm::vec3 &casterPos,
                                    const glm::vec3 &targetDir) {
  TwisterStorm ts;
  ts.position = casterPos;
  // Snap to terrain height if callback available
  if (m_getTerrainHeight) {
    ts.position.y = m_getTerrainHeight(casterPos.x, casterPos.z);
  }
  // Horizontal direction toward target
  glm::vec3 dir = targetDir;
  dir.y = 0.0f;
  float len = glm::length(dir);
  ts.direction = (len > 0.01f) ? dir / len : glm::vec3(0, 0, 1);
  ts.speed = 200.0f; // Travel speed (world units/sec)
  ts.lifetime = 2.36f;     // 59 ticks @ 25fps
  ts.maxLifetime = 2.36f;
  ts.tickTimer = 0.0f;
  ts.rotation = 0.0f;
  m_twisterStorms.push_back(ts);
}

void VFXManager::updateTwisterStorms(float dt) {
  for (int i = (int)m_twisterStorms.size() - 1; i >= 0; --i) {
    auto &ts = m_twisterStorms[i];
    ts.lifetime -= dt;
    if (ts.lifetime <= 0.0f) {
      m_twisterStorms[i] = m_twisterStorms.back();
      m_twisterStorms.pop_back();
      continue;
    }

    // Move tornado in click direction
    ts.position += ts.direction * ts.speed * dt;
    // Snap to terrain height each tick
    if (m_getTerrainHeight) {
      ts.position.y = m_getTerrainHeight(ts.position.x, ts.position.z);
    }
    // Main 5.2 SubType 0: no explicit model spin — UV scroll creates visual rotation

    // Tick-based particle effects (25fps = 0.04s/tick)
    ts.tickTimer += dt;
    while (ts.tickTimer >= 0.04f) {
      ts.tickTimer -= 0.04f;

      // Main 5.2: BITMAP_SMOKE SubType 3 — LifeTime=10, Scale=(rand()%32+80)*0.01
      // Velocity *= 0.4 per tick (rapid decel), Scale += 0.1/tick (fast expand)
      // Color: Luminosity=LifeTime/8 → (L*0.8, L*0.8, L) slight blue tint
      Particle smoke;
      smoke.type = ParticleType::SMOKE;
      smoke.position = ts.position;
      smoke.velocity = glm::vec3(
          (float)(rand() % 10 - 5) * 0.5f,
          (float)(rand() % 9 + 40) * 2.0f,  // Higher initial → decels fast with our update
          (float)(rand() % 10 - 5) * 0.5f);
      smoke.scale = (float)(rand() % 32 + 80) * 0.15f; // Start smaller, grows with update
      smoke.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
      smoke.frame = -1.0f;
      smoke.lifetime = 0.4f;      // Main 5.2: 10 ticks @ 25fps
      smoke.maxLifetime = 0.4f;
      smoke.color = glm::vec3(0.45f, 0.45f, 0.55f); // Slight blue tint (L*0.8, L*0.8, L)
      smoke.alpha = 0.6f;
      m_particles.push_back(smoke);

      // Main 5.2: TWO independent 50% checks for left/right lightning
      // Left bolt: Position[0]-200, Position[2]+700 → Position (tornado center)
      if (rand() % 2 == 0) {
        glm::vec3 skyPos = ts.position + glm::vec3(-200.0f, 700.0f, 0.0f);
        SpawnRibbon(skyPos, ts.position, 10.0f,
                    glm::vec3(0.6f, 0.7f, 1.0f), 0.2f);
        SpawnBurst(ParticleType::SPELL_LIGHTNING, ts.position, 3);
      }
      // Right bolt: Position[0]+200, Position[2]+700 → Position
      if (rand() % 2 == 0) {
        glm::vec3 skyPos = ts.position + glm::vec3(200.0f, 700.0f, 0.0f);
        SpawnRibbon(skyPos, ts.position, 10.0f,
                    glm::vec3(0.6f, 0.7f, 1.0f), 0.2f);
        SpawnBurst(ParticleType::SPELL_LIGHTNING, ts.position, 3);
      }

      // Main 5.2: 25% chance stone debris (MODEL_STONE1/2 SubType 2)
      // 20-35 frame lifetime (0.8-1.4s), scale 0.24-1.04, bouncing upward
      // Substitute with dark particles + sparks (no stone model)
      if (rand() % 4 == 0) {
        for (int d = 0; d < 2; ++d) {
          Particle debris;
          debris.type = ParticleType::SPELL_DARK;
          debris.position = ts.position + glm::vec3(
              (float)(rand() % 80 - 40), 5.0f, (float)(rand() % 80 - 40));
          debris.velocity = glm::vec3(
              (float)(rand() % 40 - 20),
              75.0f + (float)(rand() % 50),  // Upward fling
              (float)(rand() % 40 - 20));
          debris.scale = 6.0f + (float)(rand() % 20); // Main 5.2: 0.24-1.04
          debris.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          debris.frame = -1.0f;
          debris.lifetime = 0.8f + (float)(rand() % 15) * 0.04f; // 20-35 frames
          debris.maxLifetime = debris.lifetime;
          debris.color = glm::vec3(0.4f, 0.35f, 0.3f); // Earthy brown
          debris.alpha = 0.9f;
          m_particles.push_back(debris);
        }
      }
    }
  }
}

void VFXManager::renderTwisterStorms(const glm::mat4 &view,
                                      const glm::mat4 &projection) {
  if (m_twisterStorms.empty() || m_stormMeshes.empty() || !m_modelShader)
    return;

  m_modelShader->use();
  m_modelShader->setMat4("view", view);
  m_modelShader->setMat4("projection", projection);
  m_modelShader->setFloat("luminosity", 1.0f);
  m_modelShader->setInt("numPointLights", 0);
  m_modelShader->setBool("useFog", false);
  m_modelShader->setFloat("outlineOffset", 0.0f);
  m_modelShader->setVec3("lightColor", glm::vec3(1.0f));
  m_modelShader->setVec3("lightPos", glm::vec3(0, 5000, 0));
  glm::mat4 invView = glm::inverse(view);
  m_modelShader->setVec3("viewPos", glm::vec3(invView[3]));

  glEnable(GL_BLEND);
  glDisable(GL_CULL_FACE);

  for (const auto &ts : m_twisterStorms) {
    // Main 5.2: BlendMeshLight = LifeTime * 0.1 (0→5.9)
    // In fixed-function GL this was clamped to [0,1] by glColor3f.
    // Clamp to 1.0 max so additive blend isn't overblown white.
    float ticksRemaining = ts.lifetime / 0.04f;
    float blendMeshLight = std::min(ticksRemaining * 0.1f, 1.0f);

    // Main 5.2: BlendMeshTexCoordU = -(float)LifeTime * 0.1f (scrolls U axis)
    float uvScrollU = -ticksRemaining * 0.1f;

    // Subtle alpha: keep tornado translucent, fade out in last 20%
    float alpha = 0.6f;
    if (ts.lifetime < ts.maxLifetime * 0.2f)
      alpha = 0.6f * ts.lifetime / (ts.maxLifetime * 0.2f);

    // Model matrix: translate → BMD base rotation → scale
    glm::mat4 model = glm::translate(glm::mat4(1.0f), ts.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(1.0f));

    m_modelShader->setMat4("model", model);
    m_modelShader->setVec3("terrainLight", glm::vec3(1.0f));
    m_modelShader->setFloat("objectAlpha", alpha);

    // Tornado is fully transparent effect — disable depth write
    glDepthMask(GL_FALSE);

    // Main 5.2: BlendMesh=0 — mesh with textureId==0 renders additive
    // BlendMeshTexCoordU only applies to the blend mesh, not other meshes
    for (const auto &mb : m_stormMeshes) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      bool isBlend = (mb.bmdTextureId == 0);
      if (isBlend) {
        m_modelShader->setFloat("blendMeshLight", blendMeshLight);
        m_modelShader->setVec2("texCoordOffset", glm::vec2(uvScrollU, 0.0f));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Additive
      } else {
        m_modelShader->setFloat("blendMeshLight", 1.0f);
        m_modelShader->setVec2("texCoordOffset", glm::vec2(0.0f));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      }
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }

    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  // Reset UV offset
  m_modelShader->setVec2("texCoordOffset", glm::vec2(0.0f));
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
}
