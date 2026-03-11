#include "FireEffect.hpp"
#include "TextureLoader.hpp"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>

// --- Fire offset table (MU local coordinates from ZzzObject.cpp) ---

static const std::vector<glm::vec3> kNoOffsets;

static const std::vector<glm::vec3> kFireLight01Offsets = {
    glm::vec3(0.0f, 0.0f, 200.0f)};

static const std::vector<glm::vec3> kFireLight02Offsets = {
    glm::vec3(0.0f, -30.0f, 60.0f)};

static const std::vector<glm::vec3> kBonfireOffsets = {
    glm::vec3(0.0f, 0.0f, 60.0f)};

static const std::vector<glm::vec3> kDungeonGateOffsets = {
    glm::vec3(-150.0f, -150.0f, 140.0f), glm::vec3(150.0f, -150.0f, 140.0f)};

static const std::vector<glm::vec3> kBridgeOffsets = {
    glm::vec3(90.0f, -200.0f, 30.0f), glm::vec3(90.0f, 200.0f, 30.0f)};

static const std::vector<glm::vec3> kLight01Offsets = {
    glm::vec3(0.0f, 0.0f, 0.0f)};

// Dungeon torches (Main 5.2: ZzzObject.cpp case WD_1DUNGEON)
// Type 41: tall fire stand  — CreateFire(0, o, 0, -30, 240)
// Type 42: standard lantern — CreateFire(0, o, 0, 0, 190)
static const std::vector<glm::vec3> kDungeonTorch41Offsets = {
    glm::vec3(0.0f, -30.0f, 240.0f)};
static const std::vector<glm::vec3> kDungeonTorch42Offsets = {
    glm::vec3(0.0f, 0.0f, 190.0f)};

// Devias fireplaces (Main 5.2: ZzzObject.cpp WD_2DEVIAS)
// Type 30 (Stone01): fireplace — fire+smoke at z+160 (BITMAP_TRUE_FIRE particles)
// Type 66 (SteelWall02): wall fire — CreateFire(0, o, 0, 0, 50)
static const std::vector<glm::vec3> kDeviasFireplaceOffsets = {
    glm::vec3(0.0f, 0.0f, 160.0f)};
static const std::vector<glm::vec3> kDeviasWallFireOffsets = {
    glm::vec3(0.0f, 0.0f, 50.0f)};

const std::vector<glm::vec3> &GetFireOffsets(int objectType, int mapId) {
  switch (objectType) {
  case 30: // Devias fireplace only (Lorencia type 30 = Stone01, no fire)
    return (mapId == 2) ? kDeviasFireplaceOffsets : kNoOffsets;
  case 41: // Dungeon torches
    return (mapId == 1) ? kDungeonTorch41Offsets : kNoOffsets;
  case 42:
    return (mapId == 1) ? kDungeonTorch42Offsets : kNoOffsets;
  case 50: // FireLight01 — Lorencia only (Devias type 50 is a different model)
    return (mapId == 0) ? kFireLight01Offsets : kNoOffsets;
  case 51: // FireLight02 — Lorencia only
    return (mapId == 0) ? kFireLight02Offsets : kNoOffsets;
  case 52: // Bonfire01 — Lorencia only
    return (mapId == 0) ? kBonfireOffsets : kNoOffsets;
  case 55: // DoungeonGate01 — Lorencia only (dungeon gates use separate VFX)
    return (mapId == 0) ? kDungeonGateOffsets : kNoOffsets;
  case 66: // Devias wall fire only (Lorencia type 66 = different object)
    return (mapId == 2) ? kDeviasWallFireOffsets : kNoOffsets;
  case 80: // Bridge01 — Lorencia only
    return (mapId == 0) ? kBridgeOffsets : kNoOffsets;
  case 130: // Light01 — Lorencia only
    return (mapId == 0) ? kLight01Offsets : kNoOffsets;
  default:
    return kNoOffsets;
  }
}

// Smoke offset table (Main 5.2: ZzzObject.cpp — CreateFire type 1/2)
// Type 131 = Light02 (torch smoke), Type 132 = Light03 (smoke variant)
static const std::vector<glm::vec3> kSmokeTorchOffsets = {
    glm::vec3(0.0f, 0.0f, 0.0f)};


const std::vector<glm::vec3> &GetSmokeOffsets(int objectType, int mapId) {
  switch (objectType) {
  case 30: // Devias fireplace smoke (Main 5.2: BITMAP_SMOKE subtype 21 at z+160)
    return (mapId == 2) ? kDeviasFireplaceOffsets : kNoOffsets;
  case 131: // Light02 torch smoke — Lorencia only
  case 132: // Light03 smoke variant — Lorencia only
    return (mapId == 0) ? kSmokeTorchOffsets : kNoOffsets;
  default:
    return kNoOffsets;
  }
}

int GetFireTypeFromFilename(const std::string &bmdFilename) {
  if (bmdFilename == "FireLight01.bmd")
    return 50;
  if (bmdFilename == "FireLight02.bmd")
    return 51;
  if (bmdFilename == "Bonfire01.bmd")
    return 52;
  if (bmdFilename == "DoungeonGate01.bmd")
    return 55;
  if (bmdFilename == "Bridge01.bmd")
    return 80;
  if (bmdFilename == "Light01.bmd")
    return 130;
  if (bmdFilename == "Light02.bmd")
    return 131;
  if (bmdFilename == "Light03.bmd")
    return 132;
  return -1;
}

// --- Random helpers ---

static float RandFloat(float lo, float hi) {
  return lo + static_cast<float>(rand()) / RAND_MAX * (hi - lo);
}

// --- FireEffect implementation ---

void FireEffect::Init(const std::string &effectDataPath) {
  // Load fire sprite sheet texture
  std::string firePath = effectDataPath + "/Fire01.OZJ";
  fireTexture = TextureLoader::LoadOZJ(firePath);
  if (fireTexture == 0) {
    std::cerr << "[FireEffect] Failed to load fire texture: " << firePath
              << std::endl;
    return;
  }

  // Override wrap mode to clamp (prevent frame bleeding in sprite sheet)
  // Override mipmap filtering: fire particles are small (60-100 units) and
  // trigger lower mipmap levels which cause heavy blur. Use GL_LINEAR (no mipmaps)
  // to keep fire sharp at any distance.
  glBindTexture(GL_TEXTURE_2D, fireTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  std::cout << "[FireEffect] Loaded fire texture: " << firePath << std::endl;

  // Load smoke texture (Main 5.2: BITMAP_SMOKE = smoke01.jpg for fountain spray)
  std::string smokePath = effectDataPath + "/smoke01.OZJ";
  waterTexture = TextureLoader::LoadOZJ(smokePath);
  if (waterTexture == 0) {
    std::cerr << "[FireEffect] Failed to load smoke texture: " << smokePath
              << std::endl;
  } else {
    glBindTexture(GL_TEXTURE_2D, waterTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    std::cout << "[FireEffect] Loaded smoke texture: " << smokePath
              << std::endl;
  }

  // Compile billboard shader
  {
    std::ifstream test("shaders/billboard.vert");
    if (test.good())
      billboardShader = std::make_unique<Shader>("shaders/billboard.vert",
                                                 "shaders/billboard.frag");
    else
      billboardShader = std::make_unique<Shader>("../shaders/billboard.vert",
                                                 "../shaders/billboard.frag");
  }

  // Create static quad VAO (4 corners at ±0.5)
  float quadVerts[] = {
      -0.5f, -0.5f, // bottom-left
      0.5f,  -0.5f, // bottom-right
      0.5f,  0.5f,  // top-right
      -0.5f, 0.5f,  // top-left
  };
  unsigned int quadIndices[] = {0, 1, 2, 0, 2, 3};

  glGenVertexArrays(1, &quadVAO);
  glGenBuffers(1, &quadVBO);
  glGenBuffers(1, &quadEBO);
  glGenBuffers(1, &instanceVBO);

  glBindVertexArray(quadVAO);

  // Quad vertex data (location 0: aCorner)
  glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Index buffer
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices,
               GL_STATIC_DRAW);

  // Instance VBO (will be filled each frame)
  glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
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

  glBindVertexArray(0);
}

void FireEffect::ClearEmitters() {
  emitters.clear();
  particles.clear();
}

void FireEffect::AddEmitter(const glm::vec3 &worldPos) {
  Emitter e;
  e.position = worldPos;
  e.spawnAccum = RandFloat(0.0f, 1.0f); // stagger initial spawns
  e.smoke = false;
  emitters.push_back(e);
}

void FireEffect::AddSmokeEmitter(const glm::vec3 &worldPos) {
  Emitter e;
  e.position = worldPos;
  e.spawnAccum = RandFloat(0.0f, 1.0f);
  e.smoke = true;
  emitters.push_back(e);
}

void FireEffect::AddWaterSmokeEmitter(const glm::vec3 &worldPos) {
  Emitter e;
  e.position = worldPos;
  e.spawnAccum = RandFloat(0.0f, 1.0f);
  e.smoke = true;
  e.water = true;
  emitters.push_back(e);
}

void FireEffect::Update(float deltaTime) {
  if (fireTexture == 0)
    return;

  // Clamp deltaTime to avoid particle explosion after pause/lag
  if (deltaTime > 0.1f)
    deltaTime = 0.1f;

  // Spawn new particles from emitters
  for (auto &emitter : emitters) {
    // Main 5.2: smoke = rand()%2 (50% at 25fps = 12.5/sec), water = same rate
    float rate = emitter.water ? 12.5f : (emitter.smoke ? 6.0f : SPAWN_RATE);
    emitter.spawnAccum += rate * deltaTime;
    while (emitter.spawnAccum >= 1.0f &&
           (int)particles.size() < MAX_PARTICLES) {
      emitter.spawnAccum -= 1.0f;

      Particle p;
      p.position = emitter.position;
      p.position.x += RandFloat(-10.0f, 10.0f);
      p.position.y += RandFloat(-10.0f, 10.0f);
      p.position.z += RandFloat(-10.0f, 10.0f);

      if (emitter.water) {
        // Main 5.2: BITMAP_SMOKE from fountain bones, ±16 spread
        p.isWater = true;
        p.position.x += RandFloat(-16.0f, 16.0f);
        p.position.z += RandFloat(-16.0f, 16.0f);
        p.velocity = glm::vec3(RandFloat(-5.0f, 5.0f), RandFloat(25.0f, 50.0f),
                               RandFloat(-5.0f, 5.0f));
        p.scale = RandFloat(60.0f, 100.0f);
        p.maxLifetime = 1.2f;
        p.lifetime = 1.2f;
        float w = RandFloat(0.7f, 1.0f);
        p.color = glm::vec3(w, w, w);
      } else if (emitter.smoke) {
        // Smoke: slower upward, larger, longer life
        p.isWater = false;
        p.velocity = glm::vec3(RandFloat(-8.0f, 8.0f), RandFloat(20.0f, 45.0f),
                               RandFloat(-8.0f, 8.0f));
        p.scale = RandFloat(80.0f, 140.0f);
        p.maxLifetime = 1.8f;
        p.lifetime = 1.8f;
        float g = RandFloat(0.3f, 0.5f);
        p.color = glm::vec3(g, g, g);
      } else {
        p.isWater = false;
        // Fire: warm orange, faster upward
        p.velocity = glm::vec3(RandFloat(-5.0f, 5.0f), RandFloat(40.0f, 80.0f),
                               RandFloat(-5.0f, 5.0f));
        p.scale = RandFloat(60.0f, 100.0f);
        p.maxLifetime = PARTICLE_LIFETIME;
        p.lifetime = PARTICLE_LIFETIME;
        float lum = RandFloat(0.6f, 1.1f);
        p.color = glm::vec3(lum, lum * 0.6f, lum * 0.4f);
      }

      p.rotation = RandFloat(0.0f, 6.2832f);
      particles.push_back(p);
    }
  }

  // Update existing particles
  for (int i = (int)particles.size() - 1; i >= 0; --i) {
    auto &p = particles[i];
    p.lifetime -= deltaTime;

    if (p.lifetime <= 0.0f) {
      // Remove dead particle (swap with last)
      particles[i] = particles.back();
      particles.pop_back();
      continue;
    }

    p.position += p.velocity * deltaTime;
    // Slight deceleration of upward movement
    p.velocity.y *= (1.0f - 1.5f * deltaTime);
    // Shrink over lifetime
    p.scale -= 50.0f * deltaTime;
    if (p.scale < 2.0f)
      p.scale = 2.0f;
  }
}

void FireEffect::Render(const glm::mat4 &view, const glm::mat4 &projection) {
  if (fireTexture == 0 || particles.empty() || !billboardShader)
    return;

  // Build instance data
  int count = std::min((int)particles.size(), MAX_PARTICLES);
  std::vector<InstanceData> instanceData(count);

  for (int i = 0; i < count; ++i) {
    auto &p = particles[i];
    float t = 1.0f - p.lifetime / p.maxLifetime; // 0 → 1 over life
    int frame = std::min((int)(t * 4.0f), 3);
    float alpha = p.lifetime / p.maxLifetime; // fade out

    instanceData[i].worldPos = p.position;
    instanceData[i].scale = p.scale;
    instanceData[i].rotation = p.rotation;
    instanceData[i].frame = (float)frame;
    instanceData[i].color = p.color;
    instanceData[i].alpha = alpha;
  }

  // Upload instance data
  glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
  glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(InstanceData),
                  instanceData.data());

  // Set shader
  billboardShader->use();
  billboardShader->setMat4("view", view);
  billboardShader->setMat4("projection", projection);
  billboardShader->setInt("fireTexture", 0);

  glActiveTexture(GL_TEXTURE0);

  // Additive blending, no depth writes
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Pre-multiplied additive
  glDepthMask(GL_FALSE);

  glBindVertexArray(quadVAO);

  // Batch 1: Fire & Smoke (using fireTexture)
  {
    std::vector<InstanceData> fireBatch;
    for (int i = 0; i < count; ++i) {
      if (!particles[i].isWater) {
        fireBatch.push_back(instanceData[i]);
      }
    }

    if (!fireBatch.empty()) {
      glBindTexture(GL_TEXTURE_2D, fireTexture);
      glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
      glBufferSubData(GL_ARRAY_BUFFER, 0,
                      fireBatch.size() * sizeof(InstanceData),
                      fireBatch.data());
      glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0,
                              (int)fireBatch.size());
    }
  }

  // Batch 2: Water Mist (using waterTexture)
  if (waterTexture != 0) {
    std::vector<InstanceData> waterBatch;
    for (int i = 0; i < count; ++i) {
      if (particles[i].isWater) {
        // Override frame for water (not a sprite sheet)
        InstanceData d = instanceData[i];
        d.frame = 0.0f;
        waterBatch.push_back(d);
      }
    }

    if (!waterBatch.empty()) {
      glBindTexture(GL_TEXTURE_2D, waterTexture);
      glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
      glBufferSubData(GL_ARRAY_BUFFER, 0,
                      waterBatch.size() * sizeof(InstanceData),
                      waterBatch.data());
      glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0,
                              (int)waterBatch.size());
    }
  }

  glBindVertexArray(0);

  // Restore GL state
  glDepthMask(GL_TRUE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void FireEffect::Cleanup() {
  if (quadVAO) {
    glDeleteVertexArrays(1, &quadVAO);
    quadVAO = 0;
  }
  if (quadVBO) {
    glDeleteBuffers(1, &quadVBO);
    quadVBO = 0;
  }
  if (quadEBO) {
    glDeleteBuffers(1, &quadEBO);
    quadEBO = 0;
  }
  if (instanceVBO) {
    glDeleteBuffers(1, &instanceVBO);
    instanceVBO = 0;
  }
  if (fireTexture) {
    glDeleteTextures(1, &fireTexture);
    fireTexture = 0;
  }
  if (waterTexture) {
    glDeleteTextures(1, &waterTexture);
    waterTexture = 0;
  }
  billboardShader.reset();
  emitters.clear();
  particles.clear();
}
