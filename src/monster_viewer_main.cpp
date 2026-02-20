#include "BMDParser.hpp"
#include "Camera.hpp"
#include "HeroCharacter.hpp"
#include "MonsterManager.hpp"
#include "Shader.hpp"
#include "VFXManager.hpp"
#include "ViewerCommon.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <filesystem>
#include <iostream>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// ── Globals ──
GLFWwindow *window = nullptr;
Camera camera;
bool dragging = false;
double lastMouseX = 0, lastMouseY = 0;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

MonsterManager g_monsterManager;
HeroCharacter g_hero;
VFXManager g_vfxManager;

// ── Lorencia Monsters ──
struct MonsterDef {
  int type;
  const char *name;
};

static const MonsterDef kMonsters[] = {
    {0, "Bull Fighter"},      {1, "Hound"},
    {2, "Budge Dragon"},      {3, "Spider"},
    {4, "Elite Bull Fighter"}, {6, "Lich"},
    {7, "Giant"},             {14, "Skeleton Warrior"},
    {15, "Skeleton Archer"},  {16, "Skeleton Captain"},
};

// Ranged monsters should be placed further from hero for spell visibility
static bool isRangedMonster(uint16_t type) {
  return type == 6 || type == 15; // Lich, Skeleton Archer
}
static constexpr int kNumMonsters = sizeof(kMonsters) / sizeof(kMonsters[0]);

// ── Weapon Preset Table ──
struct WeaponPreset {
  const char *displayName;
  const char *groupName;
  WeaponEquipInfo info;
};

static const WeaponPreset kWeapons[] = {
    // Unarmed
    {"Fist (Unarmed)", "Unarmed", {0xFF, 0, 0, false, ""}},

    // 1H Swords (cat=0, twoHanded=false)
    {"Kris", "Swords (1H)", {0, 0, 0, false, "Sword01.bmd"}},
    {"Rapier", "Swords (1H)", {0, 2, 0, false, "Sword03.bmd"}},
    {"Katana", "Swords (1H)", {0, 3, 0, false, "Sword04.bmd"}},
    {"Blade", "Swords (1H)", {0, 5, 0, false, "Sword06.bmd"}},

    // 2H Swords (cat=0, twoHanded=true)
    {"Double Blade", "Swords (2H)", {0, 3, 0, true, "Sword04.bmd"}},
    {"Lighting Sword", "Swords (2H)", {0, 6, 0, true, "Sword07.bmd"}},
    {"Giant Sword", "Swords (2H)", {0, 8, 0, true, "Sword09.bmd"}},

    // 1H Axes (cat=1, twoHanded=false)
    {"Small Axe", "Axes (1H)", {1, 0, 0, false, "Axe01.bmd"}},
    {"Tomahawk", "Axes (1H)", {1, 3, 0, false, "Axe04.bmd"}},

    // 2H Axes (cat=1, twoHanded=true)
    {"Crescent Axe", "Axes (2H)", {1, 8, 0, true, "Axe09.bmd"}},

    // 1H Maces (cat=2, twoHanded=false)
    {"Mace", "Maces (1H)", {2, 0, 0, false, "Mace01.bmd"}},
    {"Morning Star", "Maces (1H)", {2, 1, 0, false, "Mace02.bmd"}},

    // 2H Maces (cat=2, twoHanded=true)
    {"Great Hammer", "Maces (2H)", {2, 3, 0, true, "Mace04.bmd"}},

    // Spears (cat=3, idx<7)
    {"Spear", "Spears", {3, 1, 0, true, "Spear02.bmd"}},
    {"Dragon Lance", "Spears", {3, 2, 0, true, "Spear03.bmd"}},
    {"Serpent Spear", "Spears", {3, 4, 0, true, "Spear05.bmd"}},

    // Scythes (cat=3, idx>=7)
    {"Berdysh", "Scythes", {3, 7, 0, true, "Spear08.bmd"}},
    {"Great Scythe", "Scythes", {3, 8, 0, true, "Spear09.bmd"}},
    {"Bill of Balrog", "Scythes", {3, 9, 0, true, "Spear10.bmd"}},

    // Bows (cat=4, idx<8)
    {"Short Bow", "Bows", {4, 0, 0, true, "Bow01.bmd"}},
    {"Elven Bow", "Bows", {4, 2, 0, true, "Bow03.bmd"}},
    {"Battle Bow", "Bows", {4, 3, 0, true, "Bow04.bmd"}},
    {"Chaos Nature Bow", "Bows", {4, 6, 0, true, "Bow07.bmd"}},

    // Crossbows (cat=4, idx>=8)
    {"Crossbow", "Crossbows", {4, 8, 0, false, "CrossBow01.bmd"}},
    {"Light Crossbow", "Crossbows", {4, 11, 0, false, "CrossBow04.bmd"}},
    {"Aquagold Crossbow", "Crossbows", {4, 14, 0, false, "CrossBow07.bmd"}},

    // Staves (cat=5)
    {"Skull Staff", "Staves", {5, 0, 0, false, "Staff01.bmd"}},
    {"Thunder Staff", "Staves", {5, 3, 0, false, "Staff04.bmd"}},
};
static constexpr int kNumWeapons = sizeof(kWeapons) / sizeof(kWeapons[0]);

int currentMonsterIdx = 0;
int currentWeaponIdx = 6; // Default: Double Blade (2H Sword) to match old behavior
int savedMeleeWeaponIdx = 6; // Remembers last melee weapon when auto-switching to bow
bool animPlaying = true;

// Default ranged weapon index (Short Bow, cat=4 idx=0)
static int findDefaultBowIdx() {
  for (int i = 0; i < kNumWeapons; ++i) {
    if (kWeapons[i].info.category == 4 && kWeapons[i].info.itemIndex == 0)
      return i;
  }
  return 0;
}
static const int kDefaultBowIdx = findDefaultBowIdx();

// AI Simulation State
enum class AIState { SPAWN, WANDER, ATTACK, DIE, DEAD_WAIT };
AIState aiState = AIState::SPAWN;
float aiTimer = 0.0f;
int hitCount = 0;
bool monsterCounterPending = false;
float monsterCounterTimer = 0.0f;

// ── Ground Plane ──
static GLuint g_groundVAO = 0, g_groundVBO = 0;
static GLuint g_groundShader = 0;
static int g_groundVertCount = 0;

static const char *kGroundVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
out vec3 vWorldPos;
void main() {
  gl_Position = uMVP * vec4(aPos, 1.0);
  vColor = aColor;
  vWorldPos = aPos;
}
)";

static const char *kGroundFragSrc = R"(
#version 330 core
in vec3 vColor;
in vec3 vWorldPos;
out vec4 FragColor;
uniform float uLuminosity;
void main() {
  // Subtle grid lines every 100 world units
  vec2 grid = abs(fract(vWorldPos.xz / 100.0) - 0.5);
  float line = 1.0 - smoothstep(0.47, 0.50, min(grid.x, grid.y));
  vec3 color = mix(vColor, vColor * 0.7, line * 0.3);
  FragColor = vec4(color * uLuminosity, 1.0);
}
)";

static void InitGroundPlane(const glm::vec3 &center) {
  // Compile shader
  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &kGroundVertSrc, nullptr);
  glCompileShader(vs);

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &kGroundFragSrc, nullptr);
  glCompileShader(fs);

  g_groundShader = glCreateProgram();
  glAttachShader(g_groundShader, vs);
  glAttachShader(g_groundShader, fs);
  glLinkProgram(g_groundShader);
  glDeleteShader(vs);
  glDeleteShader(fs);

  // Generate a 20x20 grid of quads (2000x2000 world units centered on worldCenter)
  constexpr int GRID = 20;
  constexpr float CELL = 100.0f;
  float halfSize = GRID * CELL * 0.5f;
  float ox = center.x - halfSize;
  float oz = center.z - halfSize;

  // Base grass colors (green-brown)
  glm::vec3 colA(0.28f, 0.42f, 0.20f); // Darker grass
  glm::vec3 colB(0.32f, 0.47f, 0.22f); // Lighter grass

  struct GroundVert {
    glm::vec3 pos;
    glm::vec3 color;
  };
  std::vector<GroundVert> verts;
  for (int z = 0; z < GRID; ++z) {
    for (int x = 0; x < GRID; ++x) {
      float x0 = ox + x * CELL;
      float z0 = oz + z * CELL;
      float x1 = x0 + CELL;
      float z1 = z0 + CELL;
      float y = -0.5f; // Slightly below origin to avoid z-fighting with shadows

      // Checkerboard pattern
      glm::vec3 col = ((x + z) % 2 == 0) ? colA : colB;

      // Two triangles per quad
      verts.push_back({{x0, y, z0}, col});
      verts.push_back({{x1, y, z0}, col});
      verts.push_back({{x1, y, z1}, col});

      verts.push_back({{x0, y, z0}, col});
      verts.push_back({{x1, y, z1}, col});
      verts.push_back({{x0, y, z1}, col});
    }
  }

  g_groundVertCount = (int)verts.size();

  glGenVertexArrays(1, &g_groundVAO);
  glGenBuffers(1, &g_groundVBO);
  glBindVertexArray(g_groundVAO);
  glBindBuffer(GL_ARRAY_BUFFER, g_groundVBO);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(GroundVert), verts.data(),
               GL_STATIC_DRAW);
  // pos
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GroundVert), (void *)0);
  glEnableVertexAttribArray(0);
  // color
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GroundVert),
                        (void *)(sizeof(float) * 3));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
}

static void RenderGroundPlane(const glm::mat4 &mvp, float luminosity) {
  if (!g_groundShader || g_groundVertCount == 0)
    return;
  glUseProgram(g_groundShader);
  glUniformMatrix4fv(glGetUniformLocation(g_groundShader, "uMVP"), 1, GL_FALSE,
                     &mvp[0][0]);
  glUniform1f(glGetUniformLocation(g_groundShader, "uLuminosity"), luminosity);
  glBindVertexArray(g_groundVAO);
  glDrawArrays(GL_TRIANGLES, 0, g_groundVertCount);
  glBindVertexArray(0);
}

static void CleanupGroundPlane() {
  if (g_groundShader) {
    glDeleteProgram(g_groundShader);
    g_groundShader = 0;
  }
  if (g_groundVAO) {
    glDeleteVertexArrays(1, &g_groundVAO);
    glDeleteBuffers(1, &g_groundVBO);
    g_groundVAO = 0;
    g_groundVBO = 0;
  }
}

// ── Input Callbacks ──
static void ScrollCallback(GLFWwindow *win, double xoffset, double yoffset) {
  if (ImGui::GetIO().WantCaptureMouse)
    return;
  camera.ProcessMouseScroll(yoffset * 10.0f);
}

static void MouseButtonCallback(GLFWwindow *win, int button, int action,
                                int mods) {
  if (ImGui::GetIO().WantCaptureMouse)
    return;
  if (button == GLFW_MOUSE_BUTTON_RIGHT) {
    if (action == GLFW_PRESS) {
      dragging = true;
      glfwGetCursorPos(win, &lastMouseX, &lastMouseY);
    } else if (action == GLFW_RELEASE) {
      dragging = false;
    }
  }
}

static void CursorPosCallback(GLFWwindow *win, double xpos, double ypos) {
  if (ImGui::GetIO().WantCaptureMouse)
    return;
  if (dragging) {
    float dx = (float)(xpos - lastMouseX);
    float dy = (float)(lastMouseY - ypos);
    camera.ProcessMouseRotation(dx * 0.3f, dy * 0.3f);
    lastMouseX = xpos;
    lastMouseY = ypos;
  }
}

static void KeyCallback(GLFWwindow *win, int key, int scancode, int action,
                        int mods) {
  if (ImGui::GetIO().WantCaptureKeyboard)
    return;
  if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    animPlaying = !animPlaying;
  else if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose(win, true);

  if (action == GLFW_PRESS || action == GLFW_REPEAT) {
    if (key == GLFW_KEY_W)
      camera.ProcessKeyboard(0, deltaTime * 50.0f);
    if (key == GLFW_KEY_S)
      camera.ProcessKeyboard(1, deltaTime * 50.0f);
    if (key == GLFW_KEY_A)
      camera.ProcessKeyboard(2, deltaTime * 50.0f);
    if (key == GLFW_KEY_D)
      camera.ProcessKeyboard(3, deltaTime * 50.0f);
  }
}

static void CharCallback(GLFWwindow *win, unsigned int codepoint) {
  ImGui_ImplGlfw_CharCallback(win, codepoint);
}

// ── Window & ImGui Init ──
bool InitWindow() {
  if (!glfwInit())
    return false;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  window = glfwCreateWindow(1280, 720, "MU Combat Simulator", nullptr, nullptr);
  if (!window)
    return false;

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  if (glewInit() != GLEW_OK)
    return false;

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);

  glfwSetScrollCallback(window, ScrollCallback);
  glfwSetMouseButtonCallback(window, MouseButtonCallback);
  glfwSetCursorPosCallback(window, CursorPosCallback);
  glfwSetKeyCallback(window, KeyCallback);
  glfwSetCharCallback(window, CharCallback);
  return true;
}

void InitImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");
}

// ── Helper: AI state name ──
static const char *AIStateName(AIState s) {
  switch (s) {
  case AIState::SPAWN:
    return "SPAWN";
  case AIState::WANDER:
    return "WANDER";
  case AIState::ATTACK:
    return "ATTACK";
  case AIState::DIE:
    return "DIE";
  case AIState::DEAD_WAIT:
    return "DEAD_WAIT";
  }
  return "?";
}

static const char *AttackStateName(AttackState s) {
  switch (s) {
  case AttackState::NONE:
    return "NONE";
  case AttackState::APPROACHING:
    return "APPROACHING";
  case AttackState::SWINGING:
    return "SWINGING";
  case AttackState::COOLDOWN:
    return "COOLDOWN";
  }
  return "?";
}

static const char *WeaponTypeName(const WeaponEquipInfo &w) {
  if (w.category == 0xFF)
    return "Fist (single)";
  switch (w.category) {
  case 0:
    return w.twoHanded ? "2H Sword (3 variants)" : "1H Sword (2 variants)";
  case 1:
    return w.twoHanded ? "2H Axe (2 variants)" : "1H Axe (2 variants)";
  case 2:
    return w.twoHanded ? "2H Mace (2 variants)" : "1H Mace (2 variants)";
  case 3:
    return (w.itemIndex >= 7) ? "Scythe (3 variants)" : "Spear (single)";
  case 4:
    return (w.itemIndex >= 8) ? "Crossbow (single)" : "Bow (single)";
  case 5:
    return "Staff (single)";
  }
  return "Unknown";
}

// ── Main Simulation Loop ──
void Run() {
  if (!InitWindow())
    return;
  ActivateMacOSApp();
  InitImGui();

  // Data path detection
  std::string dataPath = "Data/";
  if (!std::filesystem::exists(dataPath)) {
    dataPath = "../Data/";
  }

  // World center (avoids edge fog darkening in model.frag)
  glm::vec3 worldCenter(12800.0f, 0.0f, 12800.0f);

  // Isometric camera matching game defaults
  camera.SetAngles(-45.0f, -48.5f);
  camera.SetZoom(800.0f);
  camera.SetPosition(worldCenter + glm::vec3(0.0f, 100.0f, 0.0f));

  // Init Hero
  g_hero.Init(dataPath);
  g_hero.LoadStats(1, 28, 20, 25, 10, 0, 0, 110, 110, 20, 20, 1);
  g_hero.SetPosition(worldCenter + glm::vec3(100.0f, 0.0f, 0.0f));
  g_hero.SetInSafeZone(false);
  g_hero.SetLuminosity(0.65f);
  g_hero.EquipWeapon(kWeapons[currentWeaponIdx].info);

  // Init Monster Manager
  g_monsterManager.InitModels(dataPath);
  g_monsterManager.SetLuminosity(0.65f);

  // Init VFX Manager
  g_vfxManager.Init(dataPath);
  g_monsterManager.SetVFXManager(&g_vfxManager);

  // Init Ground Plane
  InitGroundPlane(worldCenter);

  auto RespawnMonster = [&]() {
    g_monsterManager.ClearMonsters();

    // Monster at worldCenter. Bow range = 500 units, melee = 150 units.
    uint16_t monType = kMonsters[currentMonsterIdx].type;
    bool ranged = isRangedMonster(monType);
    g_monsterManager.AddMonster(monType,
                                worldCenter.x / 100.0f, worldCenter.z / 100.0f,
                                0);
    g_monsterManager.SetMonsterHP(0, 100, 100);

    // Auto-equip bow for ranged encounters, restore melee otherwise
    if (ranged) {
      if (kWeapons[currentWeaponIdx].info.category != 4) {
        savedMeleeWeaponIdx = currentWeaponIdx;
        currentWeaponIdx = kDefaultBowIdx;
      }
    } else {
      if (kWeapons[currentWeaponIdx].info.category == 4) {
        currentWeaponIdx = savedMeleeWeaponIdx;
      }
    }
    g_hero.EquipWeapon(kWeapons[currentWeaponIdx].info);

    // Ranged: 400 units apart (within BOW_ATTACK_RANGE=500), melee: 100 units
    float heroOffset = ranged ? 400.0f : 100.0f;
    g_hero.SetPosition(worldCenter + glm::vec3(heroOffset, 0.0f, 0.0f));
    g_hero.CancelAttack();

    aiState = AIState::SPAWN;
    aiTimer = 0.0f;
    hitCount = 0;
    monsterCounterPending = false;
    monsterCounterTimer = 0.0f;
  };
  RespawnMonster();

  while (!glfwWindowShouldClose(window)) {
    float now = (float)glfwGetTime();
    deltaTime = now - lastFrame;
    lastFrame = now;
    if (deltaTime > 0.1f)
      deltaTime = 0.1f;

    glfwPollEvents();

    if (animPlaying) {
      g_hero.UpdateState(deltaTime);
      g_hero.UpdateAttack(deltaTime);

      g_monsterManager.SetPlayerPosition(g_hero.GetPosition());
      g_monsterManager.SetPlayerDead(false);
      g_monsterManager.Update(deltaTime);
      g_vfxManager.Update(deltaTime);

      if (g_monsterManager.GetMonsterCount() > 0) {
        aiTimer += deltaTime;

        switch (aiState) {
        case AIState::SPAWN:
          if (aiTimer > 1.0f) {
            aiState = AIState::WANDER;
            aiTimer = 0.0f;
            g_monsterManager.SetMonsterServerPosition(
                0, worldCenter.x / 100.0f - 1.0f, worldCenter.z / 100.0f,
                true);
          }
          break;

        case AIState::WANDER:
          if (aiTimer > 3.0f) {
            aiState = AIState::ATTACK;
            aiTimer = 0.0f;
            hitCount = 0;
          }
          break;

        case AIState::ATTACK: {
          MonsterInfo mi = g_monsterManager.GetMonsterInfo(0);

          // Start a new attack swing when hero is idle
          if (g_hero.GetAttackState() == AttackState::NONE) {
            g_hero.AttackMonster(0, mi.position);
          }

          // Check for hit registration (at 40% of attack animation)
          if (g_hero.CheckAttackHit()) {
            int currentHP = mi.hp;
            int newHP = std::max(0, currentHP - 10);

            glm::vec3 hitPos = mi.position + glm::vec3(0, 50, 0);

            // Bow: spawn arrow projectile from hero to monster
            if (kWeapons[currentWeaponIdx].info.category == 4) {
              glm::vec3 heroPos = g_hero.GetPosition();
              g_monsterManager.SpawnArrow(
                  heroPos + glm::vec3(0, 80, 0), hitPos, 1200.0f);
            }

            // Main 5.2: Regular hits create blood (10x BITMAP_BLOOD+1)
            // Giant (type 7) excluded per Main 5.2: if(to->Type != MODEL_MONSTER01+7)
            if (mi.type != 7) {
              g_vfxManager.SpawnBurst(ParticleType::BLOOD, hitPos, 10);
            }

            if (newHP > 0) {
              g_monsterManager.SetMonsterHP(0, newHP, 100);
              g_monsterManager.TriggerHitAnimation(0);

              // Schedule monster counter-attack (don't reset if already pending)
              if (!monsterCounterPending) {
                monsterCounterPending = true;
                monsterCounterTimer = 0.8f;
              }
            } else {
              g_monsterManager.SetMonsterHP(0, 0, 100);
              aiState = AIState::DIE;
              monsterCounterPending = false;
            }
            hitCount++;
          }

          // Monster counter-attack after delay
          if (monsterCounterPending) {
            monsterCounterTimer -= deltaTime;
            if (monsterCounterTimer <= 0.0f) {
              g_monsterManager.TriggerAttackAnimation(0);
              monsterCounterPending = false;
            }
          }
          break;
        }

        case AIState::DIE:
          aiState = AIState::DEAD_WAIT;
          g_monsterManager.SetMonsterServerPosition(
              0, worldCenter.x / 100.0f - 1.0f, worldCenter.z / 100.0f, false);
          g_monsterManager.SetMonsterDying(0);
          g_hero.CancelAttack();
          aiTimer = 0.0f;
          break;

        case AIState::DEAD_WAIT:
          if (aiTimer > 3.0f) {
            RespawnMonster();
          }
          break;
        }
      }
    }

    // ── Rendering ──
    glClearColor(0.08f, 0.10f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int fbW, fbH, winW, winH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glfwGetWindowSize(window, &winW, &winH);
    float dpiScale = (winW > 0) ? (float)fbW / (float)winW : 1.0f;
    int panelPx = (int)(300.0f * dpiScale);
    int sceneW = fbW - panelPx;
    if (sceneW < 1)
      sceneW = 1;
    glViewport(panelPx, 0, sceneW, fbH);

    glm::mat4 projection = camera.GetProjectionMatrix(sceneW, fbH);
    glm::mat4 view = camera.GetViewMatrix();
    glm::vec3 camPos = camera.GetPosition();

    // Ground plane (render first, before characters)
    RenderGroundPlane(projection * view, 0.65f);

    // Characters and VFX
    g_hero.Render(view, projection, camPos, deltaTime);
    g_monsterManager.Render(view, projection, camPos, deltaTime);
    g_vfxManager.Render(view, projection);

    // ── ImGui Sidebar ──
    glViewport(0, 0, fbW, fbH);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(300, (float)winH));
    ImGui::Begin("Combat Simulator", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse);

    // ── Monster Selection ──
    if (ImGui::CollapsingHeader("Monsters", ImGuiTreeNodeFlags_DefaultOpen)) {
      for (int i = 0; i < kNumMonsters; ++i) {
        bool selected = (currentMonsterIdx == i);
        char label[64];
        snprintf(label, sizeof(label), "%s (type %d)", kMonsters[i].name,
                 kMonsters[i].type);
        if (ImGui::Selectable(label, selected)) {
          currentMonsterIdx = i;
          RespawnMonster();
        }
      }
    }

    // ── Weapon Selection ──
    if (ImGui::CollapsingHeader("Hero Weapon", ImGuiTreeNodeFlags_DefaultOpen)) {
      const char *lastGroup = nullptr;
      bool treeOpen = false;

      for (int i = 0; i < kNumWeapons; ++i) {
        // Start a new tree node when the group changes
        if (lastGroup == nullptr ||
            strcmp(lastGroup, kWeapons[i].groupName) != 0) {
          if (treeOpen) {
            ImGui::TreePop();
          }
          treeOpen = ImGui::TreeNode(kWeapons[i].groupName);
          lastGroup = kWeapons[i].groupName;
        }

        if (treeOpen) {
          bool selected = (currentWeaponIdx == i);
          if (ImGui::Selectable(kWeapons[i].displayName, selected)) {
            currentWeaponIdx = i;
            // Track last melee weapon for auto-switch
            if (kWeapons[i].info.category != 4)
              savedMeleeWeaponIdx = i;
            g_hero.CancelAttack();
            g_hero.EquipWeapon(kWeapons[i].info);
            // Restart fight cycle with new weapon
            if (aiState == AIState::ATTACK) {
              aiState = AIState::ATTACK;
              monsterCounterPending = false;
              monsterCounterTimer = 0.0f;
            }
          }
        }
      }
      if (treeOpen) {
        ImGui::TreePop();
      }

      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Equipped: %s",
                         kWeapons[currentWeaponIdx].displayName);
      ImGui::Text("Attack: %s",
                  WeaponTypeName(kWeapons[currentWeaponIdx].info));
    }

    // ── Controls ──
    ImGui::Separator();
    ImGui::Checkbox("Play Animation", &animPlaying);
    if (ImGui::Button("Respawn Monster")) {
      RespawnMonster();
    }

    // ── Combat Status HUD ──
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Combat Status",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("AI State: %s", AIStateName(aiState));
      ImGui::Text("Hero Attack: %s",
                  AttackStateName(g_hero.GetAttackState()));
      ImGui::Text("Hit Count: %d", hitCount);

      if (g_monsterManager.GetMonsterCount() > 0) {
        MonsterInfo mi = g_monsterManager.GetMonsterInfo(0);
        float hpFrac =
            (mi.maxHp > 0) ? (float)mi.hp / (float)mi.maxHp : 0.0f;
        ImGui::Text("Monster: %s", mi.name.c_str());

        // HP bar
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
        char hpBuf[32];
        snprintf(hpBuf, sizeof(hpBuf), "%d / %d", mi.hp, mi.maxHp);
        ImGui::ProgressBar(hpFrac, ImVec2(-1, 0), hpBuf);
        ImGui::PopStyleColor();

        // Monster state
        const char *monStateNames[] = {"IDLE",    "WALKING",   "CHASING",
                                       "ATTACKING", "HIT",     "DYING",
                                       "DEAD"};
        int stateIdx = (int)mi.state;
        if (stateIdx >= 0 && stateIdx < 7)
          ImGui::Text("Monster State: %s", monStateNames[stateIdx]);

        if (monsterCounterPending)
          ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                             "Counter in %.1fs", monsterCounterTimer);
      }
    }

    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }

  // Cleanup
  CleanupGroundPlane();
  g_hero.Cleanup();
  g_monsterManager.Cleanup();
  g_vfxManager.Cleanup();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
}

int main(int argc, char **argv) {
#ifdef __APPLE__
  char path[1024];
  uint32_t size = sizeof(path);
  if (_NSGetExecutablePath(path, &size) == 0) {
    std::filesystem::path execPath(path);
    std::filesystem::current_path(execPath.parent_path());
  }
#endif

  Run();
  return 0;
}
