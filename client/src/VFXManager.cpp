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

  // Main 5.2: BITMAP_SPARK+1 — Aqua Beam laser segments (Spark03.OZJ)
  m_spark3Texture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Spark03.OZJ");
  if (m_spark3Texture == 0)
    std::cerr << "[VFX] Failed to load Spark03.OZJ" << std::endl;

  // Aqua Beam outer glow layer
  m_flareBlueTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/flareBlue.OZJ");
  if (m_flareBlueTexture == 0)
    m_flareBlueTexture = m_spark3Texture; // Fallback

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

  // Main 5.2: BITMAP_FLAME — Flame spell ground fire particles
  m_flameTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Flame01.OZJ");
  if (m_flameTexture == 0) {
    std::cerr << "[VFX] Failed to load Flame01.OZJ — using Fire01 fallback"
              << std::endl;
    m_flameTexture = m_fireTexture;
  }

  // Main 5.2: BITMAP_EXPLOTION — animated 4x4 explosion sprite sheet
  m_explosionTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Explotion01.OZJ");
  if (m_explosionTexture == 0)
    m_explosionTexture = m_flareTexture; // Fallback

  // Main 5.2: Inferno-specific fire texture
  m_infernoFireTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/inferno.OZJ");
  if (m_infernoFireTexture == 0)
    m_infernoFireTexture = m_fireTexture; // Fallback

  // Main 5.2: BITMAP_JOINT_SPIRIT — Evil Spirit beam texture
  m_jointSpiritTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/JointSpirit01.OZJ");
  if (m_jointSpiritTexture == 0)
    std::cerr << "[VFX] Failed to load JointSpirit01.OZJ" << std::endl;

  // Main 5.2: Circle01.bmd texture (Hellfire ground circle — star/pentagram pattern)
  m_hellfireCircleTex =
      TextureLoader::LoadOZJ(effectDataPath + "/Skill/magic_a01.OZJ");
  if (m_hellfireCircleTex == 0)
    std::cerr << "[VFX] Failed to load Skill/magic_a01.OZJ" << std::endl;

  // Main 5.2: Circle02.bmd texture (Hellfire light circle overlay)
  m_hellfireLightTex =
      TextureLoader::LoadOZJ(effectDataPath + "/Skill/magic_a02.OZJ");
  if (m_hellfireLightTex == 0)
    std::cerr << "[VFX] Failed to load Skill/magic_a02.OZJ" << std::endl;

  // Main 5.2: BITMAP_BLUR — regular attack blur trail (blur01.OZJ)
  m_blurTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/blur01.OZJ");
  if (m_blurTexture == 0)
    std::cerr << "[VFX] Failed to load blur01.OZJ" << std::endl;

  // Main 5.2: BITMAP_BLUR+2 — skill attack blur trail (motion_blur_r.OZJ)
  m_motionBlurTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/motion_blur_r.OZJ");
  if (m_motionBlurTexture == 0)
    std::cerr << "[VFX] Failed to load motion_blur_r.OZJ" << std::endl;

  // Main 5.2: BITMAP_SPARK — hit spark particles (Spark02.OZJ)
  m_spark2Texture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/Spark02.OZJ");
  if (m_spark2Texture == 0)
    std::cerr << "[VFX] Failed to load Spark02.OZJ" << std::endl;

  // Main 5.2: BITMAP_FLARE_FORCE — Death Stab spiral trail (NSkill.OZJ)
  m_flareForceTexture =
      TextureLoader::LoadOZJ(effectDataPath + "/Effect/NSkill.OZJ");
  if (m_flareForceTexture == 0)
    m_flareForceTexture = m_energyTexture; // Fallback

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

  // Load Ice crystal model (Main 5.2: MODEL_ICE = Data/Skill/Ice01.bmd)
  m_iceBmd = BMDParser::Parse(skillPath + "Ice01.bmd");
  if (m_iceBmd) {
    auto bones = ComputeBoneMatrices(m_iceBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_iceBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_iceMeshes, dummyAABB, false);
    }
    std::cout << "[VFX] Ice01.bmd loaded: " << m_iceMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Ice shard model (Main 5.2: MODEL_ICE_SMALL = Data/Skill/Ice02.bmd)
  m_iceSmallBmd = BMDParser::Parse(skillPath + "Ice02.bmd");
  if (m_iceSmallBmd) {
    auto bones = ComputeBoneMatrices(m_iceSmallBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_iceSmallBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_iceSmallMeshes, dummyAABB,
                          false);
    }
    std::cout << "[VFX] Ice02.bmd loaded: " << m_iceSmallMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Storm tornado model (Main 5.2: MODEL_STORM = Data/Skill/Storm01.bmd)
  m_stormBmd = BMDParser::Parse(skillPath + "Storm01.bmd");
  if (m_stormBmd) {
    auto bones = ComputeBoneMatrices(m_stormBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_stormBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_stormMeshes, dummyAABB,
                          false);
    }
    std::cout << "[VFX] Storm01.bmd loaded: " << m_stormMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Hellfire ground circle models (Main 5.2: MODEL_CIRCLE = Circle01.bmd)
  m_circleBmd = BMDParser::Parse(skillPath + "Circle01.bmd");
  if (m_circleBmd) {
    auto bones = ComputeBoneMatrices(m_circleBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_circleBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_circleMeshes, dummyAABB,
                          false);
    }
    std::cout << "[VFX] Circle01.bmd loaded: " << m_circleMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Hellfire circle light model (Main 5.2: MODEL_CIRCLE_LIGHT = Circle02.bmd)
  m_circleLightBmd = BMDParser::Parse(skillPath + "Circle02.bmd");
  if (m_circleLightBmd) {
    auto bones = ComputeBoneMatrices(m_circleLightBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_circleLightBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_circleLightMeshes,
                          dummyAABB, false);
    }
    std::cout << "[VFX] Circle02.bmd loaded: " << m_circleLightMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Inferno ring model (Main 5.2: MODEL_SKILL_INFERNO = Data/Skill/Inferno01.bmd)
  m_infernoBmd = BMDParser::Parse(skillPath + "Inferno01.bmd");
  if (m_infernoBmd) {
    auto bones = ComputeBoneMatrices(m_infernoBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_infernoBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_infernoMeshes,
                          dummyAABB, false);
    }
    std::cout << "[VFX] Inferno01.bmd loaded: " << m_infernoMeshes.size()
              << " meshes" << std::endl;
  }

  // Load Evil Spirit beam head flash (Main 5.2: MODEL_LASER = Data/Skill/Laser01.bmd)
  m_laserBmd = BMDParser::Parse(skillPath + "Laser01.bmd");
  if (m_laserBmd) {
    auto bones = ComputeBoneMatrices(m_laserBmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_laserBmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_laserMeshes,
                          dummyAABB, false);
    }
    std::cout << "[VFX] Laser01.bmd loaded: " << m_laserMeshes.size()
              << " meshes" << std::endl;
  }

  // Rageful Blow: EarthQuake01-08.bmd (Main 5.2: MODEL_SKILL_FURY_STRIKE+1..+8)
  // Types used: 1,2,3 (center), 4,5 (radial arms), 7,8 (branching extensions)
  // Type 6 reuses type 4 meshes (same model)
  {
    const int eqTypes[] = {1, 2, 3, 4, 5, 7, 8};
    for (int t : eqTypes) {
      std::string filename = skillPath + "EarthQuake0" + std::to_string(t) + ".bmd";
      m_eqBmd[t] = BMDParser::Parse(filename);
      if (m_eqBmd[t]) {
        auto bones = ComputeBoneMatrices(m_eqBmd[t].get(), 0, 0);
        AABB dummyAABB{};
        for (auto &mesh : m_eqBmd[t]->Meshes) {
          UploadMeshWithBones(mesh, skillPath, bones, m_eqMeshes[t],
                              dummyAABB, false);
        }
        std::cout << "[VFX] EarthQuake0" << t << ".bmd loaded: "
                  << m_eqMeshes[t].size() << " meshes" << std::endl;
      } else {
        std::cerr << "[VFX] FAILED to parse EarthQuake0" << t << ".bmd" << std::endl;
      }
    }
  }

  // Stone debris (Main 5.2: MODEL_GROUND_STONE)
  m_stone1Bmd = BMDParser::Parse(skillPath + "GroundStone.bmd");
  if (m_stone1Bmd) {
    auto bones = ComputeBoneMatrices(m_stone1Bmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_stone1Bmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_stone1Meshes,
                          dummyAABB, false);
    }
    std::cout << "[VFX] GroundStone.bmd loaded: " << m_stone1Meshes.size()
              << " meshes" << std::endl;
  } else {
    std::cerr << "[VFX] FAILED to parse GroundStone.bmd" << std::endl;
  }

  // Stone debris variant (Main 5.2: MODEL_GROUND_STONE2)
  m_stone2Bmd = BMDParser::Parse(skillPath + "GroundStone2.bmd");
  if (m_stone2Bmd) {
    auto bones = ComputeBoneMatrices(m_stone2Bmd.get(), 0, 0);
    AABB dummyAABB{};
    for (auto &mesh : m_stone2Bmd->Meshes) {
      UploadMeshWithBones(mesh, skillPath, bones, m_stone2Meshes,
                          dummyAABB, false);
    }
    std::cout << "[VFX] GroundStone2.bmd loaded: " << m_stone2Meshes.size()
              << " meshes" << std::endl;
  } else {
    std::cerr << "[VFX] FAILED to parse GroundStone2.bmd" << std::endl;
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
      // Main 5.2: CreateSpark — BITMAP_SPARK (Spark02.OZJ) white sparks
      // Gravity + terrain bounce, arc trajectory (6-22 per tick² gravity)
      float speed = 100.0f + (float)(rand() % 150);
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 80.0f + (float)(rand() % 120),
                    std::sin(angle) * speed);
      p.scale = 12.0f + (float)(rand() % 8);
      p.maxLifetime = 0.4f + (float)(rand() % 20) / 100.0f;
      p.color = glm::vec3(1.0f, 1.0f, 1.0f); // White (Main 5.2: no tint)
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
    case ParticleType::SPELL_FLAME: {
      // Main 5.2: BITMAP_FLAME — 20 tick lifetime (0.8s), white light (texture handles color)
      // Velocity[2] = (rand()%128+128)*0.15 = 19.2-38.4 upward per tick
      // Scale = Scale*(rand()%64+64)*0.01 = 64-128% random
      float offX = (float)(rand() % 50) - 25.0f;
      float offZ = (float)(rand() % 50) - 25.0f;
      float upSpeed = (float)(rand() % 128 + 128) * 0.15f * 25.0f; // Convert per-tick to per-sec
      p.velocity = glm::vec3(offX * 0.3f, upSpeed, offZ * 0.3f);
      p.scale = 30.0f * (float)(rand() % 64 + 64) * 0.01f; // 19.2-38.4
      p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
      p.frame = -1.0f; // Full texture UV
      p.maxLifetime = 0.8f; // 20 ticks @ 25fps
      // Main 5.2: Light is white — let Flame01.OZJ texture provide orange color
      float lum = (float)(rand() % 4 + 8) * 0.1f; // 0.8-1.1 flicker
      p.color = glm::vec3(lum, lum, lum);
      p.alpha = 1.0f;
      p.position += glm::vec3(offX, 0.0f, offZ);
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
      // Vertical rising spark column — blue energy
      p.position.y += (float)(rand() % 80); // Vertical spread (column)
      float drift = 15.0f + (float)(rand() % 10);
      p.velocity = glm::vec3(
          std::cos(angle) * drift,
          80.0f + (float)(rand() % 40), // Strong upward rise
          std::sin(angle) * drift);
      p.scale = i == 0 ? 40.0f : (20.0f + (float)(rand() % 15));
      p.maxLifetime = 0.5f;
      p.color = glm::vec3(0.3f, 0.5f, 1.0f); // Blue energy
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
    case ParticleType::INFERNO_SPARK: {
      // Main 5.2: BITMAP_SPARK SubType 2 — 2x scale, 3x velocity, long-lived
      // Lifetime 24-39 ticks (0.96-1.56s), gravity 6-21, bounce on terrain
      float speed = 240.0f + (float)(rand() % 360); // 3x base velocity
      p.velocity =
          glm::vec3(std::cos(angle) * speed, 300.0f + (float)(rand() % 300),
                    std::sin(angle) * speed);
      p.scale = 25.0f + (float)(rand() % 20); // 2x HIT_SPARK scale (25-45)
      p.maxLifetime = 0.96f + (float)(rand() % 60) / 100.0f; // 0.96-1.56s
      p.color = glm::vec3(1.0f, 0.95f, 0.85f); // Warm white (blends into fire)
      p.alpha = 1.0f;
      break;
    }
    case ParticleType::INFERNO_EXPLOSION: {
      // Main 5.2: BITMAP_EXPLOTION — 4x4 animated explosion sprite sheet
      // Lifetime 20 ticks (0.8s), large scale, stationary
      p.velocity = glm::vec3(0.0f, 30.0f, 0.0f); // Slight upward drift
      p.scale = 120.0f + (float)(rand() % 60); // Large explosion flash (120-180)
      p.maxLifetime = 0.8f; // 20 ticks
      p.color = glm::vec3(1.0f, 0.9f, 0.7f); // Warm white-orange
      p.alpha = 1.0f;
      p.frame = 100.0f; // Start at cell 0 of 4x4 grid (100+ encoding in shader)
      break;
    }
    case ParticleType::INFERNO_FIRE: {
      // Dedicated Inferno fire — uses inferno.OZJ texture, rising column
      float offX = (float)(rand() % 60) - 30.0f;
      float offZ = (float)(rand() % 60) - 30.0f;
      float upSpeed = (float)(rand() % 100 + 150) * 1.0f; // 150-250 up
      p.velocity = glm::vec3(offX * 0.4f, upSpeed, offZ * 0.4f);
      p.scale = 40.0f + (float)(rand() % 30); // 40-70 (large fire)
      p.maxLifetime = 0.6f + (float)(rand() % 30) / 100.0f; // 0.6-0.9s
      p.color = glm::vec3(1.0f, 0.8f, 0.5f); // Warm fire tint
      p.alpha = 1.0f;
      p.frame = -1.0f; // Full texture
      p.position += glm::vec3(offX, 0.0f, offZ);
      break;
    }
    case ParticleType::DEATHSTAB_SPARK:
      // This type is spawned directly (not via SpawnBurst)
      break;
    case ParticleType::PET_SPARKLE: {
      // Main 5.2: BITMAP_SPARK SubType 1 — stationary, small, fading white dot
      // Direction=(0,0,0), Velocity=0.3, Light=(0.4,0.4,0.4)
      p.velocity = glm::vec3(0.0f);       // Stationary
      p.scale = 4.0f + (float)(rand() % 4); // Small (4-8 units)
      p.maxLifetime = 0.2f + (float)(rand() % 10) / 100.0f; // 0.2-0.3s
      p.color = glm::vec3(0.5f, 0.5f, 0.5f); // Neutral gray-white
      p.alpha = 0.8f;
      break;
    }
    case ParticleType::IMP_SPARKLE: {
      // Imp companion — dark red/orange embers, slight upward drift
      p.velocity = glm::vec3(
          (float)(rand() % 10 - 5) * 0.5f,
          (float)(rand() % 30 + 10) * 1.0f, // Gentle upward float (10-40)
          (float)(rand() % 10 - 5) * 0.5f);
      p.scale = 3.0f + (float)(rand() % 4); // Small (3-7 units)
      p.maxLifetime = 0.3f + (float)(rand() % 15) / 100.0f; // 0.3-0.45s
      p.color = glm::vec3(1.0f, 0.4f + (float)(rand() % 3) * 0.1f, 0.1f); // Red-orange
      p.alpha = 0.9f;
      break;
    }
    case ParticleType::MOUNT_DUST: {
      // Main 5.2: BITMAP_SMOKE hoofbeat dust — low, slow spread, brownish
      // Position offset ±32/±32/±16 applied by caller
      float speed = 8.0f + (float)(rand() % 15);
      p.velocity = glm::vec3(std::cos(angle) * speed,
                             10.0f + (float)(rand() % 15), // Gentle rise
                             std::sin(angle) * speed);
      p.scale = 12.0f + (float)(rand() % 20); // 12-32 units (Main 5.2: 0.48-0.80 * 40)
      p.maxLifetime = 0.5f + (float)(rand() % 30) / 100.0f; // 0.5-0.8s
      p.color = glm::vec3(0.7f, 0.65f, 0.55f); // Brownish dust
      p.alpha = 0.5f;
      break;
    }
    case ParticleType::DUNGEON_MIST:
      break; // Removed — not needed
    case ParticleType::SET_WATERFALL: {
      // Main 5.2: BITMAP_WATERFALL_2 SubType 3 — rising columnar energy stream
      // Rises upward, fades out, shrinks. Used for full armor set bonus effect.
      p.velocity = glm::vec3((float)(rand() % 6 - 3),  // slight X drift
                             30.0f + (float)(rand() % 20), // upward rise
                             (float)(rand() % 6 - 3)); // slight Z drift
      p.scale = 8.0f + (float)(rand() % 6);
      p.maxLifetime = 0.8f + (float)(rand() % 4) / 10.0f; // 0.8-1.2s
      // Color set by caller via SetParticleColor or defaults to set glow color
      p.color = glm::vec3(0.5f, 0.7f, 1.0f); // Default blue-white (overridden)
      p.alpha = 0.9f;
      break;
    }
    }

    p.lifetime = p.maxLifetime;
    m_particles.push_back(p);
  }
}

void VFXManager::SpawnBurstColored(ParticleType type, const glm::vec3 &position,
                                    const glm::vec3 &color, int count) {
  // Spawn particles then override their color
  size_t before = m_particles.size();
  SpawnBurst(type, position, count);
  for (size_t i = before; i < m_particles.size(); ++i) {
    m_particles[i].color = color;
  }
}

void VFXManager::SpawnSkillCast(uint8_t skillId, const glm::vec3 &heroPos,
                                float facing, const glm::vec3 &targetPos) {
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
  case 41: // Twisting Slash — ghost weapons handled by HeroCharacter
    SpawnBurst(ParticleType::FLARE, castPos, 3);
    break;
  case 42: // Rageful Blow
    SpawnRagefulBlow(heroPos, facing);
    break;
  case 43: // Death Stab — Main 5.2: MODEL_SPEARSKILL converging + MODEL_SPEAR trail
    SpawnDeathStab(heroPos, facing, targetPos);
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
  case 7: // Ice — Main 5.2: no billboard particles on cast, just flare
    SpawnBurst(ParticleType::FLARE, castPos, 1);
    break;
  case 5: // Flame — lighter caster-side glow, main fire at target
    SpawnBurst(ParticleType::SPELL_FLAME, castPos, 8);
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    break;
  case 8: // Twister — Main 5.2: cast-side sparkle only
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    break;
  case 6: // Teleport — Main 5.2: BITMAP_SPARK+1 white rising column at feet
    SpawnBurst(ParticleType::SPELL_TELEPORT, heroPos, 18);
    break;
  case 9: // Evil Spirit — beams spawned separately, just a small cast flash
    SpawnBurst(ParticleType::SPELL_DARK, castPos, 10);
    SpawnBurst(ParticleType::FLARE, castPos, 2);
    break;
  case 12: // Aqua Beam
    SpawnBurst(ParticleType::SPELL_WATER, castPos, 20);
    SpawnBurst(ParticleType::FLARE, castPos, 3);
    break;
  case 10: // Hellfire — cast flash (main VFX from SpawnHellfire)
    SpawnBurst(ParticleType::SPELL_FIRE, castPos, 15);
    SpawnBurst(ParticleType::FLARE, castPos, 3);
    break;
  case 13: // Cometfall — no cast-side particles (Main 5.2: none for Blast)
    break;
  case 14: // Inferno — no cast-side particles (Main 5.2: ring IS the effect)
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
  case 22:
  case 23: { // Basic DK sword skills — Main 5.2: CreateSpark (1x BITMAP_SPARK+1 + 20x BITMAP_SPARK)
    SpawnBurst(ParticleType::SKILL_SLASH, hitPos, 20);  // 20x Spark02.OZJ small sparks
    SpawnBurst(ParticleType::FLARE, hitPos, 1);          // 1x bright flash (Spark03 stand-in)
    break;
  }
  case 41: // Twisting Slash
    SpawnBurst(ParticleType::SKILL_CYCLONE, hitPos, 20);
    SpawnBurst(ParticleType::FLARE, hitPos, 2);
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 15);
    break;
  case 42: // Rageful Blow — ground burst + hit sparks
    SpawnBurst(ParticleType::SKILL_FURY, hitPos, 8);
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 6);
    break;
  case 43: // Death Stab — Main 5.2: m_byHurtByOneToOne = 35 (target electrocution)
    SpawnDeathStabShock(monsterPos);
    SpawnBurst(ParticleType::FLARE, hitPos, 3); // Impact flash
    break;
  // DW Spell impacts
  case 17: // Energy Ball — Main 5.2: BITMAP_MAGIC impact + radial bursts
    SpawnBurst(ParticleType::SPELL_ENERGY, hitPos, 25);
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 10);
    SpawnBurst(ParticleType::FLARE, hitPos, 3);
    break;
  case 4: // Fire Ball — Main 5.2: impact handled by projectile collision
    SpawnBurst(ParticleType::FLARE, hitPos, 2);
    break;
  case 1: // Poison — cloud already spawned at cast time, skip duplicate
    break;
  case 3: // Lightning — electric shock on monster (no sky bolt)
    SpawnBurst(ParticleType::DEATHSTAB_SPARK, hitPos, 15);    // Cyan electric arcs on body
    SpawnBurst(ParticleType::SPELL_LIGHTNING, hitPos, 20);     // Electric sparks
    SpawnBurst(ParticleType::FLARE, hitPos, 2);                // Brief flash
    break;
  case 2: // Meteorite — falling fireball (impact particles handled by bolt system)
    SpawnMeteorStrike(hitPos);
    break;
  case 7: // Ice — Main 5.2: MODEL_ICE crystal + 5x MODEL_ICE_SMALL debris
    SpawnIceStrike(hitPos);
    SpawnBurst(ParticleType::FLARE, hitPos, 1);
    break;
  case 5: // Flame — persistent ground fire at target
    SpawnFlameGround(monsterPos);
    break;
  case 8: // Twister — Main 5.2: tornado spawned at caster, just hit sparks here
    SpawnBurst(ParticleType::HIT_SPARK, hitPos, 8);
    SpawnBurst(ParticleType::SPELL_DARK, hitPos, 5);
    break;
  case 9: // Evil Spirit — hit sparks + dark energy
    SpawnBurst(ParticleType::SPELL_DARK, hitPos, 10);
    SpawnBurst(ParticleType::FLARE, hitPos, 2);
    break;
  case 6: // Teleport — Main 5.2: white rising sparks at destination (feet level)
    SpawnBurst(ParticleType::SPELL_TELEPORT, monsterPos, 18);
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
  // Same chest-height offset for both start and target → level flight on flat terrain
  static constexpr float CHEST_HEIGHT = 80.0f;
  proj.position = start + glm::vec3(0, CHEST_HEIGHT, 0);
  proj.target = target + glm::vec3(0, CHEST_HEIGHT, 0);

  // Purely horizontal direction — Main 5.2: Direction {0,-50,0} rotated by yaw only
  glm::vec3 delta = proj.target - proj.position;
  delta.y = 0.0f; // Horizontal flight
  float dist = glm::length(delta);
  if (dist < 1.0f)
    return;

  proj.direction = delta / dist;
  // Main 5.2: Direction=(0,-50,0) = 50 units/frame × 25fps = 1250 units/sec
  proj.speed = 1250.0f;
  proj.rotation = 0.0f;
  proj.rotSpeed = 0.0f;
  proj.yaw = std::atan2(delta.x, delta.z);
  proj.pitch = 0.0f;
  // Main 5.2: Fire Ball LifeTime=60 ticks (2.4s), distance-based for variable range
  proj.maxLifetime = std::min(2.4f, dist / proj.speed + 0.1f);
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
    // Main 5.2: Fire Ball faces target, no visual spin
    proj.rotSpeed = 0.0f;
    proj.rotation = 0.0f;
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

    // Main 5.2: CheckTargetRange — impact when within 100 units XZ of target
    glm::vec3 toTarget = p.target - p.position;
    float distXZ = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    if (p.lifetime <= 0.0f || distXZ <= 100.0f) {
      glm::vec3 impactPos = p.position; // Explode at fireball's actual position

      if (p.skillId == 4) {
        // Fire Ball — Main 5.2: 2× MODEL_STONE debris on impact
        // Substitute with sparks + fire burst (no stone model)
        SpawnBurst(ParticleType::HIT_SPARK, impactPos, 8);
        SpawnBurst(ParticleType::SPELL_FIRE, impactPos, 6);
        SpawnBurst(ParticleType::FLARE, impactPos, 2);
      } else {
        // Energy Ball — rich energy orb explosion
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
        // Bright impact flash
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
        // Secondary smaller flash
        {
          Particle flash2;
          flash2.type = ParticleType::FLARE;
          flash2.position = impactPos;
          flash2.velocity = glm::vec3(0);
          flash2.scale = 100.0f;
          flash2.rotation = 0.785f;
          flash2.frame = -1.0f;
          flash2.lifetime = 0.2f;
          flash2.maxLifetime = 0.2f;
          flash2.color = glm::vec3(0.6f, 0.8f, 1.0f);
          flash2.alpha = 1.0f;
          m_particles.push_back(flash2);
        }
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
        // Main 5.2: 1× BITMAP_FIRE SubType 5 per tick at fireball position
        // LifeTime=24 ticks (0.96s), Scale=Scale*(rand%64+128)*0.01
        // Velocity=(0,-(rand%16+32)*0.1,0), Frame=(23-LifeTime)/6 (4-frame sprite)
        {
          Particle trail;
          trail.type = ParticleType::SPELL_FIRE;
          trail.position = p.position;
          trail.velocity = glm::vec3(0.0f, 0.0f, 0.0f);
          // Main 5.2: Scale = parentScale * (rand%64+128)*0.01 = 1.28-1.92x
          float scaleMul = (float)(rand() % 64 + 128) * 0.01f;
          trail.scale = 30.0f * scaleMul;
          trail.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
          trail.frame = 0.0f; // Main 5.2: 4-frame sprite animation
          trail.lifetime = 0.96f;  // Main 5.2: 24 ticks
          trail.maxLifetime = 0.96f;
          // Main 5.2: Light = (L*1.0, L*0.1, 0.0) — red tint
          float lum = 0.8f + (float)(rand() % 3) * 0.1f;
          trail.color = glm::vec3(lum, lum * 0.1f, 0.0f);
          trail.alpha = 1.0f;
          m_particles.push_back(trail);
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
    case ParticleType::PET_SPARKLE:
      // Stationary, just gentle scale shrink as it fades
      p.scale *= (1.0f - 1.5f * deltaTime);
      break;
    case ParticleType::IMP_SPARKLE:
      // Embers drift upward, decelerate, shrink
      p.velocity *= (1.0f - 2.0f * deltaTime);
      p.scale *= (1.0f - 1.8f * deltaTime);
      break;
    case ParticleType::MOUNT_DUST:
      // Hoofbeat dust: slow deceleration, gentle shrink
      p.velocity *= (1.0f - 1.5f * deltaTime);
      p.scale *= (1.0f - 0.8f * deltaTime);
      break;
    case ParticleType::DUNGEON_MIST:
      break; // Removed
    case ParticleType::SET_WATERFALL:
      // Main 5.2: BITMAP_WATERFALL_2 — rises upward, shrinks, fades
      // Continuous upward drift, gentle deceleration, color fades exponentially
      p.velocity.x *= (1.0f - 2.0f * deltaTime);
      p.velocity.z *= (1.0f - 2.0f * deltaTime);
      p.scale *= (1.0f - 1.5f * deltaTime); // Shrink over time
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
    case ParticleType::SKILL_SLASH: {
      // Main 5.2: CreateSpark — gravity pull (avg ~350/s²), terrain bounce
      p.velocity.y -= 350.0f * deltaTime;
      p.scale *= (1.0f - 1.2f * deltaTime);
      // Terrain bounce: if particle falls below ground, reflect Y velocity (damped)
      if (m_getTerrainHeight) {
        float groundY = m_getTerrainHeight(p.position.x, p.position.z);
        if (p.position.y < groundY) {
          p.position.y = groundY;
          p.velocity.y = std::abs(p.velocity.y) * 0.4f; // Bounce with 60% energy loss
          p.velocity.x *= 0.7f;
          p.velocity.z *= 0.7f;
        }
      }
      break;
    }
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
    case ParticleType::DEATHSTAB_SPARK:
      // Main 5.2: BITMAP_JOINT_THUNDER bone sparks — cyan, gravity, fast fade
      p.velocity.y -= 300.0f * deltaTime;
      p.velocity *= (1.0f - 3.0f * deltaTime);
      p.scale *= (1.0f - 4.0f * deltaTime);
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
      // Main 5.2 BITMAP_FIRE SubType 5:
      // Gravity += 0.004/tick → Position[Z] += Gravity*10 (accelerating rise)
      // Scale -= 0.04/tick, Rotation += 5°/tick, Alpha = LifeTime/maxLifeTime
      // Frame = (maxTicks - ticksLeft) / 6 (4-frame sprite animation)
      p.velocity.y += 25.0f * deltaTime; // ~0.004*10*25fps² ≈ 25 u/s²
      p.velocity.x *= (1.0f - 1.0f * deltaTime);
      p.velocity.z *= (1.0f - 1.0f * deltaTime);
      p.scale -= 25.0f * deltaTime; // Main 5.2: -0.04/tick × 25fps = -1.0/s
      if (p.scale < 1.0f) p.scale = 1.0f;
      p.rotation += 2.18f * deltaTime; // 5°/tick × 25fps = 125°/s ≈ 2.18 rad/s
      p.alpha = p.lifetime / p.maxLifetime; // Linear fade
      // 4-frame sprite animation: frame advances as lifetime decreases
      if (p.maxLifetime > 0.0f) {
        float progress = 1.0f - (p.lifetime / p.maxLifetime); // 0→1
        p.frame = std::min(3.0f, std::floor(progress * 4.0f));
      }
      break;
    case ParticleType::SPELL_FLAME:
      // Main 5.2 BITMAP_FLAME: rises with velocity, light fades over lifetime
      // Rotation randomized per frame (Main 5.2: o->Rotation = rand()%360)
      p.rotation = (float)(rand() % 360) * 3.14159f / 180.0f;
      // Light fades: subtract 0.05 per tick → ~1.25/sec
      p.color -= glm::vec3(1.25f * deltaTime);
      p.color = glm::max(p.color, glm::vec3(0.0f));
      p.alpha = p.lifetime / p.maxLifetime;
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
      // Rising blue sparks — drift slows
      p.velocity.x *= (1.0f - 2.0f * deltaTime);
      p.velocity.z *= (1.0f - 2.0f * deltaTime);
      break;
    case ParticleType::SPELL_ENERGY_ORB:
      // Main 5.2: BITMAP_ENERGY — Gravity=20 is rotation speed
      // No velocity decay: core orb particles must track projectile at full speed
      p.rotation += 500.0f * deltaTime;
      break;
    case ParticleType::INFERNO_SPARK: {
      // Main 5.2: BITMAP_SPARK SubType 2 — arcing sparks with terrain bounce
      p.velocity.y -= 500.0f * deltaTime;
      p.velocity.x *= (1.0f - 0.5f * deltaTime);
      p.velocity.z *= (1.0f - 0.5f * deltaTime);
      p.scale *= (1.0f - 0.6f * deltaTime);
      // Smooth color gradient: white-hot → orange → red as spark cools
      float sparkLife = p.lifetime / p.maxLifetime; // 1→0
      p.color = glm::vec3(1.0f,
                           0.4f + 0.6f * sparkLife,   // G: 1.0→0.4
                           0.1f + 0.9f * sparkLife * sparkLife); // B: 1.0→0.1 (fast)
      // Smooth quadratic fade-out (gentle disappearance)
      p.alpha = sparkLife * sparkLife;
      // Bounce on terrain
      if (m_getTerrainHeight) {
        float groundY = m_getTerrainHeight(p.position.x, p.position.z);
        if (p.position.y < groundY + 2.0f && p.velocity.y < 0.0f) {
          p.velocity.y *= -0.4f;
          p.position.y = groundY + 2.0f;
        }
      }
      break;
    }
    case ParticleType::INFERNO_EXPLOSION: {
      // 4x4 animated explosion sprite sheet (100+ encoding)
      // Quick flash-in (first 10%), smooth ease-out fade
      float expLife = p.lifetime / p.maxLifetime; // 1→0
      if (p.maxLifetime > 0.0f) {
        float progress = 1.0f - expLife;
        p.frame = 100.0f + std::min(15.0f, std::floor(progress * 16.0f));
      }
      p.scale *= (1.0f - 0.3f * deltaTime);
      p.velocity.y += 15.0f * deltaTime;
      // Smooth alpha: flash in over first 10%, then ease-out
      if (expLife > 0.9f)
        p.alpha = (1.0f - expLife) * 10.0f; // 0→1 in first 10%
      else
        p.alpha = expLife * expLife * 1.23f; // Quadratic ease-out (1.23 to hit 1.0 at 90%)
      // Color shifts from white-yellow to deep orange as explosion cools
      p.color = glm::vec3(1.0f,
                           0.6f + 0.4f * expLife,  // G: 1.0→0.6
                           0.2f + 0.5f * expLife);  // B: 0.7→0.2
      break;
    }
    case ParticleType::INFERNO_FIRE: {
      // inferno.OZJ — dedicated fire texture, accelerating rise
      float fireLife = p.lifetime / p.maxLifetime; // 1→0
      p.velocity.y += 60.0f * deltaTime;
      p.velocity.x *= (1.0f - 1.5f * deltaTime);
      p.velocity.z *= (1.0f - 1.5f * deltaTime);
      p.scale -= 30.0f * deltaTime;
      if (p.scale < 2.0f) p.scale = 2.0f;
      p.rotation += 3.0f * deltaTime;
      // Smooth ease-out alpha with gentle tail
      p.alpha = fireLife * (2.0f - fireLife); // Parabolic: peaks at 1.0, smooth fade
      // Color: bright yellow-white → deep orange-red
      p.color = glm::vec3(1.0f,
                           0.5f + 0.4f * fireLife,  // G: 0.9→0.5
                           0.15f + 0.45f * fireLife * fireLife); // B: 0.6→0.15 (fast)
      break;
    }
    }

    p.alpha = p.lifetime / p.maxLifetime;
  }

  // Update spell projectiles
  updateSpellProjectiles(deltaTime);

  // Update lightning sky-strike bolts
  updateLightningBolts(deltaTime);
  // Update meteor fireballs
  updateMeteorBolts(deltaTime);
  // Update ice crystals and shards
  updateIceCrystals(deltaTime);
  updateIceShards(deltaTime);

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

  // Update flame ground effects
  updateFlameGrounds(deltaTime);

  // Update twister storm tornados
  updateTwisterStorms(deltaTime);

  // Update Evil Spirit beams
  updateSpiritBeams(deltaTime);
  updateLaserFlashes(deltaTime);
  updateHellfireBeams(deltaTime);
  updateHellfireEffects(deltaTime);
  updateInfernoEffects(deltaTime);
  updateAquaBeams(deltaTime);
  updateWeaponTrail(deltaTime);
  updateFuryStrikeEffects(deltaTime);
  updateEarthQuakeCracks(deltaTime);
  updateStoneDebris(deltaTime);
  updateDeathStabEffects(deltaTime);
  updateDeathStabShocks(deltaTime);
  updateDeathStabSpirals(deltaTime);

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
  m_lineShader->setBool("beamMode", false);

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
            m_spark2Texture ? m_spark2Texture : m_sparkTexture);
  drawBatch(ParticleType::SKILL_CYCLONE,
            m_energyTexture ? m_energyTexture : m_sparkTexture);
  drawBatch(ParticleType::SKILL_FURY,
            m_flareTexture ? m_flareTexture : m_hitTexture);
  drawBatch(ParticleType::SKILL_STAB,
            m_sparkTexture ? m_sparkTexture : m_hitTexture);
  drawBatch(ParticleType::DEATHSTAB_SPARK,
            m_lightningTexture ? m_lightningTexture : m_sparkTexture);

  // DW Spell effect particles (additive)
  drawBatch(ParticleType::SPELL_ENERGY,
            m_energyTexture ? m_energyTexture : m_flareTexture);
  drawBatch(ParticleType::SPELL_FIRE,
            m_fireTexture ? m_fireTexture : m_flareTexture);
  drawBatch(ParticleType::SPELL_FLAME,
            m_flameTexture ? m_flameTexture : m_fireTexture);
  drawBatch(ParticleType::SPELL_ICE,
            m_sparkTexture ? m_sparkTexture : m_flareTexture);
  drawBatch(ParticleType::SPELL_LIGHTNING,
            m_lightningTexture ? m_lightningTexture : m_energyTexture);
  drawBatch(ParticleType::SPELL_METEOR,
            m_flareTexture ? m_flareTexture : m_hitTexture);
  drawBatch(ParticleType::SPELL_DARK,
            m_energyTexture ? m_energyTexture : m_flareTexture);
  drawBatch(ParticleType::SPELL_WATER,
            m_energyTexture ? m_energyTexture : m_flareTexture);
  drawBatch(ParticleType::SPELL_TELEPORT,
            m_energyTexture ? m_energyTexture : m_flareTexture);

  // Main 5.2: BITMAP_ENERGY orb (Thunder01.jpg) — full-texture rotating glow
  drawBatch(ParticleType::SPELL_ENERGY_ORB,
            m_thunderTexture ? m_thunderTexture : m_energyTexture);

  // Pet companion sparkle (Main 5.2: BITMAP_SPARK SubType 1 — white dot, additive)
  drawBatch(ParticleType::PET_SPARKLE,
            m_sparkTexture ? m_sparkTexture : m_flareTexture);
  // Imp companion ember sparkle (red-orange fire motes)
  drawBatch(ParticleType::IMP_SPARKLE,
            m_sparkTexture ? m_sparkTexture : m_flareTexture);

  // Mount hoofbeat dust (BITMAP_SMOKE texture)
  drawBatch(ParticleType::MOUNT_DUST,
            m_smokeTexture ? m_smokeTexture : m_flareTexture);

  // Inferno-specific particles (additive)
  drawBatch(ParticleType::INFERNO_SPARK,
            m_sparkTexture ? m_sparkTexture : m_hitTexture);
  drawBatch(ParticleType::INFERNO_EXPLOSION,
            m_explosionTexture ? m_explosionTexture : m_flareTexture);
  drawBatch(ParticleType::INFERNO_FIRE,
            m_infernoFireTexture ? m_infernoFireTexture : m_fireTexture);

  // Main 5.2: BITMAP_WATERFALL_2 — full armor set bonus rising energy
  drawBatch(ParticleType::SET_WATERFALL,
            m_energyTexture ? m_energyTexture : m_flareTexture);

  // Spell projectile 3D models (Fire Ball: Fire01.bmd) + billboard fallback
  // Trail particles already rendered via drawBatch above.
  glDepthMask(GL_TRUE); // Re-enable depth for 3D model rendering
  renderSpellProjectiles(view, projection);
  renderLightningBolts(view, projection);
  renderMeteorBolts(view, projection);
  renderIceCrystals(view, projection);
  renderIceShards(view, projection);
  renderPoisonClouds(view, projection);
  renderTwisterStorms(view, projection);
  renderStoneDebris(view, projection);
  glDepthMask(GL_FALSE); // Back to billboard mode for remaining effects

  // Render level-up orbiting flares (Main 5.2: 15 BITMAP_FLARE joints)
  renderLevelUpEffects(view, projection);

  // Render ground circles (Main 5.2: BITMAP_MAGIC level-up decal)
  renderGroundCircles(view, projection);

  // Main 5.2: BITMAP_FLAME terrain decal — flat fire sprite on ground
  renderFlameGrounds(view, projection);

  // Render Evil Spirit beams (subtractive blend — before ribbons)
  renderSpiritBeams(view, projection);
  renderLaserFlashes(view, projection);

  // Render Hellfire beams (additive blend — warm fire ribbons)
  renderHellfireBeams(view, projection);

  // Render Hellfire ground circle decals
  renderHellfireEffects(view, projection);

  // Render Inferno ring model
  renderInfernoEffects(view, projection);

  // Render Rageful Blow EarthQuake ground cracks + stone debris
  renderEarthQuakeCracks(view, projection);

  // Render Aqua Beam laser segments (additive billboard sprites)
  renderAquaBeams(view, projection);

  // Render textured ribbons (Lich Joint Thunder)
  renderRibbons(view, projection);

  // Render weapon blur trail (Main 5.2: ZzzEffectBlurSpark)
  renderWeaponTrail(view, projection);

  // Death Stab target electrocution arcs (additive lightning ribbons)
  renderDeathStabShocks(view, projection);

  // Death Stab Phase 1: spiraling energy from above converging on weapon
  renderDeathStabSpirals(view, projection);

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

  // Model matrix: translate → BMD base rotation → heading → pitch to horizontal → scale
  // Fire01.bmd is elongated along local Z (-62 to +103). After BMD base rotation,
  // local Z maps to world Y (vertical). Pitch 90° to lay it along the flight path.
  glm::mat4 model = glm::translate(glm::mat4(1.0f), p.position);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, p.yaw, glm::vec3(0, 0, 1));      // Heading
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0)); // Tilt: flame tail trails behind flight path
  model = glm::scale(model, glm::vec3(p.scale));

  m_modelShader->setMat4("model", model);
  m_modelShader->setVec3("terrainLight", glm::vec3(1.0f)); // Self-lit
  m_modelShader->setFloat("objectAlpha", p.alpha);

  // Main 5.2: BlendMesh=1, SubType 1 (Fire Ball): BlendMeshLight=0 (no glow)
  // Only SubType 0 (meteorite) has the 0.4-0.7 flicker
  glEnable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  for (const auto &mb : m_fireMeshes) {
    if (mb.indexCount == 0 || mb.hidden)
      continue;
    bool isGlow = (mb.bmdTextureId == 1);
    if (isGlow) {
      // Main 5.2 SubType 1: BlendMeshLight = 0 — skip glow mesh entirely
      continue;
    }
    m_modelShader->setFloat("blendMeshLight", 1.0f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, mb.texture);
    glBindVertexArray(mb.vao);
    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
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
      lightRange = 200.0f; // Main 5.2: AddTerrainLight range 2 grid cells
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
    // Main 5.2: Luminosity dims in last 5 ticks (0.2s) before impact
    if (!b.impacted && b.lifetime < 0.2f) {
      float ticksLeft = b.lifetime / 0.04f;
      L -= (5.0f - ticksLeft) * 0.2f;
      if (L < 0.0f) L = 0.0f;
    }
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

  // Twister storm terrain darkening — Main 5.2: Luminosity = (rand()%4+7)*0.1f
  // Fades when LifeTime < 5: Luminosity -= (5-LifeTime)*0.2f
  // AddTerrainLight(-L*0.4, -L*0.3, -L*0.2), range=5
  for (const auto &ts : m_twisterStorms) {
    float ticksRemaining = ts.lifetime / 0.04f;
    float L = (float)(rand() % 4 + 7) * 0.1f; // Main 5.2: 0.7-1.0 randomized
    if (ticksRemaining < 5.0f) {
      L -= (5.0f - ticksRemaining) * 0.2f;
      if (L < 0.0f) L = 0.0f;
    }
    if (L <= 0.01f)
      continue;
    positions.push_back(ts.position);
    colors.push_back(glm::vec3(-L * 0.4f, -L * 0.3f, -L * 0.2f));
    ranges.push_back(500.0f); // range=5 grid cells
    objectTypes.push_back(-1);
  }

  // Flame ground fire lights — Main 5.2: AddTerrainLight(pos, (1.0, 0.4, 0.0), 3)
  for (const auto &fg : m_flameGrounds) {
    float ticksRemaining = fg.lifetime / 0.04f;
    float L = std::min(1.0f, ticksRemaining * 0.05f);
    L *= (0.7f + (float)(rand() % 4) * 0.1f); // Luminosity flicker
    if (L <= 0.01f)
      continue;
    positions.push_back(fg.position);
    colors.push_back(glm::vec3(L * 1.0f, L * 0.4f, L * 0.0f));
    ranges.push_back(300.0f);
    objectTypes.push_back(-1);
  }

  // Hellfire ground circle — warm orange terrain light
  // Main 5.2: AddTerrainLight(pos, (Lum, Lum*0.8, Lum*0.2), 4)
  for (const auto &hf : m_hellfireEffects) {
    float ticksRemaining = hf.lifetime / 0.04f;
    float L = std::min(1.0f, ticksRemaining * 0.1f);
    L *= (0.7f + (float)(rand() % 4) * 0.1f);
    if (L <= 0.01f)
      continue;
    positions.push_back(hf.position);
    colors.push_back(glm::vec3(L, L * 0.8f, L * 0.2f));
    ranges.push_back(300.0f);
    objectTypes.push_back(-1);
  }

  // Inferno ring — smooth warm fire glow at ring points + darkening at center
  for (const auto &inf : m_infernoEffects) {
    float t = inf.lifetime / inf.maxLifetime; // 1→0
    // Smooth quadratic falloff — no random flicker for clean blending
    float L = t * t;
    if (L <= 0.005f)
      continue;
    // Center: Main 5.2 negative light (darkening), smooth fade
    positions.push_back(inf.position);
    colors.push_back(glm::vec3(-L * 0.4f, -L * 0.4f, -L * 0.4f));
    ranges.push_back(500.0f);
    objectTypes.push_back(-1);
    // Ring points: warm orange glow, smooth fade with color shift
    // Fire color cools from bright orange to deep red as effect fades
    for (int rp = 0; rp < 8; ++rp) {
      positions.push_back(inf.ringPoints[rp]);
      colors.push_back(glm::vec3(L * 0.8f,
                                  L * (0.25f + 0.2f * t),  // G fades faster
                                  L * 0.08f));              // Minimal blue
      ranges.push_back(200.0f);
      objectTypes.push_back(-1);
    }
  }

  // Evil Spirit beams — terrain darkening (negative light)
  // Main 5.2: Luminosity = -(rand()%4+4)*0.01, AddTerrainLight(pos, (L,L,L), range=4)
  for (const auto &b : m_spiritBeams) {
    float L = -(float)(rand() % 4 + 4) * 0.01f; // -0.04 to -0.08
    positions.push_back(b.position);
    colors.push_back(glm::vec3(L));
    ranges.push_back(400.0f); // range=4 grid cells
    objectTypes.push_back(-1);
  }

  // EarthQuake cracks — red terrain light
  // Only EQ02, EQ05, EQ08 emit light
  for (const auto &eq : m_earthQuakeCracks) {
    if (!eq.addTerrainLight || eq.blendMeshLight <= 0.01f)
      continue;
    float L = eq.blendMeshLight;
    positions.push_back(eq.position);
    colors.push_back(glm::vec3(L * 1.2f, L * 0.15f, L * 0.0f)); // Warm red-orange
    ranges.push_back(200.0f);
    objectTypes.push_back(-1);
  }

  // Aqua Beam — blue terrain light at all 20 segments (Main 5.2: AddTerrainLight, no fade)
  for (const auto &ab : m_aquaBeams) {
    for (int j = 0; j < AquaBeam::NUM_SEGMENTS; ++j) {
      glm::vec3 pos = ab.startPosition + ab.direction * (float)j;
      positions.push_back(pos);
      colors.push_back(ab.light); // Full brightness, no fade
      ranges.push_back(200.0f);   // range=2 grid cells
      objectTypes.push_back(-1);
    }
  }

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
                       m_bitmapFlareTexture, m_thunderTexture,
                       m_flameTexture,       m_jointSpiritTexture,
                       m_hellfireCircleTex,  m_hellfireLightTex};
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

  // Clean up Storm tornado model
  for (auto &mb : m_stormMeshes) {
    if (mb.vao) glDeleteVertexArrays(1, &mb.vao);
    if (mb.vbo) glDeleteBuffers(1, &mb.vbo);
    if (mb.ebo) glDeleteBuffers(1, &mb.ebo);
    if (mb.texture) glDeleteTextures(1, &mb.texture);
  }
  m_stormMeshes.clear();
  m_stormBmd.reset();

  // Clean up Hellfire circle models
  for (auto &mb : m_circleMeshes) {
    if (mb.vao) glDeleteVertexArrays(1, &mb.vao);
    if (mb.vbo) glDeleteBuffers(1, &mb.vbo);
    if (mb.ebo) glDeleteBuffers(1, &mb.ebo);
    if (mb.texture) glDeleteTextures(1, &mb.texture);
  }
  m_circleMeshes.clear();
  m_circleBmd.reset();
  for (auto &mb : m_circleLightMeshes) {
    if (mb.vao) glDeleteVertexArrays(1, &mb.vao);
    if (mb.vbo) glDeleteBuffers(1, &mb.vbo);
    if (mb.ebo) glDeleteBuffers(1, &mb.ebo);
    if (mb.texture) glDeleteTextures(1, &mb.texture);
  }
  m_circleLightMeshes.clear();
  m_circleLightBmd.reset();
  for (auto &mb : m_laserMeshes) {
    if (mb.vao) glDeleteVertexArrays(1, &mb.vao);
    if (mb.vbo) glDeleteBuffers(1, &mb.vbo);
    if (mb.ebo) glDeleteBuffers(1, &mb.ebo);
    if (mb.texture) glDeleteTextures(1, &mb.texture);
  }
  m_laserMeshes.clear();
  m_laserBmd.reset();

  m_particles.clear();
  m_ribbons.clear();
  m_groundCircles.clear();
  m_levelUpEffects.clear();
  m_spellProjectiles.clear();
  m_lightningBolts.clear();
  m_poisonClouds.clear();
  m_flameGrounds.clear();
  m_twisterStorms.clear();
  m_aquaBeams.clear();
  m_infernoEffects.clear();
}

