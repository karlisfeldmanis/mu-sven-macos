#include "ClickEffect.hpp"
#include "TextureLoader.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

float ClickEffect::getTerrainHeight(float worldX, float worldZ) const {
  if (!m_terrainData)
    return 0.0f;
  const int S = TerrainParser::TERRAIN_SIZE;
  float gz = worldX / 100.0f;
  float gx = worldZ / 100.0f;
  gz = std::clamp(gz, 0.0f, (float)(S - 2));
  gx = std::clamp(gx, 0.0f, (float)(S - 2));
  int xi = (int)gx, zi = (int)gz;
  float xd = gx - (float)xi, zd = gz - (float)zi;
  float h00 = m_terrainData->heightmap[zi * S + xi];
  float h10 = m_terrainData->heightmap[zi * S + (xi + 1)];
  float h01 = m_terrainData->heightmap[(zi + 1) * S + xi];
  float h11 = m_terrainData->heightmap[(zi + 1) * S + (xi + 1)];
  return h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
         h01 * (1 - xd) * zd + h11 * xd * zd;
}

void ClickEffect::drawGroundQuad(float cx, float cz, float halfSize,
                                 float hOff) {
  ViewerVertex verts[4];
  verts[0].pos = {cx - halfSize,
                  getTerrainHeight(cx - halfSize, cz - halfSize) + hOff,
                  cz - halfSize};
  verts[1].pos = {cx + halfSize,
                  getTerrainHeight(cx + halfSize, cz - halfSize) + hOff,
                  cz - halfSize};
  verts[2].pos = {cx + halfSize,
                  getTerrainHeight(cx + halfSize, cz + halfSize) + hOff,
                  cz + halfSize};
  verts[3].pos = {cx - halfSize,
                  getTerrainHeight(cx - halfSize, cz + halfSize) + hOff,
                  cz + halfSize};
  for (int i = 0; i < 4; ++i)
    verts[i].normal = glm::vec3(0, 1, 0);
  verts[0].tex = {0, 0};
  verts[1].tex = {1, 0};
  verts[2].tex = {1, 1};
  verts[3].tex = {0, 1};

  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
  glBindVertexArray(m_vao);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
  glBindVertexArray(0);
}

void ClickEffect::Init() {
  glGenVertexArrays(1, &m_vao);
  glGenBuffers(1, &m_vbo);
  glBindVertexArray(m_vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER, 4 * sizeof(ViewerVertex), nullptr,
               GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ViewerVertex),
                        (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ViewerVertex),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ViewerVertex),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glBindVertexArray(0);
}

void ClickEffect::LoadAssets(const std::string &dataPath) {
  std::string effectDir = dataPath + "/Effect/";
  m_ringTex = TextureLoader::Resolve(effectDir, "cursorpin02.OZJ");
  m_waveTex = TextureLoader::Resolve(effectDir, "cursorpin01.OZJ");
  m_glowTex = TextureLoader::Resolve(effectDir, "Magic_Ground1.OZJ");
  m_bmd = BMDParser::Parse(effectDir + "MoveTargetPosEffect.bmd");
  if (m_bmd && !m_bmd->Meshes.empty()) {
    std::vector<BoneWorldMatrix> idBones(m_bmd->Bones.size());
    for (auto &b : idBones)
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
          b[r][c] = (r == c) ? 1.0f : 0.0f;
    AABB aabb;
    UploadMeshWithBones(m_bmd->Meshes[0], effectDir, idBones,
                        m_modelBuffers, aabb, true);
    std::cout << "[ClickEffect] Loaded: BMD + " << (m_ringTex ? 1 : 0)
              << " ring, " << (m_waveTex ? 1 : 0) << " wave, "
              << (m_glowTex ? 1 : 0) << " glow textures" << std::endl;
  } else {
    std::cerr << "[ClickEffect] Failed to load MoveTargetPosEffect.bmd"
              << std::endl;
  }
}

void ClickEffect::Show(const glm::vec3 &pos) {
  m_pos = pos;
  m_visible = true;
  m_lifetime = 1.2f;
  m_scale = 1.8f;
  m_shrinking = true;
  m_animFrame = 0.0f;
  m_glowAngle = 0.0f;
  m_waves.clear();
  m_waveTimer = 0.0f;
  m_waves.push_back({1.2f, 1.0f});
}

void ClickEffect::Hide() {
  m_visible = false;
  m_waves.clear();
}

void ClickEffect::Render(const glm::mat4 &view, const glm::mat4 &proj,
                         float deltaTime, Shader *shader) {
  if (!m_visible || !shader || !m_terrainData)
    return;

  // Lifetime countdown
  m_lifetime -= deltaTime;
  if (m_lifetime <= 0.0f) {
    m_visible = false;
    m_waves.clear();
    return;
  }

  // Fade multiplier: fade over last 0.4s
  float fadeMul =
      (m_lifetime < 0.4f) ? (m_lifetime / 0.4f) : 1.0f;

  // Animate pulsing ring
  float pulseSpeed = 0.15f * 25.0f;
  if (m_shrinking) {
    m_scale -= pulseSpeed * deltaTime;
    if (m_scale <= 0.8f) {
      m_scale = 0.8f;
      m_shrinking = false;
    }
  } else {
    m_scale += pulseSpeed * deltaTime;
    if (m_scale >= 1.8f) {
      m_scale = 1.8f;
      m_shrinking = true;
    }
  }

  // Spawn expanding wave rings
  m_waveTimer += deltaTime;
  if (m_waveTimer >= 0.6f) {
    m_waves.push_back({1.2f, 1.0f});
    m_waveTimer -= 0.6f;
  }

  // Update wave rings
  float waveShrink = 0.04f * 25.0f;
  float waveFade = 0.05f * 25.0f;
  for (auto &w : m_waves) {
    w.scale -= waveShrink * deltaTime;
    if (w.scale < 0.6f)
      w.alpha -= waveFade * deltaTime;
  }
  m_waves.erase(
      std::remove_if(m_waves.begin(), m_waves.end(),
                     [](const Wave &w) {
                       return w.scale <= 0.2f || w.alpha <= 0.0f;
                     }),
      m_waves.end());

  // Animate ground glow rotation
  m_glowAngle += 1.5f * deltaTime;

  // Animate BMD model
  if (m_bmd && !m_bmd->Actions.empty()) {
    int numKeys = m_bmd->Actions[0].NumAnimationKeys;
    m_animFrame += ANIM_SPEED * deltaTime;
    if (m_animFrame >= (float)numKeys)
      m_animFrame = std::fmod(m_animFrame, (float)numKeys);
  }

  float cx = m_pos.x, cz = m_pos.z;
  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);

  // Common shader state for ground bitmaps
  shader->use();
  shader->setMat4("projection", proj);
  shader->setMat4("view", view);
  shader->setMat4("model", glm::mat4(1.0f));
  shader->setVec3("lightPos", eye + glm::vec3(0, 500, 0));
  shader->setVec3("viewPos", eye);
  shader->setFloat("objectAlpha", 1.0f);
  shader->setBool("useFog", false);
  shader->setVec2("texCoordOffset", glm::vec2(0.0f));
  shader->setVec3("terrainLight", glm::vec3(1.0f));
  shader->setInt("numPointLights", 0);

  // Additive blending for all cursor effects
  glBlendFunc(GL_ONE, GL_ONE);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glActiveTexture(GL_TEXTURE0);

  // Pass 1: Ground glow (Magic_Ground1)
  if (m_glowTex) {
    shader->setVec3("lightColor", 0.7f * fadeMul, 0.5f * fadeMul,
                    0.2f * fadeMul);
    shader->setFloat("blendMeshLight", fadeMul);
    glBindTexture(GL_TEXTURE_2D, m_glowTex);
    drawGroundQuad(cx, cz, 50.0f, 1.5f);
  }

  // Pass 2: Pulsing ring (cursorpin02)
  if (m_ringTex) {
    shader->setVec3("lightColor", 1.0f * fadeMul, 0.7f * fadeMul,
                    0.3f * fadeMul);
    shader->setFloat("blendMeshLight", fadeMul);
    glBindTexture(GL_TEXTURE_2D, m_ringTex);
    float halfSize = m_scale * 30.0f;
    drawGroundQuad(cx, cz, halfSize, 2.0f);
  }

  // Pass 3: Expanding wave rings (cursorpin01)
  if (m_waveTex && !m_waves.empty()) {
    glBindTexture(GL_TEXTURE_2D, m_waveTex);
    for (auto &w : m_waves) {
      float a = w.alpha * fadeMul;
      shader->setVec3("lightColor", 1.0f * a, 0.7f * a, 0.3f * a);
      shader->setFloat("blendMeshLight", a);
      float halfSize = w.scale * 30.0f;
      drawGroundQuad(cx, cz, halfSize, 2.5f);
    }
  }

  // Pass 4: BMD spinning cone model
  if (m_bmd && !m_modelBuffers.empty() &&
      m_modelBuffers[0].indexCount > 0) {
    auto bones = ComputeBoneMatricesInterpolated(m_bmd.get(), 0, m_animFrame);
    RetransformMeshWithBones(m_bmd->Meshes[0], bones, m_modelBuffers[0]);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), m_pos);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(0.35f));

    shader->setMat4("model", model);
    shader->setVec3("lightColor", 1.0f * fadeMul, 0.7f * fadeMul,
                    0.3f * fadeMul);
    shader->setFloat("blendMeshLight", fadeMul);

    auto &mb = m_modelBuffers[0];
    glBindTexture(GL_TEXTURE_2D, mb.texture);
    glBindVertexArray(mb.vao);
    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
  }

  // Restore state
  glEnable(GL_CULL_FACE);
  glDepthMask(GL_TRUE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void ClickEffect::Cleanup() {
  if (m_vao) {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    m_vao = m_vbo = 0;
  }
  CleanupMeshBuffers(m_modelBuffers);
  m_bmd.reset();
  if (m_ringTex)
    glDeleteTextures(1, &m_ringTex);
  if (m_waveTex)
    glDeleteTextures(1, &m_waveTex);
  if (m_glowTex)
    glDeleteTextures(1, &m_glowTex);
  m_ringTex = m_waveTex = m_glowTex = 0;
}
