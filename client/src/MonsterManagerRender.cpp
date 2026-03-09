#include "MonsterManager.hpp"
#include "TerrainUtils.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <iostream>

void MonsterManager::Render(const glm::mat4 &view, const glm::mat4 &proj,
                            const glm::vec3 &camPos, float deltaTime) {
  if (!m_shader || m_monsters.empty())
    return;

  // Extract frustum planes from VP matrix for culling
  glm::mat4 vp = proj * view;
  glm::vec4 frustum[6];
  frustum[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0],
                          vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // Left
  frustum[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0],
                          vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // Right
  frustum[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1],
                          vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // Bottom
  frustum[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1],
                          vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // Top
  frustum[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2],
                          vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]); // Near
  frustum[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2],
                          vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // Far
  for (int i = 0; i < 6; ++i)
    frustum[i] /= glm::length(glm::vec3(frustum[i]));

  m_shader->use();
  m_shader->setMat4("projection", proj);
  m_shader->setMat4("view", view);

  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
  m_shader->setVec3("lightPos", eye + glm::vec3(0, 500, 0));
  m_shader->setVec3("lightColor", 1.0f, 1.0f, 1.0f);
  m_shader->setVec3("viewPos", eye);
  m_shader->setBool("useFog", true);
  if (m_mapId == 1) { // Dungeon: black fog
    m_shader->setVec3("uFogColor", glm::vec3(0.0f));
    m_shader->setFloat("uFogNear", 800.0f);
    m_shader->setFloat("uFogFar", 2500.0f);
  } else { // Lorencia: warm brown fog
    m_shader->setVec3("uFogColor", glm::vec3(0.117f, 0.078f, 0.039f));
    m_shader->setFloat("uFogNear", 1500.0f);
    m_shader->setFloat("uFogFar", 3500.0f);
  }
  m_shader->setFloat("blendMeshLight", 1.0f);
  m_shader->setVec2("texCoordOffset", glm::vec2(0.0f));
  m_shader->setFloat("luminosity", m_luminosity);
  m_shader->setVec3("baseTint", glm::vec3(1.0f));
  m_shader->setVec3("glowColor", glm::vec3(0.0f));
  m_shader->setInt("chromeMode", 0);

  // Point lights (pre-cached locations)
  int plCount = std::min((int)m_pointLights.size(), MAX_POINT_LIGHTS);
  m_shader->uploadPointLights(plCount, m_pointLights.data());

  // Disable face culling — spider legs are thin planar geometry visible from
  // both sides
  glDisable(GL_CULL_FACE);

  for (auto &mon : m_monsters) {
    // Skip fully faded corpses
    if (mon.state == MonsterState::DEAD && mon.corpseAlpha <= 0.01f)
      continue;

    auto &mdl = m_models[mon.modelIdx];

    // Frustum culling: skip entities fully outside view frustum
    {
      float cullRadius = mdl.collisionHeight * mon.scale * 2.0f;
      glm::vec3 center = mon.position + glm::vec3(0, cullRadius * 0.4f, 0);
      bool outside = false;
      for (int p = 0; p < 6; ++p) {
        if (frustum[p].x * center.x + frustum[p].y * center.y +
                frustum[p].z * center.z + frustum[p].w <
            -cullRadius) {
          outside = true;
          break;
        }
      }
      if (outside)
        continue;
    }

    // Advance animation (use animBmd + actionMap for skeleton types)
    BMDData *animBmd = mdl.getAnimBmd();
    int mappedAction = (mon.action >= 0 && mon.action < 7)
                           ? mdl.actionMap[mon.action]
                           : mon.action;
    int numKeys = 1;
    bool lockPos = false;
    if (mappedAction >= 0 && mappedAction < (int)animBmd->Actions.size()) {
      numKeys = animBmd->Actions[mappedAction].NumAnimationKeys;
      lockPos = animBmd->Actions[mappedAction].LockPositions;
    }
    if (numKeys > 1) {
      float animSpeed = getAnimSpeed(mon.monsterType, mon.action);

      // Scale walk animation speed to match actual movement speed.
      // refMoveSpeed = the speed the walk animation was designed for.
      // MU Online MoveSpeed=400 means 400ms per grid cell = 100/0.4 = 250 u/s.
      float refMoveSpeed = 250.0f;
      // Skeleton types 14-16 use Player.bmd walk animation (stride at player speed)
      if (mon.monsterType >= 14 && mon.monsterType <= 16)
        refMoveSpeed = 334.0f;

      if (mon.action == ACTION_WALK) {
        if (mon.state == MonsterState::WALKING)
          animSpeed *= WANDER_SPEED / refMoveSpeed;
        else if (mon.state == MonsterState::CHASING)
          animSpeed *= CHASE_SPEED / refMoveSpeed;
      }

      mon.animFrame += animSpeed * deltaTime;

      // Die animation doesn't loop
      if (mon.state == MonsterState::DYING || mon.state == MonsterState::DEAD) {
        if (mon.animFrame >= (float)(numKeys - 1))
          mon.animFrame = (float)(numKeys - 1);
      } else {
        // LockPositions actions wrap at numKeys-1 (last frame == first frame)
        int wrapKeys = lockPos ? (numKeys - 1) : numKeys;
        if (wrapKeys < 1)
          wrapKeys = 1;
        if (mon.animFrame >= (float)wrapKeys)
          mon.animFrame = std::fmod(mon.animFrame, (float)wrapKeys);
      }
    }

    // Advance blending Alpha
    if (mon.isBlending) {
      mon.blendAlpha += deltaTime / mon.BLEND_DURATION;
      if (mon.blendAlpha >= 1.0f) {
        mon.blendAlpha = 1.0f;
        mon.isBlending = false;
      }
    }

    // Compute bone matrices with blending support (animBmd for skeleton types)
    int mappedPrior = (mon.priorAction >= 0 && mon.priorAction < 7)
                          ? mdl.actionMap[mon.priorAction]
                          : mon.priorAction;
    std::vector<BoneWorldMatrix> bones;
    if (mon.isBlending && mon.priorAction != -1) {
      bones = ComputeBoneMatricesBlended(animBmd, mappedPrior,
                                         mon.priorAnimFrame, mappedAction,
                                         mon.animFrame, mon.blendAlpha);
    } else {
      bones =
          ComputeBoneMatricesInterpolated(animBmd, mappedAction, mon.animFrame);
    }

    // LockPositions: cancel root bone X/Y displacement to prevent animation
    // from physically moving the model. In blending mode, we interpolate the
    // offset.
    if (mdl.rootBone >= 0) {
      int rb = mdl.rootBone;
      float dx = 0.0f, dy = 0.0f;

      if (mon.isBlending && mon.priorAction != -1) {
        bool lock1 = mappedPrior < (int)animBmd->Actions.size() &&
                     animBmd->Actions[mappedPrior].LockPositions;
        bool lock2 = mappedAction < (int)animBmd->Actions.size() &&
                     animBmd->Actions[mappedAction].LockPositions;

        float dx1 = 0.0f, dy1 = 0.0f, dx2 = 0.0f, dy2 = 0.0f;
        if (lock1) {
          auto &bm1 = animBmd->Bones[rb].BoneMatrixes[mappedPrior];
          if (!bm1.Position.empty()) {
            glm::vec3 p;
            glm::vec4 q;
            if (GetInterpolatedBoneData(animBmd, mappedPrior,
                                        mon.priorAnimFrame, rb, p, q)) {
              dx1 = p.x - bm1.Position[0].x;
              dy1 = p.y - bm1.Position[0].y;
            }
          }
        }
        if (lock2) {
          auto &bm2 = animBmd->Bones[rb].BoneMatrixes[mappedAction];
          if (!bm2.Position.empty()) {
            glm::vec3 p;
            glm::vec4 q;
            if (GetInterpolatedBoneData(animBmd, mappedAction, mon.animFrame,
                                        rb, p, q)) {
              dx2 = p.x - bm2.Position[0].x;
              dy2 = p.y - bm2.Position[0].y;
            }
          }
        }
        dx = dx1 * (1.0f - mon.blendAlpha) + dx2 * mon.blendAlpha;
        dy = dy1 * (1.0f - mon.blendAlpha) + dy2 * mon.blendAlpha;
      } else if (mappedAction >= 0 &&
                 mappedAction < (int)animBmd->Actions.size() &&
                 animBmd->Actions[mappedAction].LockPositions) {
        auto &bm = animBmd->Bones[rb].BoneMatrixes[mappedAction];
        if (!bm.Position.empty()) {
          dx = bones[rb][0][3] - bm.Position[0].x;
          dy = bones[rb][1][3] - bm.Position[0].y;
        }
      }

      if (dx != 0.0f || dy != 0.0f) {
        for (int b = 0; b < (int)bones.size(); ++b) {
          bones[b][0][3] -= dx;
          bones[b][1][3] -= dy;
        }
      }
    }

    mon.cachedBones = bones;

    // Monster ambient VFX (Main 5.2: MoveCharacterVisual)
    if (m_vfxManager && mon.state != MonsterState::DYING &&
        mon.state != MonsterState::DEAD) {
      mon.ambientVfxTimer += deltaTime;

      // Budge Dragon (type 2): fire breath during ATTACK1 only (bone 7 = mouth)
      // Main 5.2: 1 particle per tick, frames 0-4, offset (0, 32-64, 0) in bone
      // space
      if (mon.monsterType == 2 && mon.action == ACTION_ATTACK1 &&
          mon.animFrame <= 4.0f) {
        if (7 < (int)bones.size()) {
          glm::mat4 modelRot = glm::mat4(1.0f);
          modelRot =
              glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 0, 1));
          modelRot =
              glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
          modelRot = glm::rotate(modelRot, mon.facing, glm::vec3(0, 0, 1));

          glm::vec3 localOff(0.0f, (float)(rand() % 32 + 32), 0.0f);

          auto applyFireToBone = [&](int boneIdx) {
            const auto &bm = bones[boneIdx];
            glm::vec3 worldOff;
            worldOff.x = bm[0][0] * localOff.x + bm[0][1] * localOff.y +
                         bm[0][2] * localOff.z;
            worldOff.y = bm[1][0] * localOff.x + bm[1][1] * localOff.y +
                         bm[1][2] * localOff.z;
            worldOff.z = bm[2][0] * localOff.x + bm[2][1] * localOff.y +
                         bm[2][2] * localOff.z;
            glm::vec3 bonePos(bm[0][3], bm[1][3], bm[2][3]);
            glm::vec3 localPos = (bonePos + worldOff);
            glm::vec3 worldPos =
                glm::vec3(modelRot * glm::vec4(localPos, 1.0f));
            glm::vec3 firePos = worldPos * mon.scale + mon.position;
            m_vfxManager->SpawnBurst(ParticleType::FIRE, firePos, 1);
          };

          applyFireToBone(7);
        }
      }

      // Lich (type 6) / Thunder Lich (type 9): fire VFX along entire staff
      if (mon.monsterType == 6 || mon.monsterType == 9) {
        bool wantAttackFire =
            mon.action == ACTION_ATTACK1 && mon.animFrame <= 4.0f;
        bool wantAmbientFire = mon.ambientVfxTimer >= 0.08f;

        if (wantAttackFire || wantAmbientFire) {
          // Find staff weapon def (bone 41)
          const WeaponDef *staffDef = nullptr;
          for (const auto &wd : mdl.weaponDefs) {
            if (wd.attachBone == 41 && wd.bmd) {
              staffDef = &wd;
              break;
            }
          }

          glm::vec3 staffTopLocal(0.0f), staffBottomLocal(0.0f);
          bool haveStaff = false;

          if (staffDef && staffDef->attachBone < (int)bones.size()) {
            // Weapon transform chain (same as weapon rendering code)
            const auto &parentBone = bones[staffDef->attachBone];
            BoneWorldMatrix weaponLocal =
                MuMath::BuildWeaponOffsetMatrix(staffDef->rot,
                                                staffDef->offset);
            BoneWorldMatrix parentMat;
            MuMath::ConcatTransforms((const float(*)[4])parentBone.data(),
                                     (const float(*)[4])weaponLocal.data(),
                                     (float(*)[4])parentMat.data());

            const auto &wLocalBones = staffDef->cachedLocalBones;
            std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
            for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
              MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                       (const float(*)[4])wLocalBones[bi].data(),
                                       (float(*)[4])wFinalBones[bi].data());
            }

            // Skin all staff vertices to model-local space
            glm::vec3 handBonePos(bones[staffDef->attachBone][0][3],
                                  bones[staffDef->attachBone][1][3],
                                  bones[staffDef->attachBone][2][3]);
            std::vector<glm::vec3> skinnedVerts;
            for (const auto &mesh : staffDef->bmd->Meshes) {
              for (const auto &vert : mesh.Vertices) {
                int ni = std::clamp((int)vert.Node, 0,
                                    (int)wFinalBones.size() - 1);
                const auto &bm = wFinalBones[ni];
                glm::vec3 vp;
                vp.x = bm[0][0] * vert.Position[0] +
                       bm[0][1] * vert.Position[1] +
                       bm[0][2] * vert.Position[2] + bm[0][3];
                vp.y = bm[1][0] * vert.Position[0] +
                       bm[1][1] * vert.Position[1] +
                       bm[1][2] * vert.Position[2] + bm[1][3];
                vp.z = bm[2][0] * vert.Position[0] +
                       bm[2][1] * vert.Position[1] +
                       bm[2][2] * vert.Position[2] + bm[2][3];
                skinnedVerts.push_back(vp);
              }
            }

            // Find tip (farthest vertex from hand bone)
            float maxDist = 0.0f;
            staffTopLocal = handBonePos;
            for (const auto &vp : skinnedVerts) {
              float d = glm::length(vp - handBonePos);
              if (d > maxDist) {
                maxDist = d;
                staffTopLocal = vp;
              }
            }
            // Find bottom (farthest vertex from tip = opposite end)
            float maxDist2 = 0.0f;
            staffBottomLocal = staffTopLocal;
            for (const auto &vp : skinnedVerts) {
              float d = glm::length(vp - staffTopLocal);
              if (d > maxDist2) {
                maxDist2 = d;
                staffBottomLocal = vp;
              }
            }
            haveStaff = true;
          }

          if (haveStaff) {
            glm::mat4 modelRot = glm::mat4(1.0f);
            modelRot = glm::rotate(modelRot, glm::radians(-90.0f),
                                   glm::vec3(0, 0, 1));
            modelRot = glm::rotate(modelRot, glm::radians(-90.0f),
                                   glm::vec3(0, 1, 0));
            modelRot =
                glm::rotate(modelRot, mon.facing, glm::vec3(0, 0, 1));

            // Spawn fire along entire staff (bottom -> top)
            auto spawnFireAt = [&](float t) {
              glm::vec3 p = glm::mix(staffBottomLocal, staffTopLocal, t);
              glm::vec3 scatter((float)(rand() % 12 - 6),
                                (float)(rand() % 12 - 6),
                                (float)(rand() % 12 - 6));
              glm::vec3 worldPos =
                  glm::vec3(modelRot * glm::vec4(p + scatter, 1.0f));
              glm::vec3 firePos = worldPos * mon.scale + mon.position;
              m_vfxManager->SpawnBurst(ParticleType::FIRE, firePos, 1);
            };

            if (wantAttackFire) {
              for (int i = 0; i < 5; ++i)
                spawnFireAt((float)(rand() % 100) / 100.0f);
            }
            if (wantAmbientFire) {
              for (int i = 0; i < 3; ++i)
                spawnFireAt((float)(rand() % 100) / 100.0f);
              mon.ambientVfxTimer = 0.0f;
            }
          }
        }
      }

      // Ghost (type 11): ambient fire along staff (body bones 31=R Hand, 34=topp)
      // Main 5.2: Ghost has fire VFX similar to Lich, but staff is body mesh
      if (mon.monsterType == 11 && mon.ambientVfxTimer >= 0.08f) {
        if (31 < (int)bones.size() && 34 < (int)bones.size()) {
          glm::mat4 modelRot = glm::mat4(1.0f);
          modelRot =
              glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 0, 1));
          modelRot =
              glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
          modelRot = glm::rotate(modelRot, mon.facing, glm::vec3(0, 0, 1));

          glm::vec3 handLocal(bones[31][0][3], bones[31][1][3],
                              bones[31][2][3]);
          glm::vec3 tipLocal(bones[34][0][3], bones[34][1][3],
                             bones[34][2][3]);

          for (int i = 0; i < 2; ++i) {
            float t = (float)(rand() % 100) / 100.0f;
            glm::vec3 p = glm::mix(handLocal, tipLocal, t);
            glm::vec3 scatter((float)(rand() % 10 - 5),
                              (float)(rand() % 10 - 5),
                              (float)(rand() % 10 - 5));
            glm::vec3 worldPos =
                glm::vec3(modelRot * glm::vec4(p + scatter, 1.0f));
            glm::vec3 firePos = worldPos * mon.scale + mon.position;
            m_vfxManager->SpawnBurst(ParticleType::FIRE, firePos, 1);
          }
          mon.ambientVfxTimer = 0.0f;
        }
      }

      // Gorgon (type 18): ambient fire from random bones + red terrain glow
      // Main 5.2: 10 fire particles per tick from random bones, red light (1,0.2,0)
      if (mon.monsterType == 18 && mon.ambientVfxTimer >= 0.08f) {
        int numBones = (int)bones.size();
        if (numBones > 1) {
          glm::mat4 modelRot = glm::mat4(1.0f);
          modelRot =
              glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 0, 1));
          modelRot =
              glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
          modelRot = glm::rotate(modelRot, mon.facing, glm::vec3(0, 0, 1));

          for (int i = 0; i < 3; ++i) {
            int boneIdx = rand() % numBones;
            const auto &bm = bones[boneIdx];
            glm::vec3 boneLocal(bm[0][3], bm[1][3], bm[2][3]);
            glm::vec3 scatter((float)(rand() % 20 - 10),
                              (float)(rand() % 20 - 10),
                              (float)(rand() % 20 - 10));
            glm::vec3 worldPos =
                glm::vec3(modelRot * glm::vec4(boneLocal + scatter, 1.0f));
            glm::vec3 firePos = worldPos * mon.scale + mon.position;
            m_vfxManager->SpawnBurst(ParticleType::FIRE, firePos, 1);
          }
          mon.ambientVfxTimer = 0.0f;
        }
      }

      // Ambient smoke: Hound (1), Budge Dragon (2), Hell Hound (5)
      // Main 5.2: rand()%4 per tick (~25fps) = ~6/sec. At 60fps, use timer.
      if ((mon.monsterType == 1 || mon.monsterType == 2 ||
           mon.monsterType == 5) &&
          mon.ambientVfxTimer >= 0.5f) {
        mon.ambientVfxTimer = 0.0f;
        glm::vec3 smokePos =
            mon.position + glm::vec3((float)(rand() % 64 - 32),
                                     20.0f + (float)(rand() % 30),
                                     (float)(rand() % 64 - 32));
        m_vfxManager->SpawnBurst(ParticleType::SMOKE, smokePos, 1);
      }
    }

    // Re-skin meshes
    for (int mi = 0;
         mi < (int)mon.meshBuffers.size() && mi < (int)mdl.bmd->Meshes.size();
         ++mi) {
      RetransformMeshWithBones(mdl.bmd->Meshes[mi], bones,
                               mon.meshBuffers[mi]);
    }

    // Build model matrix
    glm::mat4 model = glm::translate(glm::mat4(1.0f), mon.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, mon.facing, glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(mon.scale));

    m_shader->setMat4("model", model);

    // Terrain lightmap at monster position
    glm::vec3 tLight = sampleTerrainLightAt(mon.position);
    // Elite Bull Fighter (type 4): darker skin tone (Main 5.2 visual reference)
    if (mon.monsterType == 4)
      tLight *= 0.45f;
    else if (mon.monsterType == 5) // Hell Hound: darker body
      tLight *= 0.3f;
    else if (mon.monsterType == 8) // Poison Bull: green tint
      tLight *= glm::vec3(0.5f, 0.9f, 0.5f);
    else if (mon.monsterType == 11) // Ghost: spectral blue tint, darken skin
      tLight *= glm::vec3(0.3f, 0.4f, 0.6f);
    m_shader->setVec3("terrainLight", tLight);

    // Spawn fade-in (0->1 over ~0.4s)
    if (mon.spawnAlpha < 1.0f) {
      mon.spawnAlpha += deltaTime * 2.5f; // ~0.4s fade-in
      if (mon.spawnAlpha > 1.0f)
        mon.spawnAlpha = 1.0f;
    }

    // Combined alpha: corpse fade * spawn fade-in * per-type alpha
    float renderAlpha = mon.corpseAlpha * mon.spawnAlpha * mdl.typeAlpha;
    m_shader->setFloat("objectAlpha", renderAlpha);

    // BlendMesh UV scroll (Main 5.2: Lich — texCoordV scrolls over time)
    // -(float)((int)(WorldTime)%2000)*0.0005f
    bool hasBlendMesh = (mdl.blendMesh >= 0);
    float blendMeshUVOffset = 0.0f;
    if (hasBlendMesh) {
      int wt = (int)(m_worldTime * 1000.0f) % 2000;
      blendMeshUVOffset = -(float)wt * 0.0005f;
    }
    // Ghost/Gorgon BlendMeshLight flicker (Main 5.2: (float)(rand()%10)*0.1f)
    float blendMeshLightVal = 1.0f;
    if (mon.monsterType == 11 || mon.monsterType == 18) {
      // Smooth flicker instead of pure random to avoid strobing at 60fps
      float phase = m_worldTime * 3.0f + (float)mon.serverIndex * 2.1f;
      blendMeshLightVal = 0.4f + 0.5f * (0.5f + 0.5f * std::sin(phase));
    }

    // Draw all meshes
    for (auto &mb : mon.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      // Main 5.2 HiddenMesh: skip mesh with matching Texture index
      if (mdl.hiddenMesh >= 0 && mb.bmdTextureId == mdl.hiddenMesh)
        continue;
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);

      // Main 5.2 BlendMesh: mesh with Texture==blendMesh renders additive
      bool isBlendMesh = hasBlendMesh && (mb.bmdTextureId == mdl.blendMesh);
      if (isBlendMesh) {
        m_shader->setFloat("blendMeshLight", blendMeshLightVal);
        m_shader->setVec2("texCoordOffset", glm::vec2(0.0f, blendMeshUVOffset));
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_shader->setFloat("blendMeshLight", 1.0f);
        m_shader->setVec2("texCoordOffset", glm::vec2(0.0f));
      } else if (mb.noneBlend) {
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

    // Draw weapons (skeleton types: sword, shield, bow on Player.bmd bones)
    for (int wi = 0;
         wi < (int)mdl.weaponDefs.size() && wi < (int)mon.weaponMeshes.size();
         ++wi) {
      auto &wd = mdl.weaponDefs[wi];
      auto &wms = mon.weaponMeshes[wi];
      if (!wd.bmd || wms.meshBuffers.empty())
        continue;
      if (wd.attachBone >= (int)bones.size())
        continue;

      // Parent matrix: character bone * weapon local transform
      // Main 5.2: ParentMatrix = BoneTransform[LinkBone] * AngleMatrix(rot) +
      // offset
      const auto &parentBone = bones[wd.attachBone];
      BoneWorldMatrix weaponLocal =
          MuMath::BuildWeaponOffsetMatrix(wd.rot, wd.offset);

      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms((const float(*)[4])parentBone.data(),
                               (const float(*)[4])weaponLocal.data(),
                               (float(*)[4])parentMat.data());

      // Use cached weapon local bones (static bind-pose, computed once at load)
      const auto &wLocalBones = wd.cachedLocalBones;
      std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
      for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
        MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                 (const float(*)[4])wLocalBones[bi].data(),
                                 (float(*)[4])wFinalBones[bi].data());
      }

      // Re-skin and draw each weapon mesh
      for (int mi = 0;
           mi < (int)wms.meshBuffers.size() && mi < (int)wd.bmd->Meshes.size();
           ++mi) {
        RetransformMeshWithBones(wd.bmd->Meshes[mi], wFinalBones,
                                 wms.meshBuffers[mi]);
        auto &mb = wms.meshBuffers[mi];
        if (mb.indexCount == 0)
          continue;
        glBindTexture(GL_TEXTURE_2D, mb.texture);
        glBindVertexArray(mb.vao);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      }
    }
  }

  // Restore state
  glEnable(GL_CULL_FACE);
  m_shader->setFloat("objectAlpha", 1.0f);

  renderDebris(view, proj, camPos);
  renderArrows(view, proj, camPos);
}

void MonsterManager::RenderShadows(const glm::mat4 &view,
                                   const glm::mat4 &proj) {
  if (!m_shadowShader || m_monsters.empty())
    return;

  m_shadowShader->use();
  m_shadowShader->setMat4("projection", proj);
  m_shadowShader->setMat4("view", view);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.0f, -1.0f);
  glDisable(GL_CULL_FACE);
  glEnable(GL_STENCIL_TEST);

  const float sx = 2000.0f;
  const float sy = 4000.0f;

  for (auto &mon : m_monsters) {
    if (mon.cachedBones.empty())
      continue;
    // Skip faded corpses
    if (mon.state == MonsterState::DEAD && mon.corpseAlpha <= 0.01f)
      continue;

    auto &mdl = m_models[mon.modelIdx];

    // Shadow model matrix
    glm::mat4 model = glm::translate(glm::mat4(1.0f), mon.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::scale(model, glm::vec3(mon.scale));

    m_shadowShader->setMat4("model", model);

    // Clear stencil for this monster
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    float cosF = cosf(mon.facing);
    float sinF = sinf(mon.facing);

    for (int mi = 0;
         mi < (int)mdl.bmd->Meshes.size() && mi < (int)mon.shadowMeshes.size();
         ++mi) {
      auto &sm = mon.shadowMeshes[mi];
      if (sm.vertexCount == 0 || sm.vao == 0)
        continue;

      auto &mesh = mdl.bmd->Meshes[mi];
      static std::vector<glm::vec3> shadowVerts;
      shadowVerts.clear();

      for (int i = 0; i < mesh.NumTriangles; ++i) {
        auto &tri = mesh.Triangles[i];
        int steps = (tri.Polygon == 3) ? 3 : 4;
        for (int v = 0; v < 3; ++v) {
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 pos = srcVert.Position;
          int boneIdx = srcVert.Node;
          if (boneIdx >= 0 && boneIdx < (int)mon.cachedBones.size()) {
            pos = MuMath::TransformPoint(
                (const float(*)[4])mon.cachedBones[boneIdx].data(), pos);
          }
          pos *= mon.scale;
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
        if (steps == 4) {
          int quadIndices[3] = {0, 2, 3};
          for (int v : quadIndices) {
            auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
            glm::vec3 pos = srcVert.Position;
            int boneIdx = srcVert.Node;
            if (boneIdx >= 0 && boneIdx < (int)mon.cachedBones.size()) {
              pos = MuMath::TransformPoint(
                  (const float(*)[4])mon.cachedBones[boneIdx].data(), pos);
            }
            pos *= mon.scale;
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

    // Render weapon shadows (skeleton/lich types)
    for (int wi = 0;
         wi < (int)mdl.weaponDefs.size() &&
         wi < (int)mon.weaponShadowMeshes.size();
         ++wi) {
      auto &wd = mdl.weaponDefs[wi];
      auto &wss = mon.weaponShadowMeshes[wi];
      if (!wd.bmd || wss.meshes.empty())
        continue;
      if (wd.attachBone >= (int)mon.cachedBones.size())
        continue;

      // Compute weapon final bones (same as normal render)
      const auto &parentBone = mon.cachedBones[wd.attachBone];
      BoneWorldMatrix weaponLocal =
          MuMath::BuildWeaponOffsetMatrix(wd.rot, wd.offset);
      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms((const float(*)[4])parentBone.data(),
                               (const float(*)[4])weaponLocal.data(),
                               (float(*)[4])parentMat.data());
      const auto &wLocalBones = wd.cachedLocalBones;
      std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
      for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
        MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                 (const float(*)[4])wLocalBones[bi].data(),
                                 (float(*)[4])wFinalBones[bi].data());
      }

      for (int wmi = 0;
           wmi < (int)wd.bmd->Meshes.size() && wmi < (int)wss.meshes.size();
           ++wmi) {
        auto &wsm = wss.meshes[wmi];
        if (wsm.vertexCount == 0 || wsm.vao == 0)
          continue;

        auto &mesh = wd.bmd->Meshes[wmi];
        static std::vector<glm::vec3> shadowVerts;
        shadowVerts.clear();

        for (int i = 0; i < mesh.NumTriangles; ++i) {
          auto &tri = mesh.Triangles[i];
          int steps = (tri.Polygon == 3) ? 3 : 4;
          for (int v = 0; v < 3; ++v) {
            auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
            glm::vec3 pos = srcVert.Position;
            int boneIdx = srcVert.Node;
            if (boneIdx >= 0 && boneIdx < (int)wFinalBones.size()) {
              pos = MuMath::TransformPoint(
                  (const float(*)[4])wFinalBones[boneIdx].data(), pos);
            }
            pos *= mon.scale;
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
          if (steps == 4) {
            int quadIndices[3] = {0, 2, 3};
            for (int v : quadIndices) {
              auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
              glm::vec3 pos = srcVert.Position;
              int boneIdx = srcVert.Node;
              if (boneIdx >= 0 && boneIdx < (int)wFinalBones.size()) {
                pos = MuMath::TransformPoint(
                    (const float(*)[4])wFinalBones[boneIdx].data(), pos);
              }
              pos *= mon.scale;
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

        glBindBuffer(GL_ARRAY_BUFFER, wsm.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        shadowVerts.size() * sizeof(glm::vec3),
                        shadowVerts.data());
        glBindVertexArray(wsm.vao);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shadowVerts.size());
      }
    }
  }

  glBindVertexArray(0);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
}

void MonsterManager::RenderSilhouetteOutline(int monsterIndex,
                                              const glm::mat4 &view,
                                              const glm::mat4 &proj) {
  if (!m_outlineShader || monsterIndex < 0 ||
      monsterIndex >= (int)m_monsters.size())
    return;

  auto &mon = m_monsters[monsterIndex];
  if (mon.state == MonsterState::DEAD && mon.corpseAlpha <= 0.01f)
    return;

  auto &mdl = m_models[mon.modelIdx];

  // Build model matrix at normal scale (outline uses normal extrusion, not scale)
  glm::mat4 baseModel = glm::translate(glm::mat4(1.0f), mon.position);
  baseModel = glm::rotate(baseModel, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  baseModel = glm::rotate(baseModel, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  baseModel = glm::rotate(baseModel, mon.facing, glm::vec3(0, 0, 1));

  glm::mat4 stencilModel = glm::scale(baseModel, glm::vec3(mon.scale));

  m_outlineShader->use();
  m_outlineShader->setMat4("projection", proj);
  m_outlineShader->setMat4("view", view);

  glDisable(GL_CULL_FACE);

  // === Pass 1: Write COMPLETE silhouette to stencil ===
  // Depth test OFF so ALL mesh pixels get stenciled (no gaps between parts)
  glEnable(GL_STENCIL_TEST);
  glStencilFunc(GL_ALWAYS, 1, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
  glStencilMask(0xFF);
  glClear(GL_STENCIL_BUFFER_BIT);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glDepthMask(GL_FALSE);
  glDisable(GL_DEPTH_TEST);

  m_outlineShader->setMat4("model", stencilModel);
  m_outlineShader->setFloat("outlineThickness", 0.0f);

  for (auto &mb : mon.meshBuffers) {
    if (mb.indexCount == 0 || mb.hidden)
      continue;
    glBindVertexArray(mb.vao);
    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
  }
  for (int wi = 0;
       wi < (int)mdl.weaponDefs.size() && wi < (int)mon.weaponMeshes.size();
       ++wi) {
    for (auto &mb : mon.weaponMeshes[wi].meshBuffers) {
      if (mb.indexCount == 0)
        continue;
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }

  // === Pass 2: Multi-layer soft glow where stencil != 1 ===
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
  glStencilMask(0x00);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  m_outlineShader->setVec3("outlineColor", 0.8f, 0.4f, 0.15f);

  // Render multiple layers from outermost (faint) to innermost (bright)
  // for smooth soft glow falloff — normal extrusion for uniform width
  constexpr float thicknesses[] = {5.0f, 3.5f, 2.0f};
  constexpr float alphas[] = {0.08f, 0.18f, 0.35f};

  m_outlineShader->setMat4("model", stencilModel);

  for (int layer = 0; layer < 3; ++layer) {
    m_outlineShader->setFloat("outlineThickness", thicknesses[layer]);
    m_outlineShader->setFloat("outlineAlpha", alphas[layer]);

    for (auto &mb : mon.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
    for (int wi = 0;
         wi < (int)mdl.weaponDefs.size() && wi < (int)mon.weaponMeshes.size();
         ++wi) {
      for (auto &mb : mon.weaponMeshes[wi].meshBuffers) {
        if (mb.indexCount == 0)
          continue;
        glBindVertexArray(mb.vao);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      }
    }
  }

  // Restore state
  glStencilMask(0xFF);
  glStencilFunc(GL_ALWAYS, 0, 0xFF);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindVertexArray(0);
}

void MonsterManager::RenderNameplates(ImDrawList *dl, ImFont *font,
                                      const glm::mat4 &view,
                                      const glm::mat4 &proj, int winW, int winH,
                                      const glm::vec3 &camPos,
                                      int hoveredMonster,
                                      int attackTarget, int playerLevel) {
  // Show nameplate for hovered monster, or attack target if not hovering
  int targetIdx = hoveredMonster;
  if (targetIdx < 0 || targetIdx >= GetMonsterCount())
    targetIdx = attackTarget;
  if (targetIdx < 0 || targetIdx >= GetMonsterCount())
    return;

  MonsterInfo mi = GetMonsterInfo(targetIdx);
  if (mi.state == MonsterState::DEAD)
    return;

  // Project monster head position to screen
  glm::vec4 worldPos(mi.position.x, mi.position.y + mi.height + 20.0f,
                     mi.position.z, 1.0f);
  glm::vec4 clip = proj * view * worldPos;
  if (clip.w <= 0.0f)
    return;

  glm::vec3 ndc = glm::vec3(clip) / clip.w;
  float sx = (ndc.x * 0.5f + 0.5f) * winW;
  float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * winH;

  // WoW-style threat color based on XP gain potential
  // Gray threshold matches server CalculateXP formula
  int grayThreshold = 8 + playerLevel / 10;
  if (grayThreshold > 20) grayThreshold = 20;
  int diff = playerLevel - mi.level; // positive = player higher
  ImU32 threatCol;
  if (diff > grayThreshold)
    threatCol = IM_COL32(120, 150, 120, 200);  // Dark green-gray: very low XP
  else if (diff > 5)
    threatCol = IM_COL32(80, 210, 80, 200);    // Green: reduced XP
  else if (diff >= -2)
    threatCol = IM_COL32(255, 230, 60, 200);   // Yellow: even
  else if (diff >= -5)
    threatCol = IM_COL32(255, 140, 40, 200);   // Orange: dangerous
  else
    threatCol = IM_COL32(255, 60, 60, 200);    // Red: very dangerous

  // Name + Level text (no background frame — floating text with shadow)
  char nameText[64];
  snprintf(nameText, sizeof(nameText), "%s", mi.name.c_str());
  char levelText[16];
  snprintf(levelText, sizeof(levelText), "%d", mi.level);
  ImVec2 nameSize = font->CalcTextSizeA(12.0f, FLT_MAX, 0, nameText);
  ImVec2 lvlSize = font->CalcTextSizeA(10.0f, FLT_MAX, 0, levelText);

  // HP bar dimensions (thin, elegant)
  float barW = 60.0f;
  float barH = 5.0f;

  // Center everything on projected point
  float nameX = sx - nameSize.x * 0.5f;
  float nameY = sy - barH - nameSize.y - 6.0f;
  float barX = sx - barW * 0.5f;
  float barY = sy - barH - 3.0f;

  // Name shadow + text
  dl->AddText(font, 12.0f, ImVec2(nameX + 1, nameY + 1),
              IM_COL32(0, 0, 0, 160), nameText);
  dl->AddText(font, 12.0f, ImVec2(nameX, nameY), threatCol, nameText);

  // Level badge (small, right of name)
  float lvlX = nameX + nameSize.x + 4.0f;
  float lvlY = nameY + 2.0f;
  dl->AddText(font, 10.0f, ImVec2(lvlX + 1, lvlY + 1),
              IM_COL32(0, 0, 0, 140), levelText);
  dl->AddText(font, 10.0f, ImVec2(lvlX, lvlY),
              IM_COL32(200, 200, 200, 160), levelText);

  // HP bar — subtle translucent
  float hpFrac = mi.maxHp > 0 ? (float)mi.hp / mi.maxHp : 0.0f;
  hpFrac = std::max(0.0f, std::min(1.0f, hpFrac));

  // Bar background (very subtle dark)
  dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
                    IM_COL32(0, 0, 0, 100), 2.0f);

  // Bar fill (soft gradient feel via two-tone)
  if (hpFrac > 0.0f) {
    ImU32 hpCol = hpFrac > 0.5f    ? IM_COL32(50, 200, 50, 180)
                  : hpFrac > 0.25f ? IM_COL32(220, 190, 40, 180)
                                   : IM_COL32(210, 50, 50, 180);
    dl->AddRectFilled(ImVec2(barX, barY),
                      ImVec2(barX + barW * hpFrac, barY + barH), hpCol, 2.0f);
  }

  // Thin border on bar
  dl->AddRect(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
              IM_COL32(255, 255, 255, 40), 2.0f);
}
