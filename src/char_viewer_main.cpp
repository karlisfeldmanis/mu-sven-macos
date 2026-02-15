#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "Screenshot.hpp"
#include "Shader.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>

static const std::string DATA_PATH =
    "/Users/karlisfeldmanis/Desktop/mu_remaster/"
    "references/other/MuMain/src/bin/Data/Player/";
static const int WIN_WIDTH = 1280;
static const int WIN_HEIGHT = 720;

// 0.97d classes: 4 base + 3 second class (MG has no 2nd class)
enum CharClass {
  CLASS_DW = 0,  // Dark Wizard  → Class01
  CLASS_DK,      // Dark Knight  → Class02
  CLASS_ELF,     // Fairy Elf    → Class03
  CLASS_MG,      // Magic Gladiator → Class04
  CLASS_SM,      // Soul Master  → Class201 (DW 2nd)
  CLASS_BK,      // Blade Knight → Class202 (DK 2nd)
  CLASS_ME,      // Muse Elf     → Class203 (ELF 2nd)
  CLASS_COUNT
};

static const char *kClassNames[] = {"Dark Wizard",     "Dark Knight",
                                    "Fairy Elf",       "Magic Gladiator",
                                    "Soul Master (2)", "Blade Knight (2)",
                                    "Muse Elf (2)"};

static const char *kClassSuffix[] = {"Class01", "Class02", "Class03",
                                     "Class04", "Class201", "Class202",
                                     "Class203"};

// Body part slot names and BMD prefix
// PART_HEAD is a separate slot for the base head model (HelmClassXX.bmd),
// rendered underneath "accessory" helms that don't cover the full head.
enum BodyPart { PART_HELM = 0, PART_ARMOR, PART_PANTS, PART_GLOVES, PART_BOOTS, PART_HEAD, PART_COUNT };
static const char *kPartPrefix[] = {"Helm", "Armor", "Pant", "Glove", "Boot", "Helm"};
static const char *kPartNames[] = {"Helm", "Armor", "Pants", "Gloves", "Boots", "Head"};

// --- Armor set definitions (0.97d scope) ---
// File naming: {Part}{fileSuffix}.bmd — e.g. "Male01" → ArmorMale01.bmd
// fileSuffix=nullptr means naked (uses class-specific base model)
struct ArmorSetDef {
  const char *name;
  const char *fileSuffix;   // "Male01", "Elf03", "ElfC01", etc. (nullptr = naked)
  bool dk, dw, elf, mg;     // which base classes can wear this
  bool showHead;             // true = render base head (HelmClassXX) underneath helm
};

// Class availability for 2nd classes: SM inherits DW, BK inherits DK, ME inherits ELF
static bool CanClassWearSet(int classIdx, const ArmorSetDef &set) {
  switch (classIdx) {
  case CLASS_DW: return set.dw;
  case CLASS_DK: return set.dk;
  case CLASS_ELF: return set.elf;
  case CLASS_MG: return set.mg;
  case CLASS_SM: return set.dw;  // Soul Master = DW 2nd class
  case CLASS_BK: return set.dk;  // Blade Knight = DK 2nd class
  case CLASS_ME: return set.elf; // Muse Elf = ELF 2nd class
  default: return false;
  }
}

// 0.97d armor sets: base (0-14) + 2nd class tier (15-20)
// Reference: ZzzOpenData.cpp, item indices, MU wiki
// showHead whitelist from ZzzCharacter.cpp:11718 — MODEL_HELM indices 0,2,10-13
//   Male01=idx0(Bronze), Male03=idx2(Pad), Elf01-04=idx10-13
static const ArmorSetDef kArmorSets[] = {
    // name                suffix      DK    DW    ELF   MG    showHead
    {"Naked",              nullptr,    true, true, true, true, false},
    // --- DK sets (ArmorMale) ---
    {"Bronze",             "Male01",   true, false,false,true, true },
    {"Dragon",             "Male02",   true, false,false,true, false},
    {"Leather",            "Male06",   true, false,false,true, false},
    {"Scale",              "Male07",   true, false,false,true, false},
    {"Brass",              "Male09",   true, false,false,true, false},
    {"Plate",              "Male10",   true, false,false,true, false},
    // --- DW sets (ArmorMale) ---
    {"Pad",                "Male03",   false,true, false,true, true },
    {"Legendary",          "Male04",   false,true, false,true, false},
    {"Bone",               "Male05",   false,true, false,true, false},
    {"Sphinx",             "Male08",   false,true, false,true, false},
    // --- ELF sets (ArmorElf) ---
    {"Vine",               "Elf01",    false,false,true, false,true },
    {"Silk",               "Elf02",    false,false,true, false,true },
    {"Wind",               "Elf03",    false,false,true, false,true },
    {"Spirit",             "Elf04",    false,false,true, false,true },
    {"Guardian",           "Elf05",    false,false,true, false,false},
    // --- 2nd class tier sets (DK/BK) ---
    {"Storm Crow",         "Male16",   true, false,false,false,false},
    {"Black Dragon",       "Male17",   true, false,false,false,false},
    // --- 2nd class tier sets (DW/SM) ---
    {"Dark Phoenix",       "Male18",   false,true, false,false,false},
    {"Grand Soul",         "Male19",   false,true, false,false,false},
    // --- 2nd class tier sets (ELF/ME) ---
    {"Divine",             "ElfC01",   false,false,true, false,false},
    {"Thunder Hawk",       "ElfC02",   false,false,true, false,false},
};
static const int kNumArmorSets = sizeof(kArmorSets) / sizeof(kArmorSets[0]);

// Animation category for UI grouping
struct AnimEntry {
  const char *name;
  int actionIndex;
};

struct AnimCategory {
  const char *name;
  std::vector<AnimEntry> entries;
};

// Female classes: ELF and ME (Muse Elf = ELF 2nd class)
static bool IsFemaleClass(int classIdx) {
  return classIdx == CLASS_ELF || classIdx == CLASS_ME;
}

// Build animation categories with correct indices from _enum.h.
// Skills tab is class-aware: DK=sword, DW=magic, ELF=elf cast, MG=hybrid.
// Reference: _enum.h (PLAYER_SET=0, sequential), SkillManager.h (AT_SKILL_*)
static std::vector<AnimCategory> BuildAnimCategories(int classIdx) {
  bool female = IsFemaleClass(classIdx);
  bool isDK = (classIdx == CLASS_DK || classIdx == CLASS_BK);
  bool isDW = (classIdx == CLASS_DW || classIdx == CLASS_SM);
  bool isElf = (classIdx == CLASS_ELF || classIdx == CLASS_ME);
  bool isMG = (classIdx == CLASS_MG);

  std::vector<AnimCategory> cats;

  // --- Idle/Stop ---
  {
    AnimCategory cat{"Idle", {}};
    cat.entries.push_back({female ? "Stop (Female)" : "Stop (Male)",
                           female ? 2 : 1});
    cat.entries.push_back({"Sword", 4});
    cat.entries.push_back({"Two-Hand Sword", 5});
    cat.entries.push_back({"Spear", 6});
    cat.entries.push_back({"Scythe/Staff", 7});
    cat.entries.push_back({"Bow", 8});
    cat.entries.push_back({"Crossbow", 9});
    cat.entries.push_back({"Flying", 11});
    cats.push_back(cat);
  }

  // --- Walk ---
  {
    AnimCategory cat{"Walk", {}};
    cat.entries.push_back({female ? "Walk (Female)" : "Walk (Male)",
                           female ? 16 : 15});
    cat.entries.push_back({"Sword", 17});
    cat.entries.push_back({"Two-Hand Sword", 18});
    cat.entries.push_back({"Spear", 19});
    cat.entries.push_back({"Scythe/Staff", 20});
    cat.entries.push_back({"Bow", 21});
    cat.entries.push_back({"Crossbow", 22});
    cats.push_back(cat);
  }

  // --- Run ---
  {
    AnimCategory cat{"Run", {}};
    cat.entries.push_back({"Run", 25});
    cat.entries.push_back({"Sword", 26});
    cat.entries.push_back({"Dual Wield", 27});
    cat.entries.push_back({"Two-Hand Sword", 28});
    cat.entries.push_back({"Spear", 29});
    cat.entries.push_back({"Bow", 30});
    cat.entries.push_back({"Crossbow", 31});
    cat.entries.push_back({"Fly", 34});
    cats.push_back(cat);
  }

  // --- Combat ---
  {
    AnimCategory cat{"Combat", {}};
    cat.entries.push_back({"Fist", 38});
    cat.entries.push_back({"Sword R1", 39});
    cat.entries.push_back({"Sword R2", 40});
    cat.entries.push_back({"Sword L1", 41});
    cat.entries.push_back({"Sword L2", 42});
    cat.entries.push_back({"Two-Hand 1", 43});
    cat.entries.push_back({"Two-Hand 2", 44});
    cat.entries.push_back({"Two-Hand 3", 45});
    cat.entries.push_back({"Spear", 46});
    cat.entries.push_back({"Scythe 1", 47});
    cat.entries.push_back({"Scythe 2", 48});
    cat.entries.push_back({"Scythe 3", 49});
    cat.entries.push_back({"Bow", 50});
    cat.entries.push_back({"Crossbow", 51});
    cats.push_back(cat);
  }

  // --- Skills (class-specific) ---
  // DK/BK: sword skills. DW/SM: magic. ELF/ME: elf cast. MG: sword + magic.
  // Reference: SkillManager.h AT_SKILL_*, ZzzCharacter.cpp SetPlayerAttack
  {
    AnimCategory cat{"Skills", {}};

    // DK sword skills (also available to MG)
    // Reference: _enum.h PLAYER_ATTACK_SKILL_SWORD1=60..SWORD5=64
    if (isDK || isMG) {
      cat.entries.push_back({"Falling Slash", 60});
      cat.entries.push_back({"Lunge", 61});
      cat.entries.push_back({"Uppercut", 62});
      cat.entries.push_back({"Cyclone", 63});
      cat.entries.push_back({"Slash", 64});
      cat.entries.push_back({"Twisting Slash", 65}); // PLAYER_ATTACK_SKILL_WHEEL
      cat.entries.push_back({"Rageful Blow", 66});   // PLAYER_ATTACK_SKILL_FURY_STRIKE
      cat.entries.push_back({"Spear Skill", 70});    // PLAYER_ATTACK_SKILL_SPEAR
      cat.entries.push_back({"Death Stab", 71});     // PLAYER_ATTACK_ONETOONE
    }

    // DW magic skills (also available to MG except Teleport)
    // Action indices from _enum.h (0.97d, no YDG_ADD_SKILL_RIDING_ANIMATIONS ifdef)
    if (isDW || isMG) {
      cat.entries.push_back({"Energy Ball", 146});   // PLAYER_SKILL_HAND1
      cat.entries.push_back({"Magic Cast 2", 147});  // PLAYER_SKILL_HAND2
      cat.entries.push_back({"Staff Cast 1", 148});  // PLAYER_SKILL_WEAPON1
      cat.entries.push_back({"Staff Cast 2", 149});  // PLAYER_SKILL_WEAPON2
      cat.entries.push_back({"Aqua Beam", 152});     // PLAYER_SKILL_FLASH
      cat.entries.push_back({"Inferno", 153});       // PLAYER_SKILL_INFERNO
      cat.entries.push_back({"Hell Fire", 154});     // PLAYER_SKILL_HELL
    }

    // Teleport — DW/SM only (not MG)
    if (isDW) {
      cat.entries.push_back({"Teleport", 151});      // PLAYER_SKILL_TELEPORT
    }

    // ELF/ME skills
    if (isElf) {
      cat.entries.push_back({"Heal", 67});           // PLAYER_SKILL_VITALITY
      cat.entries.push_back({"Elf Buff", 150});      // PLAYER_SKILL_ELF1 (Greater Def/Dmg)
      cat.entries.push_back({"Penetration", 50});    // bow attack anim
      cat.entries.push_back({"Ice Arrow", 51});      // crossbow attack anim
    }

    cats.push_back(cat);
  }

  // --- Emotes (male/female variants) ---
  {
    AnimCategory cat{"Emotes", {}};
    cat.entries.push_back({"Defense", 186});
    cat.entries.push_back({"Greeting", female ? 188 : 187});
    cat.entries.push_back({"Goodbye", female ? 190 : 189});
    cat.entries.push_back({"Clap", female ? 192 : 191});
    cat.entries.push_back({"Cheer", female ? 194 : 193});
    cat.entries.push_back({"Direction", female ? 196 : 195});
    cat.entries.push_back({"Gesture", female ? 198 : 197});
    cat.entries.push_back({"Cry", female ? 202 : 201});
    cat.entries.push_back({"Awkward", female ? 204 : 203});
    cat.entries.push_back({"See", female ? 206 : 205});
    cat.entries.push_back({"Win", female ? 208 : 207});
    cat.entries.push_back({"Smile", female ? 210 : 209});
    cat.entries.push_back({"Sleep", female ? 212 : 211});
    cat.entries.push_back({"Cold", female ? 214 : 213});
    cat.entries.push_back({"Again", female ? 216 : 215});
    cat.entries.push_back({"Respect", 217});
    cat.entries.push_back({"Salute", 218});
    cat.entries.push_back({"Scissors", 219});
    cat.entries.push_back({"Rock", 220});
    cat.entries.push_back({"Paper", 221});
    cats.push_back(cat);
  }

  // --- Other ---
  {
    AnimCategory cat{"Other", {}};
    cat.entries.push_back({"Shock", 230});
    cat.entries.push_back({"Die 1", 231});
    cat.entries.push_back({"Die 2", 232});
    cat.entries.push_back({"Sit 1", female ? 235 : 233});
    cat.entries.push_back({"Sit 2", female ? 236 : 234});
    cat.entries.push_back({"Healing", female ? 238 : 237});
    cat.entries.push_back({"Pose", female ? 240 : 239});
    cats.push_back(cat);
  }

  return cats;
}

class CharacterViewer {
public:
  void Run() {
    if (!InitWindow())
      return;

    ActivateMacOSApp();
    InitImGui(window);

    std::ifstream shaderTest("shaders/model.vert");
    shader = std::make_unique<Shader>(
        shaderTest.good() ? "shaders/model.vert" : "../shaders/model.vert",
        shaderTest.good() ? "shaders/model.frag" : "../shaders/model.frag");

    axes.Init();
    animCategories = BuildAnimCategories(currentClass);

    // Load skeleton + default class
    LoadSkeleton();
    if (!skeleton) {
      std::cerr << "[CharViewer] Failed to load Player.bmd skeleton"
                << std::endl;
      ShutdownImGui();
      glfwDestroyWindow(window);
      glfwTerminate();
      return;
    }

    LoadClass(currentClass);

    while (!glfwWindowShouldClose(window)) {
      float now = glfwGetTime();
      deltaTime = now - lastFrame;
      lastFrame = now;

      glfwPollEvents();
      RenderScene();
      Screenshot::TickRecording(window);
      RenderUI();
      glfwSwapBuffers(window);
    }

    UnloadParts();
    axes.Cleanup();
    ShutdownImGui();
    glfwDestroyWindow(window);
    glfwTerminate();
  }

private:
  GLFWwindow *window = nullptr;
  std::unique_ptr<Shader> shader;

  // Skeleton (Player.bmd — bones + actions only, zero meshes)
  std::unique_ptr<BMDData> skeleton;
  int totalActions = 0;

  // 5 body part slots
  struct BodyPartSlot {
    std::unique_ptr<BMDData> bmd;
    std::vector<MeshBuffers> meshBuffers;
    std::string filename;
    bool loaded = false;
  };
  BodyPartSlot parts[PART_COUNT];

  // Class + armor selection
  int currentClass = CLASS_DW;
  int currentArmorSet = 0; // index into kArmorSets (0 = Naked)

  // Animation state
  int currentAction = 0;
  float animFrame = 0.0f;
  float animSpeed = 4.0f;
  bool animPlaying = true;
  int currentNumKeys = 0;

  // Animation categories
  std::vector<AnimCategory> animCategories;

  // Orbit camera + axes
  OrbitCamera camera;
  DebugAxes axes;

  // Mouse
  bool dragging = false;
  double lastMouseX = 0, lastMouseY = 0;

  // Timing
  float deltaTime = 0.0f;
  float lastFrame = 0.0f;

  // GIF recording
  int gifFrameTarget = 72;
  float gifScaleSetting = 0.5f;
  int gifFpsSetting = 12;

  // --- Init ---

  bool InitWindow() {
    if (!glfwInit())
      return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    window = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, "MU Character Viewer",
                              nullptr, nullptr);
    if (!window)
      return false;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK)
      return false;
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glfwSetWindowUserPointer(window, this);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCharCallback(window, CharCallback);

    return true;
  }

  // --- Skeleton ---

  void LoadSkeleton() {
    std::string path = DATA_PATH + "player.bmd";
    skeleton = BMDParser::Parse(path);
    if (skeleton) {
      totalActions = (int)skeleton->Actions.size();
      std::cout << "[CharViewer] Player.bmd: " << skeleton->Bones.size()
                << " bones, " << totalActions << " actions, "
                << skeleton->Meshes.size() << " meshes" << std::endl;

      // Set initial action keyframes
      if (totalActions > 0) {
        currentNumKeys = skeleton->Actions[0].NumAnimationKeys;
      }
    }
  }

  // --- Body parts ---

  void UnloadParts() {
    for (int i = 0; i < PART_COUNT; ++i) {
      CleanupMeshBuffers(parts[i].meshBuffers);
      parts[i].bmd.reset();
      parts[i].filename.clear();
      parts[i].loaded = false;
    }
  }

  void LoadClass(int classIdx) {
    currentClass = classIdx;

    // Rebuild animation categories for class-specific skills + male/female variants
    animCategories = BuildAnimCategories(classIdx);

    // Reset to Naked if current armor set not available for new class
    if (!CanClassWearSet(classIdx, kArmorSets[currentArmorSet]))
      currentArmorSet = 0; // Naked is always available

    LoadArmorSet(currentArmorSet);

    // Reset animation to class-appropriate idle
    animFrame = 0.0f;
    SetAction(IsFemaleClass(classIdx) ? 2 : 1);
  }

  // Build filename for a body part: either ClassXX (naked) or Male##/Elf##/ElfC## (armor)
  std::string BuildPartFilename(int partIdx, int armorSetIdx) const {
    const auto &set = kArmorSets[armorSetIdx];

    if (!set.fileSuffix) {
      // Naked — use class-specific base model
      return std::string(kPartPrefix[partIdx]) + kClassSuffix[currentClass] +
             ".bmd";
    }

    // Armor set — e.g. "Armor" + "Male01" + ".bmd"
    return std::string(kPartPrefix[partIdx]) + set.fileSuffix + ".bmd";
  }

  void LoadArmorSet(int armorSetIdx) {
    UnloadParts();
    currentArmorSet = armorSetIdx;

    auto bones = ComputeBoneMatrices(skeleton.get());
    AABB totalAABB{};

    // Load 5 equipment body parts (Helm, Armor, Pants, Gloves, Boots)
    for (int p = 0; p < PART_BOOTS + 1; ++p) {
      std::string filename = BuildPartFilename(p, armorSetIdx);
      std::string fullPath = DATA_PATH + filename;

      auto bmd = BMDParser::Parse(fullPath);
      if (!bmd) {
        std::cerr << "[CharViewer] Failed to load: " << filename << std::endl;
        continue;
      }

      std::cout << "[CharViewer] Loaded " << filename << ": "
                << bmd->Meshes.size() << " meshes" << std::endl;

      for (auto &mesh : bmd->Meshes) {
        UploadMeshWithBones(mesh, DATA_PATH, bones, parts[p].meshBuffers,
                            totalAABB, true);
      }

      parts[p].bmd = std::move(bmd);
      parts[p].filename = filename;
      parts[p].loaded = true;
    }

    // Load base head model (HelmClassXX.bmd) for accessory helms that show
    // the head underneath. Naked doesn't need this — its helm IS the head.
    // Reference: ZzzCharacter.cpp:11718 SetCharacterScale() whitelist
    const auto &set = kArmorSets[armorSetIdx];
    if (set.fileSuffix && set.showHead) {
      std::string headFile =
          std::string("Helm") + kClassSuffix[currentClass] + ".bmd";
      std::string headPath = DATA_PATH + headFile;

      auto headBmd = BMDParser::Parse(headPath);
      if (headBmd) {
        std::cout << "[CharViewer] Head: " << headFile << " ("
                  << headBmd->Meshes.size() << " meshes)" << std::endl;
        for (auto &mesh : headBmd->Meshes) {
          UploadMeshWithBones(mesh, DATA_PATH, bones,
                              parts[PART_HEAD].meshBuffers, totalAABB, true);
        }
        parts[PART_HEAD].bmd = std::move(headBmd);
        parts[PART_HEAD].filename = headFile;
        parts[PART_HEAD].loaded = true;
      }
    }

    AutoFrame(totalAABB);
    UpdateWindowTitle();
  }

  void UpdateWindowTitle() {
    std::string title = "MU Character Viewer - " +
                        std::string(kClassNames[currentClass]) + " [" +
                        kArmorSets[currentArmorSet].name + "]";
    glfwSetWindowTitle(window, title.c_str());
  }

  void AutoFrame(const AABB &aabb) {
    glm::vec3 c = aabb.center();
    camera.center = glm::vec3(c.x, c.z, -c.y);
    float radius = aabb.radius();
    if (radius < 0.001f)
      radius = 100.0f;

    camera.distance = radius * 2.6f;
    camera.yaw = 180.0f;
    camera.pitch = -15.0f;

    axes.length = radius * 0.3f;
    axes.UpdateGeometry();
  }

  void SetAction(int action) {
    if (action < 0 || action >= totalActions)
      action = 0;
    currentAction = action;
    animFrame = 0.0f;
    currentNumKeys = skeleton->Actions[action].NumAnimationKeys;
  }

  // --- Rendering ---

  void RenderScene() {
    glClearColor(0.15f, 0.18f, 0.22f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Advance animation
    if (animPlaying && currentNumKeys > 1) {
      animFrame += animSpeed * deltaTime;
      if (animFrame >= (float)currentNumKeys)
        animFrame = std::fmod(animFrame, (float)currentNumKeys);
    }

    // Compute skeleton bones for current frame
    auto bones = ComputeBoneMatricesInterpolated(skeleton.get(), currentAction,
                                                 animFrame);

    // Re-skin all body part meshes
    for (int p = 0; p < PART_COUNT; ++p) {
      if (!parts[p].loaded)
        continue;
      for (int mi = 0; mi < (int)parts[p].meshBuffers.size() &&
                        mi < (int)parts[p].bmd->Meshes.size();
           ++mi) {
        RetransformMeshWithBones(parts[p].bmd->Meshes[mi], bones,
                                 parts[p].meshBuffers[mi]);
      }
    }

    shader->use();

    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), (float)fbWidth / (float)fbHeight, 0.1f,
        100000.0f);
    glm::mat4 view = camera.GetViewMatrix();
    // MU Z-up → GL Y-up
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                                  glm::vec3(1.0f, 0.0f, 0.0f));

    shader->setMat4("projection", projection);
    shader->setMat4("view", view);
    shader->setMat4("model", model);

    glm::vec3 eye = camera.GetEyePosition();
    shader->setVec3("lightPos", eye + glm::vec3(0, 200, 0));
    shader->setVec3("lightColor", 1.0f, 1.0f, 1.0f);
    shader->setVec3("viewPos", eye);
    shader->setBool("useFog", false);
    shader->setFloat("blendMeshLight", 1.0f);
    shader->setFloat("objectAlpha", 1.0f);
    shader->setVec3("terrainLight", 1.0f, 1.0f, 1.0f);
    shader->setVec2("texCoordOffset", glm::vec2(0.0f));
    shader->setInt("numPointLights", 0);

    // Draw all body parts
    for (int p = 0; p < PART_COUNT; ++p) {
      for (auto &mb : parts[p].meshBuffers) {
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

    // Debug axes
    glm::mat4 mvp = projection * view * model;
    axes.Draw(mvp);
  }

  void RenderUI() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(280, (float)winH));
    ImGui::Begin("Character", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse);

    // --- Class selection ---
    ImGui::Text("Class:");
    for (int i = 0; i < CLASS_COUNT; ++i) {
      if (ImGui::RadioButton(kClassNames[i], currentClass == i)) {
        LoadClass(i);
      }
    }

    // --- Armor set selection ---
    ImGui::Separator();
    ImGui::Text("Armor Set:");
    if (ImGui::BeginCombo("##armor", kArmorSets[currentArmorSet].name)) {
      for (int i = 0; i < kNumArmorSets; ++i) {
        if (!CanClassWearSet(currentClass, kArmorSets[i]))
          continue;
        bool selected = (currentArmorSet == i);
        if (ImGui::Selectable(kArmorSets[i].name, selected)) {
          LoadArmorSet(i);
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    // --- Body parts info ---
    ImGui::Separator();
    ImGui::Text("Body Parts:");
    int totalMeshes = 0, totalTris = 0;
    for (int p = 0; p < PART_COUNT; ++p) {
      if (parts[p].loaded) {
        int tris = 0;
        for (auto &m : parts[p].bmd->Meshes)
          tris += m.NumTriangles;
        totalMeshes += (int)parts[p].bmd->Meshes.size();
        totalTris += tris;
        ImGui::BulletText("%s: %s (%d tri)", kPartNames[p],
                          parts[p].filename.c_str(), tris);
      } else {
        ImGui::BulletText("%s: (missing)", kPartNames[p]);
      }
    }

    ImGui::Separator();
    ImGui::Text("Bones: %d", (int)skeleton->Bones.size());
    ImGui::Text("Actions: %d", totalActions);
    ImGui::Text("Meshes: %d | Tris: %d", totalMeshes, totalTris);

    // --- Animation categories (tab bar) ---
    ImGui::Separator();
    ImGui::Text("Animation:");

    if (ImGui::BeginTabBar("AnimTabs")) {
      for (auto &cat : animCategories) {
        if (ImGui::BeginTabItem(cat.name)) {
          for (auto &entry : cat.entries) {
            // Skip entries that exceed available actions
            if (entry.actionIndex >= totalActions)
              continue;

            bool selected = (currentAction == entry.actionIndex);
            char label[64];
            snprintf(label, sizeof(label), "%s [%d]", entry.name,
                     entry.actionIndex);
            if (ImGui::Selectable(label, selected)) {
              SetAction(entry.actionIndex);
            }
          }
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }

    // --- Raw action slider (fallback) ---
    ImGui::Separator();
    int actionVal = currentAction;
    if (ImGui::SliderInt("Action##raw", &actionVal, 0,
                         std::max(0, totalActions - 1))) {
      SetAction(actionVal);
    }

    // --- Playback controls ---
    ImGui::Checkbox("Play", &animPlaying);
    ImGui::SameLine();
    ImGui::SliderFloat("Speed", &animSpeed, 0.5f, 20.0f, "%.1f");

    float frameVal = animFrame;
    if (ImGui::SliderFloat("Frame", &frameVal, 0.0f,
                            (float)std::max(1, currentNumKeys - 1), "%.1f")) {
      animFrame = frameVal;
    }
    ImGui::Text("Keys: %d", currentNumKeys);

    // --- GIF recording ---
    ImGui::Separator();
    ImGui::Text("GIF Recording:");
    ImGui::SliderFloat("Scale", &gifScaleSetting, 0.1f, 1.0f, "%.2f");
    ImGui::SliderInt("FPS", &gifFpsSetting, 5, 25);
    ImGui::SliderInt("Frames", &gifFrameTarget, 10, 200);

    if (Screenshot::IsRecording()) {
      float progress = Screenshot::GetProgress();
      const char *label =
          Screenshot::IsWarmingUp() ? "Warming up..." : "Recording...";
      ImGui::ProgressBar(progress, ImVec2(-1, 0), label);
    } else {
      if (ImGui::Button("Capture GIF", ImVec2(-1, 0))) {
        int skip = 25 / gifFpsSetting;
        Screenshot::StartRecording(window, "screenshots/char_capture.gif",
                                   gifFrameTarget, 100 / gifFpsSetting,
                                   gifScaleSetting, skip - 1);
      }
    }

    ImGui::Separator();
    ImGui::TextWrapped("LMB drag: Rotate\nScroll: Zoom\nESC: Quit");

    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  // --- GLFW Callbacks ---

  static void ScrollCallback(GLFWwindow *w, double xoff, double yoff) {
    auto *self = static_cast<CharacterViewer *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_ScrollCallback(w, xoff, yoff);
    if (ImGui::GetIO().WantCaptureMouse)
      return;
    self->camera.distance -= (float)yoff * self->camera.distance * 0.15f;
    self->camera.distance = glm::clamp(self->camera.distance, 1.0f, 50000.0f);
  }

  static void MouseButtonCallback(GLFWwindow *w, int button, int action,
                                  int mods) {
    auto *self = static_cast<CharacterViewer *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
      if (action == GLFW_PRESS && !ImGui::GetIO().WantCaptureMouse) {
        self->dragging = true;
        glfwGetCursorPos(w, &self->lastMouseX, &self->lastMouseY);
      } else if (action == GLFW_RELEASE) {
        self->dragging = false;
      }
    }
  }

  static void CursorPosCallback(GLFWwindow *w, double x, double y) {
    auto *self = static_cast<CharacterViewer *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_CursorPosCallback(w, x, y);
    if (self->dragging && !ImGui::GetIO().WantCaptureMouse) {
      float dx = (float)(x - self->lastMouseX);
      float dy = (float)(y - self->lastMouseY);
      self->lastMouseX = x;
      self->lastMouseY = y;
      self->camera.yaw += dx * 0.3f;
      self->camera.pitch += dy * 0.3f;
      self->camera.pitch = glm::clamp(self->camera.pitch, -89.0f, 89.0f);
    }
  }

  static void KeyCallback(GLFWwindow *w, int key, int scancode, int action,
                          int mods) {
    auto *self = static_cast<CharacterViewer *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);
    if (ImGui::GetIO().WantCaptureKeyboard)
      return;
    if (action == GLFW_PRESS) {
      if (key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(w, true);
    }
  }

  static void CharCallback(GLFWwindow *w, unsigned int c) {
    ImGui_ImplGlfw_CharCallback(w, c);
  }
};

int main(int argc, char **argv) {
  CharacterViewer viewer;
  viewer.Run();
  return 0;
}
