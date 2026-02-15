#include "HeroCharacter.hpp"
#include "TextureLoader.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

glm::vec3 HeroCharacter::sampleTerrainLightAt(const glm::vec3 &worldPos) const {
  const int SIZE = 256;
  if (m_terrainLightmap.size() < (size_t)(SIZE * SIZE))
    return glm::vec3(1.0f);

  float gz = worldPos.x / 100.0f;
  float gx = worldPos.z / 100.0f;
  int xi = (int)gx, zi = (int)gz;
  if (xi < 0 || zi < 0 || xi > SIZE - 2 || zi > SIZE - 2)
    return glm::vec3(0.5f);

  float xd = gx - (float)xi, zd = gz - (float)zi;
  const glm::vec3 &c00 = m_terrainLightmap[zi * SIZE + xi];
  const glm::vec3 &c10 = m_terrainLightmap[zi * SIZE + (xi + 1)];
  const glm::vec3 &c01 = m_terrainLightmap[(zi + 1) * SIZE + xi];
  const glm::vec3 &c11 = m_terrainLightmap[(zi + 1) * SIZE + (xi + 1)];
  glm::vec3 left = c00 + (c01 - c00) * zd;
  glm::vec3 right = c10 + (c11 - c10) * zd;
  return left + (right - left) * xd;
}

void HeroCharacter::Init(const std::string &dataPath) {
  std::string playerPath = dataPath + "/Player/";

  // Load skeleton (Player.bmd — bones + actions, zero meshes)
  m_skeleton = BMDParser::Parse(playerPath + "player.bmd");
  if (!m_skeleton) {
    std::cerr << "[Hero] Failed to load Player.bmd skeleton" << std::endl;
    return;
  }
  std::cout << "[Hero] Player.bmd: " << m_skeleton->Bones.size()
            << " bones, " << m_skeleton->Actions.size() << " actions"
            << std::endl;

  // Load DK Naked body parts (Class02)
  const char *partFiles[] = {"HelmClass02.bmd", "ArmorClass02.bmd",
                             "PantClass02.bmd", "GloveClass02.bmd",
                             "BootClass02.bmd"};

  auto bones = ComputeBoneMatrices(m_skeleton.get());
  AABB totalAABB{};

  for (int p = 0; p < PART_COUNT; ++p) {
    std::string fullPath = playerPath + partFiles[p];
    auto bmd = BMDParser::Parse(fullPath);
    if (!bmd) {
      std::cerr << "[Hero] Failed to load: " << partFiles[p] << std::endl;
      continue;
    }

    for (auto &mesh : bmd->Meshes) {
      UploadMeshWithBones(mesh, playerPath, bones, m_parts[p].meshBuffers,
                          totalAABB, true);
    }
    m_parts[p].bmd = std::move(bmd);
    std::cout << "[Hero] Loaded " << partFiles[p] << std::endl;
  }

  // Create shader (same model.vert/frag as ObjectRenderer)
  std::ifstream shaderTest("shaders/model.vert");
  m_shader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/model.vert" : "../shaders/model.vert",
      shaderTest.good() ? "shaders/model.frag" : "../shaders/model.frag");

  // Cache root bone index and log walk animation info
  if (m_skeleton) {
    for (int i = 0; i < (int)m_skeleton->Bones.size(); ++i) {
      if (m_skeleton->Bones[i].Parent == -1 &&
          !m_skeleton->Bones[i].Dummy) {
        m_rootBone = i;
        break;
      }
    }
    const int WALK_ACTION = 15;
    if (m_rootBone >= 0 &&
        WALK_ACTION < (int)m_skeleton->Actions.size()) {
      int numKeys = m_skeleton->Actions[WALK_ACTION].NumAnimationKeys;
      auto &bm = m_skeleton->Bones[m_rootBone].BoneMatrixes[WALK_ACTION];
      if ((int)bm.Position.size() >= numKeys && numKeys > 1) {
        glm::vec3 p0 = bm.Position[0];
        glm::vec3 pN = bm.Position[numKeys - 1];
        float strideY = pN.y - p0.y;
        std::cout << "[Hero] Root bone " << m_rootBone
                  << ": walk stride=" << strideY << " MU-Y over "
                  << numKeys << " keys, LockPositions="
                  << m_skeleton->Actions[WALK_ACTION].LockPositions
                  << std::endl;
      }
    }
  }
  // Create shadow shader
  m_shadowShader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/shadow.vert" : "../shaders/shadow.vert",
      shaderTest.good() ? "shaders/shadow.frag" : "../shaders/shadow.frag");

  // Create shadow mesh buffers (position-only VBO for each body part mesh)
  for (int p = 0; p < PART_COUNT; ++p) {
    if (!m_parts[p].bmd)
      continue;
    for (auto &mb : m_parts[p].meshBuffers) {
      ShadowMesh sm;
      sm.vertexCount = mb.vertexCount;
      sm.indexCount = mb.vertexCount; // sequential indices = vertex count
      if (sm.vertexCount == 0) {
        m_shadowMeshes.push_back(sm);
        continue;
      }
      glGenVertexArrays(1, &sm.vao);
      glGenBuffers(1, &sm.vbo);
      glBindVertexArray(sm.vao);
      glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
      glBufferData(GL_ARRAY_BUFFER, sm.vertexCount * sizeof(glm::vec3), nullptr,
                   GL_DYNAMIC_DRAW);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3),
                            (void *)0);
      glEnableVertexAttribArray(0);
      glBindVertexArray(0);
      m_shadowMeshes.push_back(sm);
    }
  }

  std::cout << "[Hero] Character initialized (DK Naked)" << std::endl;
}

void HeroCharacter::Render(const glm::mat4 &view, const glm::mat4 &proj,
                           const glm::vec3 &camPos, float deltaTime) {
  if (!m_skeleton || !m_shader)
    return;

  // Advance animation
  int numKeys = 1;
  bool lockPos = false;
  if (m_action >= 0 && m_action < (int)m_skeleton->Actions.size()) {
    numKeys = m_skeleton->Actions[m_action].NumAnimationKeys;
    lockPos = m_skeleton->Actions[m_action].LockPositions;
  }
  if (numKeys > 1) {
    m_animFrame += ANIM_SPEED * deltaTime;
    int wrapKeys = lockPos ? (numKeys - 1) : numKeys;
    if (m_animFrame >= (float)wrapKeys)
      m_animFrame = std::fmod(m_animFrame, (float)wrapKeys);
  }

  // Compute bones for current animation frame
  auto bones = ComputeBoneMatricesInterpolated(m_skeleton.get(),
                                               m_action, m_animFrame);

  // LockPositions: root bone X/Y locked to frame 0
  if (lockPos && m_rootBone >= 0) {
    int i = m_rootBone;
    auto &bm = m_skeleton->Bones[i].BoneMatrixes[m_action];
    if (!bm.Position.empty()) {
      float dx = bones[i][0][3] - bm.Position[0].x;
      float dy = bones[i][1][3] - bm.Position[0].y;
      for (int b = 0; b < (int)bones.size(); ++b) {
        bones[b][0][3] -= dx;
        bones[b][1][3] -= dy;
      }
    }
  }

  // Cache bones for shadow rendering
  m_cachedBones = bones;

  // Re-skin all body part meshes
  for (int p = 0; p < PART_COUNT; ++p) {
    if (!m_parts[p].bmd)
      continue;
    for (int mi = 0; mi < (int)m_parts[p].meshBuffers.size() &&
                      mi < (int)m_parts[p].bmd->Meshes.size();
         ++mi) {
      RetransformMeshWithBones(m_parts[p].bmd->Meshes[mi], bones,
                               m_parts[p].meshBuffers[mi]);
    }
  }

  // Build model matrix: translate -> MU->GL coord conversion -> facing rotation
  glm::mat4 model = glm::translate(glm::mat4(1.0f), m_pos);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, m_facing, glm::vec3(0, 0, 1));

  m_shader->use();
  m_shader->setMat4("projection", proj);
  m_shader->setMat4("view", view);
  m_shader->setMat4("model", model);

  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
  m_shader->setVec3("lightPos", eye + glm::vec3(0, 500, 0));
  m_shader->setVec3("lightColor", 1.0f, 1.0f, 1.0f);
  m_shader->setVec3("viewPos", eye);
  m_shader->setBool("useFog", true);
  m_shader->setFloat("blendMeshLight", 1.0f);
  m_shader->setFloat("objectAlpha", 1.0f);
  m_shader->setVec2("texCoordOffset", glm::vec2(0.0f));
  m_shader->setFloat("luminosity", m_luminosity);

  // Terrain lightmap at hero position
  glm::vec3 tLight = sampleTerrainLightAt(m_pos);
  m_shader->setVec3("terrainLight", tLight);

  // Point lights
  int plCount = std::min((int)m_pointLights.size(), MAX_POINT_LIGHTS);
  m_shader->setInt("numPointLights", plCount);
  for (int i = 0; i < plCount; ++i) {
    std::string idx = std::to_string(i);
    m_shader->setVec3("pointLightPos[" + idx + "]", m_pointLights[i].position);
    m_shader->setVec3("pointLightColor[" + idx + "]", m_pointLights[i].color);
    m_shader->setFloat("pointLightRange[" + idx + "]", m_pointLights[i].range);
  }

  // Draw all body part meshes
  for (int p = 0; p < PART_COUNT; ++p) {
    for (auto &mb : m_parts[p].meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;

      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);

      if (mb.noneBlend) {
        glDisable(GL_BLEND);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        glEnable(GL_BLEND);
      } else if (mb.bright) {
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      } else {
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      }
    }
  }
}

void HeroCharacter::RenderShadow(const glm::mat4 &view, const glm::mat4 &proj) {
  if (!m_skeleton || !m_shadowShader || m_cachedBones.empty() ||
      m_shadowMeshes.empty())
    return;

  // Shadow model matrix: NO facing rotation (facing is baked into vertices
  // before shadow projection so the shadow direction stays fixed in world space)
  glm::mat4 model = glm::translate(glm::mat4(1.0f), m_pos);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));

  m_shadowShader->use();
  m_shadowShader->setMat4("projection", proj);
  m_shadowShader->setMat4("view", view);
  m_shadowShader->setMat4("model", model);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.0f, -1.0f);
  glDisable(GL_CULL_FACE);

  // Stencil: only draw each shadow pixel once (prevents overlap darkening)
  glEnable(GL_STENCIL_TEST);
  glClear(GL_STENCIL_BUFFER_BIT);
  glStencilFunc(GL_EQUAL, 0, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

  // Shadow projection constants (from ZzzBMD.cpp RenderBodyShadow)
  const float sx = 2000.0f;
  const float sy = 4000.0f;

  // Pre-compute facing rotation in MU-local space (around MU Z = height axis)
  float cosF = cosf(m_facing);
  float sinF = sinf(m_facing);

  int smIdx = 0;
  for (int p = 0; p < PART_COUNT; ++p) {
    if (!m_parts[p].bmd)
      continue;
    for (int mi = 0; mi < (int)m_parts[p].bmd->Meshes.size() &&
                      smIdx < (int)m_shadowMeshes.size();
         ++mi, ++smIdx) {
      auto &sm = m_shadowMeshes[smIdx];
      if (sm.vertexCount == 0 || sm.vao == 0)
        continue;

      auto &mesh = m_parts[p].bmd->Meshes[mi];
      std::vector<glm::vec3> shadowVerts;
      shadowVerts.reserve(sm.vertexCount);

      for (int i = 0; i < mesh.NumTriangles; ++i) {
        auto &tri = mesh.Triangles[i];
        int steps = (tri.Polygon == 3) ? 3 : 4;
        for (int v = 0; v < 3; ++v) {
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 pos = srcVert.Position;
          int boneIdx = srcVert.Node;
          if (boneIdx >= 0 && boneIdx < (int)m_cachedBones.size()) {
            pos = MuMath::TransformPoint(
                (const float(*)[4])m_cachedBones[boneIdx].data(), pos);
          }
          // Apply facing rotation in MU space (around Z/height axis)
          float rx = pos.x * cosF - pos.y * sinF;
          float ry = pos.x * sinF + pos.y * cosF;
          pos.x = rx;
          pos.y = ry;
          // Shadow projection in MU-local space
          if (pos.z < sy) {
            float factor = 1.0f / (pos.z - sy);
            pos.x += pos.z * (pos.x + sx) * factor;
            pos.y += pos.z * (pos.y + sx) * factor;
          }
          pos.z = 5.0f;
          shadowVerts.push_back(pos);
        }
        if (steps == 4) {
          int quadIndices[3] = {0, 2, 3};
          for (int v : quadIndices) {
            auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
            glm::vec3 pos = srcVert.Position;
            int boneIdx = srcVert.Node;
            if (boneIdx >= 0 && boneIdx < (int)m_cachedBones.size()) {
              pos = MuMath::TransformPoint(
                  (const float(*)[4])m_cachedBones[boneIdx].data(), pos);
            }
            float rx = pos.x * cosF - pos.y * sinF;
            float ry = pos.x * sinF + pos.y * cosF;
            pos.x = rx;
            pos.y = ry;
            if (pos.z < sy) {
              float factor = 1.0f / (pos.z - sy);
              pos.x += pos.z * (pos.x + sx) * factor;
              pos.y += pos.z * (pos.y + sx) * factor;
            }
            pos.z = 5.0f;
            shadowVerts.push_back(pos);
          }
        }
      }

      glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
      glBufferSubData(GL_ARRAY_BUFFER, 0,
                      shadowVerts.size() * sizeof(glm::vec3),
                      shadowVerts.data());
      glBindVertexArray(sm.vao);
      glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shadowVerts.size());
    }
  }

  glBindVertexArray(0);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
}

void HeroCharacter::ProcessMovement(float deltaTime) {
  if (!m_terrainData || !m_moving)
    return;

  glm::vec3 dir = m_target - m_pos;
  dir.y = 0;
  float dist = glm::length(dir);

  if (dist < 10.0f) {
    StopMoving();
  } else {
    dir = glm::normalize(dir);
    glm::vec3 step = dir * m_speed * deltaTime;
    glm::vec3 newPos = m_pos + step;

    const int S = TerrainParser::TERRAIN_SIZE;
    int gz = (int)(newPos.x / 100.0f);
    int gx = (int)(newPos.z / 100.0f);
    bool walkable = (gx >= 0 && gz >= 0 && gx < S && gz < S) &&
                    (m_terrainData->mapping.attributes[gz * S + gx] & 0x04) == 0;

    if (walkable) {
      m_pos.x = newPos.x;
      m_pos.z = newPos.z;
    } else {
      StopMoving();
    }
  }

  SnapToTerrain();
}

void HeroCharacter::MoveTo(const glm::vec3 &target) {
  m_target = target;
  if (!m_moving) {
    m_action = 15; // PLAYER_WALK_MALE
    m_animFrame = 0.0f;
  }
  m_moving = true;
  // Compute facing angle
  float dx = target.x - m_pos.x;
  float dz = target.z - m_pos.z;
  m_facing = atan2f(dz, -dx);
}

void HeroCharacter::StopMoving() {
  m_moving = false;
  m_action = 1; // PLAYER_STOP1 (male idle)
  m_animFrame = 0.0f;
}

void HeroCharacter::SnapToTerrain() {
  if (!m_terrainData)
    return;
  const int S = TerrainParser::TERRAIN_SIZE;
  float gz = m_pos.x / 100.0f;
  float gx = m_pos.z / 100.0f;
  gz = std::clamp(gz, 0.0f, (float)(S - 2));
  gx = std::clamp(gx, 0.0f, (float)(S - 2));
  int xi = (int)gx, zi = (int)gz;
  float xd = gx - (float)xi, zd = gz - (float)zi;
  float h00 = m_terrainData->heightmap[zi * S + xi];
  float h10 = m_terrainData->heightmap[zi * S + (xi + 1)];
  float h01 = m_terrainData->heightmap[(zi + 1) * S + xi];
  float h11 = m_terrainData->heightmap[(zi + 1) * S + (xi + 1)];
  m_pos.y = h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
            h01 * (1 - xd) * zd + h11 * xd * zd;
}

void HeroCharacter::Cleanup() {
  for (int p = 0; p < PART_COUNT; ++p)
    CleanupMeshBuffers(m_parts[p].meshBuffers);
  for (auto &sm : m_shadowMeshes) {
    if (sm.vao)
      glDeleteVertexArrays(1, &sm.vao);
    if (sm.vbo)
      glDeleteBuffers(1, &sm.vbo);
    if (sm.ebo)
      glDeleteBuffers(1, &sm.ebo);
  }
  m_shadowMeshes.clear();
  m_shader.reset();
  m_shadowShader.reset();
  m_skeleton.reset();
}
