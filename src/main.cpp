#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "Camera.hpp"
#include "ClickEffect.hpp"
#include "ClientPacketHandler.hpp"
#include "ClientTypes.hpp"
#include "FireEffect.hpp"
#include "GrassRenderer.hpp"
#include "GroundItemRenderer.hpp"
#include "HeroCharacter.hpp"
#include "InputHandler.hpp"
#include "InventoryUI.hpp"
#include "ItemDatabase.hpp"
#include "ItemModelManager.hpp"
#include "MockData.hpp"
#include "MonsterManager.hpp"
#include "NpcManager.hpp"
#include "ObjectRenderer.hpp"
#include "RayPicker.hpp"
#include "Screenshot.hpp"
#include "ServerConnection.hpp"
#include "Shader.hpp"
#include "Sky.hpp"
#include "Terrain.hpp"
#include "TerrainParser.hpp"
#include "UICoords.hpp"
#include "UITexture.hpp"
#include "VFXManager.hpp"
#include "ViewerCommon.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <streambuf>
#include <turbojpeg.h>
#include <unistd.h>

// Tee streambuf: writes to both a file and the original stream
class TeeStreambuf : public std::streambuf {
public:
  TeeStreambuf(std::streambuf *orig, std::streambuf *file)
      : original(orig), fileBuf(file) {}

protected:
  int overflow(int c) override {
    if (c == EOF)
      return !EOF;
    int r1 = original->sputc(c);
    int r2 = fileBuf->sputc(c);
    return (r1 == EOF || r2 == EOF) ? EOF : c;
  }
  int sync() override {
    original->pubsync();
    fileBuf->pubsync();
    return 0;
  }

private:
  std::streambuf *original;
  std::streambuf *fileBuf;
};

#ifdef __APPLE__
#include <objc/message.h>
#include <objc/runtime.h>
static void activateMacOSApp() {
  id app =
      ((id(*)(id, SEL))objc_msgSend)((id)objc_getClass("NSApplication"),
                                     sel_registerName("sharedApplication"));
  ((void (*)(id, SEL, long))objc_msgSend)(
      app, sel_registerName("setActivationPolicy:"),
      0); // NSApplicationActivationPolicyRegular
  ((void (*)(id, SEL, BOOL))objc_msgSend)(
      app, sel_registerName("activateIgnoringOtherApps:"), YES);
}
#endif

// GL error checking utility — call after critical GL operations
static void checkGLError(const char *label) {
  GLenum err;
  while ((err = glGetError()) != GL_NO_ERROR) {
    const char *errStr = "UNKNOWN";
    switch (err) {
    case GL_INVALID_ENUM:
      errStr = "INVALID_ENUM";
      break;
    case GL_INVALID_VALUE:
      errStr = "INVALID_VALUE";
      break;
    case GL_INVALID_OPERATION:
      errStr = "INVALID_OP";
      break;
    case GL_OUT_OF_MEMORY:
      errStr = "OUT_OF_MEMORY";
      break;
    case GL_INVALID_FRAMEBUFFER_OPERATION:
      errStr = "INVALID_FBO";
      break;
    }
    std::cerr << "[GL ERROR] " << errStr << " (0x" << std::hex << err
              << std::dec << ") at " << label << std::endl;
  }
}

// OpenGL debug callback (ARB_debug_output) — logs all GL warnings/errors
static void GLAPIENTRY glDebugCallback(GLenum source, GLenum type, GLuint id,
                                       GLenum severity, GLsizei /*length*/,
                                       const GLchar *message,
                                       const void * /*userParam*/) {
  // Skip notifications (very noisy)
  if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
    return;
  const char *sevStr = "???";
  switch (severity) {
  case GL_DEBUG_SEVERITY_HIGH:
    sevStr = "HIGH";
    break;
  case GL_DEBUG_SEVERITY_MEDIUM:
    sevStr = "MED";
    break;
  case GL_DEBUG_SEVERITY_LOW:
    sevStr = "LOW";
    break;
  }
  const char *typeStr = "other";
  switch (type) {
  case GL_DEBUG_TYPE_ERROR:
    typeStr = "ERROR";
    break;
  case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
    typeStr = "DEPRECATED";
    break;
  case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
    typeStr = "UNDEFINED";
    break;
  case GL_DEBUG_TYPE_PERFORMANCE:
    typeStr = "PERF";
    break;
  }
  std::cerr << "[GL " << sevStr << "/" << typeStr << "] " << message
            << std::endl;
}

Camera g_camera(glm::vec3(12800.0f, 0.0f, 12800.0f));
Terrain g_terrain;
ObjectRenderer g_objectRenderer;
FireEffect g_fireEffect;
Sky g_sky;

GrassRenderer g_grass;
VFXManager g_vfxManager;

// Point lights collected from light-emitting world objects
static const int MAX_POINT_LIGHTS = 64;
static std::vector<PointLight> g_pointLights;

// Hero character and click-to-move effect
static HeroCharacter g_hero;
static ClickEffect g_clickEffect;
static NpcManager g_npcManager;
static MonsterManager g_monsterManager;
static ServerConnection g_server;

// NPC interaction state
static int g_hoveredNpc = -1;        // Index of NPC under mouse cursor
static int g_hoveredMonster = -1;    // Index of Monster under mouse cursor
static int g_hoveredGroundItem = -1; // Index of Ground Item under mouse cursor
static int g_selectedNpc = -1; // Index of NPC that was clicked (dialog open)

// Client-side item definitions (owned by ItemDatabase, reference here)
static auto &g_itemDefs = ItemDatabase::GetItemDefs();

// ── Floating damage numbers (type in GroundItemRenderer.hpp) ──
static FloatingDamage g_floatingDmg[MAX_FLOATING_DAMAGE] = {};

// Ground item drops (type in ClientTypes.hpp)
static GroundItem g_groundItems[MAX_GROUND_ITEMS] = {};
static const std::string g_dataPath = "Data";

// Server-received character stats for HUD
static int g_serverLevel = 1;
static int g_serverHP = 110, g_serverMaxHP = 110;
static int g_serverMP = 20, g_serverMaxMP = 20;
static int g_serverStr = 28, g_serverDex = 20, g_serverVit = 25,
           g_serverEne = 10;
static int g_serverLevelUpPoints = 0;
static int64_t g_serverXP = 0;
static int g_serverDefense = 0, g_serverAttackSpeed = 0, g_serverMagicSpeed = 0;

// Inventory & UI state
static bool g_showCharInfo = false;
static bool g_showInventory = false;

// Quick slot (Q) item
// Quick slot assignments
static int16_t g_quickSlotDefIndex = 850; // Apple by default
static ImVec2 g_quickSlotPos = {0, 0};    // Screen pos of Q slot for overlays
static float g_potionCooldown = 0.0f;     // Potion cooldown timer (seconds)
static constexpr float POTION_COOLDOWN_TIME = 30.0f;
static bool g_shopOpen = false;
static std::vector<ShopItem> g_shopItems;

// Client-side inventory (synced from server via 0x36)
// ClientInventoryItem defined in ClientTypes.hpp
static ClientInventoryItem g_inventory[INVENTORY_SLOTS] = {};
static uint32_t g_zen = 0;
static bool g_syncDone =
    false; // Safeguard: don't send updates until initial sync done

// Equipment display (type in ClientTypes.hpp)
static ClientEquipSlot g_equipSlots[12] = {};

// UI fonts
static ImFont *g_fontDefault = nullptr;
static ImFont *g_fontBold = nullptr;

static UICoords g_hudCoords; // File-scope for mouse callback access

// ServerEquipSlot defined in ClientTypes.hpp

// Delegated to ClientPacketHandler::HandleInitialPacket
// ServerData defined in ClientTypes.hpp

// Delegated to ClientPacketHandler::HandleInitialPacket
// (see src/ClientPacketHandler.cpp)

// Delegated to ClientPacketHandler::HandleGamePacket
// (see src/ClientPacketHandler.cpp)

static const TerrainData *g_terrainDataPtr = nullptr;

// Roof hiding: types 125 (HouseWall05) and 126 (HouseWall06) fade when
// hero stands on layer1 tile == 4 (building interior). Original:
// ZzzObject.cpp:3744
static std::unordered_map<int, float> g_typeAlpha = {{125, 1.0f}, {126, 1.0f}};
static std::unordered_map<int, float> g_typeAlphaTarget = {{125, 1.0f},
                                                           {126, 1.0f}};

struct LightTemplate {
  glm::vec3 color;
  float range;
  float heightOffset; // Y offset above object base for emission point
};

// Returns light properties for a given object type, or nullptr if not a light
static const LightTemplate *GetLightProperties(int type) {
  static const LightTemplate fireLightProps = {glm::vec3(1.5f, 0.9f, 0.5f),
                                               800.0f, 150.0f};
  static const LightTemplate bonfireProps = {glm::vec3(1.5f, 0.75f, 0.3f),
                                             1000.0f, 100.0f};
  static const LightTemplate gateProps = {glm::vec3(1.5f, 0.9f, 0.5f), 800.0f,
                                          200.0f};
  static const LightTemplate bridgeProps = {glm::vec3(1.2f, 0.7f, 0.4f), 700.0f,
                                            50.0f};
  static const LightTemplate streetLightProps = {glm::vec3(1.5f, 1.2f, 0.75f),
                                                 800.0f, 250.0f};
  static const LightTemplate candleProps = {glm::vec3(1.2f, 0.7f, 0.3f), 600.0f,
                                            80.0f};
  static const LightTemplate lightFixtureProps = {glm::vec3(1.2f, 0.85f, 0.5f),
                                                  700.0f, 150.0f};

  switch (type) {
  case 50:
  case 51:
    return &fireLightProps;
  case 52:
    return &bonfireProps;
  case 55:
    return &gateProps;
  case 80:
    return &bridgeProps;
  case 90:
    return &streetLightProps;
  case 130:
  case 131:
  case 132:
    return &lightFixtureProps;
  case 150:
    return &candleProps;
  default:
    return nullptr;
  }
}

// Input handling (mouse, keyboard, click-to-move, processInput) delegated
// to InputHandler module (see src/InputHandler.cpp)

// Panel rendering, click handling, drag/drop, tooltip, and item layout
// all delegated to InventoryUI module (see src/InventoryUI.cpp)

int main(int argc, char **argv) {
  // Open client.log — tee all cout/cerr to both console and file
  std::ofstream logFile("client.log", std::ios::trunc);
  TeeStreambuf *coutTee = nullptr, *cerrTee = nullptr;
  std::streambuf *origCout = nullptr, *origCerr = nullptr;
  if (logFile.is_open()) {
    // Log header with timestamp
    std::time_t now = std::time(nullptr);
    logFile << "=== MuRemaster client.log === " << std::ctime(&now)
            << std::endl;
    logFile.flush();

    origCout = std::cout.rdbuf();
    origCerr = std::cerr.rdbuf();
    coutTee = new TeeStreambuf(origCout, logFile.rdbuf());
    cerrTee = new TeeStreambuf(origCerr, logFile.rdbuf());
    std::cout.rdbuf(coutTee);
    std::cerr.rdbuf(cerrTee);
  }

  struct StreamRedirector {
    std::streambuf *origCout, *origCerr;
    TeeStreambuf *coutTee, *cerrTee;
    StreamRedirector(std::streambuf *oc, std::streambuf *oce, TeeStreambuf *ct,
                     TeeStreambuf *cet)
        : origCout(oc), origCerr(oce), coutTee(ct), cerrTee(cet) {}
    ~StreamRedirector() {
      if (origCout)
        std::cout.rdbuf(origCout);
      if (origCerr)
        std::cerr.rdbuf(origCerr);
      delete coutTee;
      delete cerrTee;
    }
  } redirector(origCout, origCerr, coutTee, cerrTee);

  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    return -1;
  }

  // GL 3.3 + GLSL 150
  const char *glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_STENCIL_BITS, 8);

  GLFWwindow *window = glfwCreateWindow(
      1366, 768, "Mu Online Remaster (Native macOS C++)", nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // Enable vsync

#ifdef __APPLE__
  activateMacOSApp();
#endif

  if (glewInit() != GLEW_OK) {
    std::cerr << "Failed to initialize GLEW" << std::endl;
    return -1;
  }
  ItemDatabase::Init();

  // Enable OpenGL debug output if available (ARB_debug_output)
  if (GLEW_ARB_debug_output) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallbackARB(glDebugCallback, nullptr);
    std::cout << "[GL] Debug output enabled" << std::endl;
  } else {
    std::cout << "[GL] Debug output not available — using manual checks"
              << std::endl;
  }
  std::cout << "[GL] Renderer: " << glGetString(GL_RENDERER) << std::endl;
  std::cout << "[GL] Version: " << glGetString(GL_VERSION) << std::endl;

  g_terrain.Init(); // Initialize OpenGL resources for terrain
  checkGLError("terrain init");

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window, false);
  // GLFW input callbacks registered later via InputHandler::RegisterCallbacks
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Load fonts for high-fidelity UI
  float contentScale = 1.0f;
  {
    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    contentScale = xscale;
  }
  {
    ImFontConfig cfg;
    const char *fontPath = "external/imgui/misc/fonts/ProggyClean.ttf";
    // Check if font file exists before calling AddFontFromFileTTF
    // (imgui asserts internally on missing file before returning null)
    FILE *ftest = fopen(fontPath, "rb");
    if (ftest) {
      fclose(ftest);
      g_fontDefault = io.Fonts->AddFontFromFileTTF(fontPath, 13.0f * contentScale);
      g_fontBold = io.Fonts->AddFontFromFileTTF(fontPath, 15.0f * contentScale);
    }
    if (!g_fontDefault)
      g_fontDefault = io.Fonts->AddFontDefault(&cfg);
    if (!g_fontBold)
      g_fontBold = g_fontDefault;

    io.Fonts->Build();
  }

  // Initialize modern HUD (centered at 70% scale)
  g_hudCoords.window = window;
  g_hudCoords.SetCenteredScale(0.7f);

  std::string hudAssetPath = "../lab-studio/modern-ui/assets";
  // --- Main Render Loop ---

  // Main Loop logic continues...

  MockData hudData = MockData::CreateDK50();

  // Load Terrain for testing
  std::string data_path = g_dataPath;
  TerrainData terrainData = TerrainParser::LoadWorld(1, data_path);

  // Reconstruct TW_NOGROUND for bridge cells.
  // The .att file for this data version lacks these flags (verified: 0
  // cells). Original engine reads them from .att (ZzzLodTerrain.cpp:1665). We
  // reconstruct from bridge objects (type 80) with orientation awareness.
  {
    const int S = TerrainParser::TERRAIN_SIZE;
    int count = 0;
    for (const auto &obj : terrainData.objects) {
      if (obj.type != 80)
        continue;
      int gz = (int)(obj.position.x / 100.0f);
      int gx = (int)(obj.position.z / 100.0f);
      float angZ =
          std::abs(std::fmod(glm::degrees(obj.rotation.z) + 360.0f, 180.0f));
      bool spanAlongGZ = (std::abs(angZ - 90.0f) < 45.0f);
      // +1 buffer for bilinear neighbor coverage in shader
      int rGZ = spanAlongGZ ? 4 : 2;
      int rGX = spanAlongGZ ? 2 : 4;
      for (int dz = -rGZ; dz <= rGZ; ++dz) {
        for (int dx = -rGX; dx <= rGX; ++dx) {
          int cz = gz + dz, cx = gx + dx;
          if (cz >= 0 && cz < S && cx >= 0 && cx < S) {
            terrainData.mapping.attributes[cz * S + cx] |= 0x08;
            count++;
          }
        }
      }
    }
    // Expand TW_NOGROUND to adjacent water cells so bilinear sampling in the
    // shader never mixes unmarked water into bridge road tiles.
    std::vector<uint8_t> expanded = terrainData.mapping.attributes;
    for (int z = 0; z < S; ++z) {
      for (int x = 0; x < S; ++x) {
        if (!(terrainData.mapping.attributes[z * S + x] & 0x08))
          continue;
        for (int dz = -1; dz <= 1; ++dz) {
          for (int dx = -1; dx <= 1; ++dx) {
            int nz = z + dz, nx = x + dx;
            if (nz >= 0 && nz < S && nx >= 0 && nx < S) {
              if (terrainData.mapping.layer1[nz * S + nx] == 5)
                expanded[nz * S + nx] |= 0x08;
            }
          }
        }
      }
    }
    terrainData.mapping.attributes = expanded;

    int finalCount = 0;
    for (int idx = 0; idx < S * S; ++idx)
      if (terrainData.mapping.attributes[idx] & 0x08)
        finalCount++;
    std::cout << "[Terrain] Marked " << finalCount
              << " bridge cells as TW_NOGROUND (" << count
              << " from objects + expansion)" << std::endl;
  }

  // Make terrain data accessible for movement/height
  g_terrainDataPtr = &terrainData;
  RayPicker::Init(&terrainData, &g_camera, &g_npcManager, &g_monsterManager,
                  g_groundItems, MAX_GROUND_ITEMS);

  g_terrain.Load(terrainData, 1, data_path);
  std::cout << "Loaded Map 1 (Lorencia): " << terrainData.heightmap.size()
            << " height samples, " << terrainData.objects.size() << " objects"
            << std::endl;

  // Load world objects
  g_objectRenderer.Init();
  g_objectRenderer.SetTerrainLightmap(terrainData.lightmap);
  g_objectRenderer.SetTerrainMapping(&terrainData.mapping);
  g_objectRenderer.SetTerrainHeightmap(terrainData.heightmap);
  std::string object1_path = data_path + "/Object1";
  g_objectRenderer.LoadObjects(terrainData.objects, object1_path);
  checkGLError("object renderer load");
  std::cout << "[ObjectRenderer] Loaded " << terrainData.objects.size()
            << " object instances, " << g_objectRenderer.GetModelCount()
            << " unique models" << std::endl;
  g_grass.Init();
  g_grass.Load(terrainData, 1, data_path);
  checkGLError("grass load");

  // Initialize sky
  g_sky.Init(data_path + "/");
  checkGLError("sky init");

  // Initialize fire effects and register emitters from fire-type objects
  g_fireEffect.Init(data_path + "/Effect");
  g_vfxManager.Init(data_path);
  checkGLError("fire init");
  for (auto &inst : g_objectRenderer.GetInstances()) {
    auto &offsets = GetFireOffsets(inst.type);
    for (auto &off : offsets) {
      // Extract rotation without scale (original CreateFire only rotates, no
      // scale)
      glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
      glm::mat3 rot;
      for (int c = 0; c < 3; c++)
        rot[c] = glm::normalize(glm::vec3(inst.modelMatrix[c]));
      g_fireEffect.AddEmitter(worldPos + rot * off);
    }
  }
  std::cout << "[FireEffect] Registered " << g_fireEffect.GetEmitterCount()
            << " fire emitters" << std::endl;
  // Print fire-type objects for debugging/testing
  for (int i = 0; i < (int)terrainData.objects.size(); ++i) {
    int t = terrainData.objects[i].type;
    if (t == 50 || t == 51 || t == 52 || t == 55 || t == 80 || t == 130)
      std::cout << "  fire obj idx=" << i << " type=" << t << std::endl;
  }

  // Collect point lights from light-emitting objects
  g_pointLights.clear();
  for (auto &inst : g_objectRenderer.GetInstances()) {
    const LightTemplate *props = GetLightProperties(inst.type);
    if (!props)
      continue;
    // Extract world position from model matrix translation column
    glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
    PointLight light;
    light.position = worldPos + glm::vec3(0.0f, props->heightOffset, 0.0f);
    light.color = props->color;
    light.range = props->range;
    g_pointLights.push_back(light);
  }
  // Cap at shader maximum
  if ((int)g_pointLights.size() > MAX_POINT_LIGHTS)
    g_pointLights.resize(MAX_POINT_LIGHTS);
  std::cout << "[Lights] Collected " << g_pointLights.size()
            << " point lights from world objects" << std::endl;

  // Initialize hero character and click effect
  g_hero.Init(data_path);
  g_hero.SetTerrainData(&terrainData);

  // Starting character initialization: empty inventory for realistic testing
  // Initial stats for Level 1 DK
  g_hero.LoadStats(1, 28, 20, 25, 10, 0, 0, 110, 110, 20, 20, 1);
  g_hero.SetTerrainLightmap(terrainData.lightmap);
  g_hero.SetPointLights(g_pointLights);
  ItemModelManager::Init(g_hero.GetShader(), g_dataPath);
  g_hero.SnapToTerrain();

  g_clickEffect.Init();
  InventoryUI::LoadSlotBackgrounds(g_dataPath);

  // Initialize InventoryUI with shared state pointers
  {
    InventoryUIContext uiCtx;
    uiCtx.inventory = g_inventory;
    uiCtx.equipSlots = g_equipSlots;
    uiCtx.zen = &g_zen;
    uiCtx.syncDone = &g_syncDone;
    uiCtx.showCharInfo = &g_showCharInfo;
    uiCtx.showInventory = &g_showInventory;
    uiCtx.quickSlotDefIndex = &g_quickSlotDefIndex;
    uiCtx.potionCooldown = &g_potionCooldown;
    uiCtx.shopOpen = &g_shopOpen;
    uiCtx.shopItems = &g_shopItems;
    uiCtx.serverLevel = &g_serverLevel;
    uiCtx.serverStr = &g_serverStr;
    uiCtx.serverDex = &g_serverDex;
    uiCtx.serverVit = &g_serverVit;
    uiCtx.serverEne = &g_serverEne;
    uiCtx.serverLevelUpPoints = &g_serverLevelUpPoints;
    uiCtx.serverDefense = &g_serverDefense;
    uiCtx.serverAttackSpeed = &g_serverAttackSpeed;
    uiCtx.serverMagicSpeed = &g_serverMagicSpeed;
    uiCtx.serverHP = &g_serverHP;
    uiCtx.serverMaxHP = &g_serverMaxHP;
    uiCtx.serverMP = &g_serverMP;
    uiCtx.serverMaxMP = &g_serverMaxMP;
    uiCtx.serverXP = &g_serverXP;
    uiCtx.hero = &g_hero;
    uiCtx.server = &g_server;
    uiCtx.hudCoords = &g_hudCoords;
    uiCtx.fontDefault = g_fontDefault;
    InventoryUI::Init(uiCtx);
  }

  g_clickEffect.LoadAssets(data_path);
  g_clickEffect.SetTerrainData(&terrainData);
  checkGLError("hero init");

  // Initialize input handler with shared game state
  {
    InputContext inputCtx;
    inputCtx.hero = &g_hero;
    inputCtx.camera = &g_camera;
    inputCtx.clickEffect = &g_clickEffect;
    inputCtx.server = &g_server;
    inputCtx.monsterMgr = &g_monsterManager;
    inputCtx.npcMgr = &g_npcManager;
    inputCtx.groundItems = g_groundItems;
    inputCtx.maxGroundItems = MAX_GROUND_ITEMS;
    inputCtx.hudCoords = &g_hudCoords;
    inputCtx.showCharInfo = &g_showCharInfo;
    inputCtx.showInventory = &g_showInventory;
    inputCtx.hoveredNpc = &g_hoveredNpc;
    inputCtx.hoveredMonster = &g_hoveredMonster;
    inputCtx.hoveredGroundItem = &g_hoveredGroundItem;
    inputCtx.selectedNpc = &g_selectedNpc;
    inputCtx.quickSlotDefIndex = &g_quickSlotDefIndex;
    inputCtx.shopOpen = &g_shopOpen;
    InputHandler::Init(inputCtx);
    InputHandler::RegisterCallbacks(window);
  }

  // Connect to server via persistent ServerConnection
  g_npcManager.SetTerrainData(&terrainData);
  ServerData serverData;

  // Initialize ClientPacketHandler with game state context
  {
    static ClientGameState gameState;
    gameState.hero = &g_hero;
    gameState.monsterManager = &g_monsterManager;
    gameState.vfxManager = &g_vfxManager;
    gameState.terrain = &g_terrain;
    gameState.inventory = g_inventory;
    gameState.equipSlots = g_equipSlots;
    gameState.groundItems = g_groundItems;
    gameState.itemDefs = &g_itemDefs;
    gameState.zen = &g_zen;
    gameState.syncDone = &g_syncDone;
    gameState.shopOpen = &g_shopOpen;
    gameState.shopItems = &g_shopItems;
    gameState.serverLevel = &g_serverLevel;
    gameState.serverHP = &g_serverHP;
    gameState.serverMaxHP = &g_serverMaxHP;
    gameState.serverMP = &g_serverMP;
    gameState.serverMaxMP = &g_serverMaxMP;
    gameState.serverStr = &g_serverStr;
    gameState.serverDex = &g_serverDex;
    gameState.serverVit = &g_serverVit;
    gameState.serverEne = &g_serverEne;
    gameState.serverLevelUpPoints = &g_serverLevelUpPoints;
    gameState.serverXP = &g_serverXP;
    gameState.serverDefense = &g_serverDefense;
    gameState.serverAttackSpeed = &g_serverAttackSpeed;
    gameState.serverMagicSpeed = &g_serverMagicSpeed;
    gameState.quickSlotDefIndex = &g_quickSlotDefIndex;
    gameState.spawnDamageNumber = [](const glm::vec3 &pos, int dmg,
                                     uint8_t type) {
      FloatingDamageRenderer::Spawn(pos, dmg, type, g_floatingDmg,
                                    MAX_FLOATING_DAMAGE);
    };
    gameState.getBodyPartIndex = ItemDatabase::GetBodyPartIndex;
    gameState.getBodyPartModelFile = ItemDatabase::GetBodyPartModelFile;
    gameState.getItemRestingAngle = [](int16_t defIdx, glm::vec3 &angle,
                                       float &scale) {
      GroundItemRenderer::GetItemRestingAngle(defIdx, angle, scale);
    };
    ClientPacketHandler::Init(&gameState);
  }

  // Set up packet handler BEFORE connecting so no packets are lost
  g_server.onPacket = [&serverData](const uint8_t *pkt, int size) {
    if (size >= 3) {
      std::cout << "[Net:Initial] Received packet type=0x" << std::hex
                << (int)pkt[0] << std::dec << " size=" << size << std::endl;
    }
    ClientPacketHandler::HandleInitialPacket(pkt, size, serverData);
  };

  // Auto-diagnostic mode: --diag flag captures all debug views and exits
  bool autoDiag = false;
  bool autoScreenshot = false;
  bool autoGif = false;
  int gifFrameCount = 72; // ~3 seconds at 24fps
  int gifDelay = 4;       // centiseconds between frames (4cs = 25fps)
  int objectDebugIdx = -1;
  std::string objectDebugName;
  bool hasCustomPos = false;
  float customX = 0, customY = 0, customZ = 0;
  std::string outputName;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--diag")
      autoDiag = true;
    if (std::string(argv[i]) == "--screenshot")
      autoScreenshot = true;
    if (std::string(argv[i]) == "--debug" && i + 1 < argc) {
      g_terrain.SetDebugMode(std::atoi(argv[i + 1]));
      ++i;
    }
    if (std::string(argv[i]) == "--gif")
      autoGif = true;
    if (std::string(argv[i]) == "--gif-frames" && i + 1 < argc) {
      gifFrameCount = std::atoi(argv[i + 1]);
      ++i;
    }
    if (std::string(argv[i]) == "--pos" && i + 3 < argc) {
      customX = std::atof(argv[i + 1]);
      customY = std::atof(argv[i + 2]);
      customZ = std::atof(argv[i + 3]);
      hasCustomPos = true;
      i += 3;
    }
    if (std::string(argv[i]) == "--output" && i + 1 < argc) {
      outputName = argv[i + 1];
      ++i;
    }
    if (std::string(argv[i]) == "--object-debug" && i + 1 < argc) {
      objectDebugIdx = std::atoi(argv[i + 1]);
      ++i;
    }
  }

  bool connected = false;
  for (int i = 0; i < 5; ++i) {
    if (g_server.Connect("127.0.0.1", 44405)) {
      connected = true;
      break;
    }
    std::cout << "[Net] Retrying connection in 1s..." << std::endl;
    sleep(1);
  }

  if (!connected) {
    std::cerr << "[Net] FATAL: Could not connect to MU Server. Ensure the "
                 "server is running at 127.0.0.1:44405."
              << std::endl;
    return 1;
  }

  serverData.connected = true;

  // Receive initial data burst (welcome + NPCs + monsters + equipment +
  // stats) Give server time to send all initial packets, poll to parse them
  std::cout << "[Net] Connected. Syncing initial state..." << std::endl;
  int packetsReceived = 0;
  for (int attempt = 0; attempt < 100; attempt++) {
    g_server.Poll();
    usleep(20000); // 20ms
  }

  if (serverData.npcs.empty() && !autoScreenshot && !autoDiag) {
    // If we didn't get initial sync data (e.g. timeout), it's probably a
    // stale connection
    std::cerr << "[Net] FATAL: Server connected but failed to sync initial "
                 "game state."
              << std::endl;
    return 1;
  }

  // Switch to ongoing packet handler for game loop
  g_server.onPacket = [](const uint8_t *pkt, int size) {
    ClientPacketHandler::HandleGamePacket(pkt, size);
  };

  if (serverData.connected && !serverData.npcs.empty()) {
    g_npcManager.InitModels(data_path);
    for (auto &npc : serverData.npcs) {
      g_npcManager.AddNpcByType(npc.type, npc.gridX, npc.gridY, npc.dir);
    }
    std::cout << "[NPC] Loaded " << serverData.npcs.size()
              << " NPCs from server" << std::endl;
  } else {
    std::cout << "[NPC] No server connection, using hardcoded NPCs"
              << std::endl;
    g_npcManager.Init(data_path);
  }

  // Equip weapon + shield + armor from server equipment data (DB-driven)
  for (auto &eq : serverData.equipment) {
    if (eq.slot == 0) {
      g_hero.EquipWeapon(eq.info); // Right hand weapon
    } else if (eq.slot == 1) {
      g_hero.EquipShield(eq.info); // Left hand shield
    }
    // Body part equipment (slot 2=Helm, 3=Armor, 4=Pants, 5=Gloves, 6=Boots)
    int bodyPart = ItemDatabase::GetBodyPartIndex(eq.info.category);
    if (bodyPart >= 0) {
      std::string partModel = ItemDatabase::GetBodyPartModelFile(
          eq.info.category, eq.info.itemIndex);
      if (!partModel.empty())
        g_hero.EquipBodyPart(bodyPart, partModel);
    }
    std::cout << "[Equip] Slot " << (int)eq.slot << ": " << eq.info.modelFile
              << " cat=" << (int)eq.info.category << std::endl;
  }
  g_syncDone = true; // Initial sync complete, allow updates
  g_npcManager.SetTerrainLightmap(terrainData.lightmap);
  InventoryUI::RecalcEquipmentStats(); // Compute initial weapon/defense bonuses
  g_npcManager.SetPointLights(g_pointLights);
  checkGLError("npc init");

  // Initialize monster manager and spawn monsters from server data
  g_monsterManager.InitModels(data_path);
  g_monsterManager.SetTerrainData(&terrainData);
  g_monsterManager.SetTerrainLightmap(terrainData.lightmap);
  g_monsterManager.SetPointLights(g_pointLights);
  if (!serverData.monsters.empty()) {
    for (auto &mon : serverData.monsters) {
      g_monsterManager.AddMonster(mon.monsterType, mon.gridX, mon.gridY,
                                  mon.dir, mon.serverIndex);
    }
    std::cout << "[Monster] Spawned " << serverData.monsters.size()
              << " monsters from server" << std::endl;
  }
  checkGLError("monster init");

  // Load saved camera state (persists position/angle/zoom across restarts)
  g_camera.LoadState("camera_save.txt");

  // Sync hero position from loaded camera state
  g_hero.SetPosition(g_camera.GetPosition());
  g_hero.SnapToTerrain();

  // Fix: if hero spawned on a non-walkable tile, move to a known safe
  // position
  {
    glm::vec3 heroPos = g_hero.GetPosition();
    const int S = TerrainParser::TERRAIN_SIZE;
    int gz = (int)(heroPos.x / 100.0f);
    int gx = (int)(heroPos.z / 100.0f);
    bool walkable = (gx >= 0 && gz >= 0 && gx < S && gz < S) &&
                    (terrainData.mapping.attributes[gz * S + gx] & 0x04) == 0;
    if (!walkable) {
      std::cout << "[Hero] Spawn position non-walkable (attr=0x" << std::hex
                << (int)terrainData.mapping.attributes[gz * S + gx] << std::dec
                << "), searching for walkable tile..." << std::endl;
      // Spiral search from Lorencia town center for nearest walkable tile
      int startGX = 125, startGZ = 135;
      bool found = false;
      for (int radius = 0; radius < 30 && !found; radius++) {
        for (int dy = -radius; dy <= radius && !found; dy++) {
          for (int dx = -radius; dx <= radius && !found; dx++) {
            if (radius > 0 && std::abs(dx) != radius && std::abs(dy) != radius)
              continue;
            int cx = startGX + dx, cz = startGZ + dy;
            if (cx < 1 || cz < 1 || cx >= S - 1 || cz >= S - 1)
              continue;
            uint8_t attr = terrainData.mapping.attributes[cz * S + cx];
            if ((attr & 0x04) == 0 && (attr & 0x08) == 0) {
              float wx = (float)cz * 100.0f;
              float wz = (float)cx * 100.0f;
              std::cout << "[Hero] Found walkable tile at grid (" << cx << ","
                        << cz << ") attr=0x" << std::hex << (int)attr
                        << std::dec << std::endl;
              g_hero.SetPosition(glm::vec3(wx, 0.0f, wz));
              g_hero.SnapToTerrain();
              found = true;
            }
          }
        }
      }
      if (!found) {
        std::cout << "[Hero] WARNING: No walkable tile found nearby"
                  << std::endl;
        g_hero.SetPosition(glm::vec3(13000.0f, 0.0f, 13000.0f));
        g_hero.SnapToTerrain();
      }
    }
  }
  g_camera.SetPosition(g_hero.GetPosition());

  if (autoDiag) {
    // Diag mode no longer forces top-down view to respect user's "only default"
    // request
  }
  if ((autoScreenshot || autoGif) && !hasCustomPos) {
    // Lorencia town center at original MU isometric angle (default for
    // captures)
    g_camera.SetPosition(glm::vec3(13000.0f, 350.0f, 13500.0f));
  }
  // --pos X Y Z: override camera position with exact coordinates
  if (hasCustomPos) {
    g_hero.SetPosition(glm::vec3(customX, customY, customZ));
    g_hero.SnapToTerrain();
    g_camera.SetPosition(g_hero.GetPosition());
    std::cout << "[camera] Position set to (" << customX << ", " << customY
              << ", " << customZ << ")" << std::endl;
  }
  // --object-debug <index>: position camera to look at a specific object
  if (objectDebugIdx >= 0 && objectDebugIdx < (int)terrainData.objects.size()) {
    auto &debugObj = terrainData.objects[objectDebugIdx];
    // Position camera offset from the object, looking at it
    glm::vec3 objPos = debugObj.position;
    // Check for --topdown flag for bird's eye view
    bool topDown = false;
    for (int ii = 1; ii < argc; ++ii) {
      if (std::string(argv[ii]) == "--topdown")
        topDown = true;
    }
    if (topDown) {
      // Disabled to force "only default" view
      g_hero.SetPosition(objPos);
      g_hero.SnapToTerrain();
      g_camera.SetPosition(g_hero.GetPosition());
    } else {
      // Position camera at the object using the fixed isometric angle
      g_hero.SetPosition(objPos);
      g_hero.SnapToTerrain();
      g_camera.SetPosition(g_hero.GetPosition());
    }
    objectDebugName = "obj_type" + std::to_string(debugObj.type) + "_idx" +
                      std::to_string(objectDebugIdx);
    if (autoGif)
      ; // keep autoGif, skip autoScreenshot
    else
      autoScreenshot = true;
    std::cout << "[object-debug] Targeting object " << objectDebugIdx
              << " type=" << debugObj.type << " at gl_pos=(" << objPos.x << ", "
              << objPos.y << ", " << objPos.z << ")" << std::endl;
  }
  int diagFrame = 0;
  const char *diagNames[] = {"normal", "tileindex", "tileuv",
                             "alpha",  "lightmap",  "nolightmap"};

  // Pass point lights to renderers
  {
    std::vector<glm::vec3> lightPos, lightCol;
    std::vector<float> lightRange;
    for (auto &pl : g_pointLights) {
      lightPos.push_back(pl.position);
      lightCol.push_back(pl.color);
      lightRange.push_back(pl.range);
    }
    g_objectRenderer.SetPointLights(lightPos, lightCol, lightRange);
    g_terrain.SetPointLights(lightPos, lightCol, lightRange);
  }

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  ImVec4 clear_color = ImVec4(
      0.0f, 0.0f, 0.0f, 1.00f); // Black: matches edge fog at map boundaries
  float lastFrame = 0.0f;
  while (!glfwWindowShouldClose(window)) {
    float currentFrame = glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    glfwPollEvents();
    InputHandler::ProcessInput(window, deltaTime);
    g_camera.Update(deltaTime);

    // Poll persistent network connection for server packets
    g_server.Poll();
    g_server.Flush();

    // Send player position to server periodically (~4Hz)
    {
      // Tick potion cooldown
      if (g_potionCooldown > 0.0f)
        g_potionCooldown = std::max(0.0f, g_potionCooldown - deltaTime);

      static float posTimer = 0.0f;
      static int lastGridX = -1, lastGridY = -1;
      posTimer += deltaTime;
      if (posTimer >= 0.25f) {
        posTimer = 0.0f;
        glm::vec3 hp = g_hero.GetPosition();
        g_server.SendPrecisePosition(hp.x, hp.z);

        // Also send grid move when grid cell changes (for DB persistence)
        int gx = (int)(hp.z / 100.0f);
        int gy = (int)(hp.x / 100.0f);
        if (gx != lastGridX || gy != lastGridY) {
          g_server.SendGridMove((uint8_t)gx, (uint8_t)gy);
          lastGridX = gx;
          lastGridY = gy;
        }
      }
    }

    // Update monster manager (state machines, animation)
    g_monsterManager.SetPlayerPosition(g_hero.GetPosition());
    g_monsterManager.SetPlayerDead(g_hero.IsDead());
    g_monsterManager.Update(deltaTime);

    // Hero combat: update attack state machine, send attack packet on hit
    // Block all combat in safe zone — but don't stop movement
    {
      bool nowInSafe = g_hero.IsInSafeZone();
      static bool wasInSafe = false;
      if (nowInSafe) {
        // On transition INTO safe zone: cancel any active attack once
        if (!wasInSafe &&
            (g_hero.GetAttackTarget() >= 0 || g_hero.IsAttacking())) {
          g_hero.CancelAttack();
        }
        // Don't update attack/state while in safe zone
      } else {
        g_hero.UpdateAttack(deltaTime);
        g_hero.UpdateState(deltaTime);
        if (g_hero.CheckAttackHit()) {
          int targetIdx = g_hero.GetAttackTarget();
          if (targetIdx >= 0 &&
              targetIdx < g_monsterManager.GetMonsterCount()) {
            uint16_t serverIdx = g_monsterManager.GetServerIndex(targetIdx);
            g_server.SendAttack(serverIdx);
          }
        }
        // Auto-attack: re-engage after cooldown if target still alive
        if (g_hero.GetAttackState() == AttackState::NONE &&
            g_hero.GetAttackTarget() >= 0) {
          int targetIdx = g_hero.GetAttackTarget();
          if (targetIdx < g_monsterManager.GetMonsterCount()) {
            MonsterInfo mi = g_monsterManager.GetMonsterInfo(targetIdx);
            if (mi.state != MonsterState::DYING &&
                mi.state != MonsterState::DEAD && mi.hp > 0) {
              g_hero.AttackMonster(targetIdx, mi.position);
            }
          }
        }
      }
      wasInSafe = nowInSafe;
    }

    // Hero respawn: after death timer expires, respawn in Lorencia safe zone
    if (g_hero.ReadyToRespawn()) {
      // Find walkable safe zone tile (same spiral search as init)
      const int S = TerrainParser::TERRAIN_SIZE;
      int startGX = 125, startGZ = 125;
      glm::vec3 spawnPos(12500.0f, 0.0f, 12500.0f);
      for (int radius = 0; radius < 30; radius++) {
        bool found = false;
        for (int dy = -radius; dy <= radius && !found; dy++) {
          for (int dx = -radius; dx <= radius && !found; dx++) {
            if (radius > 0 && std::abs(dx) != radius && std::abs(dy) != radius)
              continue;
            int cx = startGX + dx, cz = startGZ + dy;
            if (cx < 1 || cz < 1 || cx >= S - 1 || cz >= S - 1)
              continue;
            uint8_t attr = g_terrainDataPtr->mapping.attributes[cz * S + cx];
            if ((attr & 0x04) == 0 && (attr & 0x08) == 0) {
              spawnPos =
                  glm::vec3((float)cz * 100.0f, 0.0f, (float)cx * 100.0f);
              found = true;
            }
          }
        }
        if (found)
          break;
      }
      g_hero.Respawn(spawnPos);
      g_hero.SnapToTerrain();
      g_camera.SetPosition(g_hero.GetPosition());
      g_serverHP = g_serverMaxHP; // Reset HUD HP

      // Notify server that player is alive (clears session.dead)
      g_server.SendCharSave(1, (uint16_t)g_serverLevel, (uint16_t)g_serverStr,
                            (uint16_t)g_serverDex, (uint16_t)g_serverVit,
                            (uint16_t)g_serverEne, (uint16_t)g_serverMaxHP,
                            (uint16_t)g_serverMaxHP,
                            (uint16_t)g_serverLevelUpPoints,
                            (uint64_t)g_serverXP, g_quickSlotDefIndex);
    }

    // Auto-pickup: walk near a ground item to pick it up
    {
      glm::vec3 heroPos = g_hero.GetPosition();
      for (auto &gi : g_groundItems) {
        if (!gi.active)
          continue;
        gi.timer += deltaTime;
        // Snap drop Y to terrain
        if (gi.position.y == 0.0f && g_terrainDataPtr) {
          float gx = gi.position.z / 100.0f;
          float gz = gi.position.x / 100.0f;
          int ix = (int)gx, iz = (int)gz;
          if (ix >= 0 && iz >= 0 && ix < 256 && iz < 256) {
            float h = g_terrainDataPtr->heightmap[iz * 256 + ix] * 1.5f;
            gi.position.y = h + 0.5f;
          }
        }
        float dist = glm::length(
            glm::vec3(heroPos.x - gi.position.x, 0, heroPos.z - gi.position.z));
        // Auto-pickup Zen only (items require explicit click)
        if (gi.defIndex == -1 && dist < 120.0f && !g_hero.IsDead()) {
          g_server.SendPickup(gi.dropIndex);
          gi.active = false; // Optimistic remove
        }
        // Despawn after 60s
        if (gi.timer > 60.0f)
          gi.active = false;
      }
    }

    // Roof hiding: read layer1 tile at hero position, fade types 125/126
    if (g_terrainDataPtr) {
      glm::vec3 heroPos = g_hero.GetPosition();
      const int S = TerrainParser::TERRAIN_SIZE;
      int gz = (int)(heroPos.x / 100.0f);
      int gx = (int)(heroPos.z / 100.0f);
      uint8_t heroTile = 0;
      if (gx >= 0 && gz >= 0 && gx < S && gz < S)
        heroTile = g_terrainDataPtr->mapping.layer1[gz * S + gx];
      // Original: HeroTile == 4 hides roof meshes
      float target = (heroTile == 4) ? 0.0f : 1.0f;
      g_typeAlphaTarget[125] = target;
      g_typeAlphaTarget[126] = target;
      // Fast fade — nearly instant (95%+ in 1-2 frames)
      float blend = 1.0f - std::exp(-20.0f * deltaTime);
      for (auto &[type, alpha] : g_typeAlpha) {
        alpha += (g_typeAlphaTarget[type] - alpha) * blend;
      }
      g_objectRenderer.SetTypeAlpha(g_typeAlpha);

      // SafeZone detection: attribute 0x01 = TW_SAFEZONE
      uint8_t heroAttr = 0;
      if (gx >= 0 && gz >= 0 && gx < S && gz < S)
        heroAttr = g_terrainDataPtr->mapping.attributes[gz * S + gx];
      g_hero.SetInSafeZone((heroAttr & 0x01) != 0 || (heroAttr & 0x08) != 0);
    }

    // Auto-screenshot/diagnostic camera override
    if ((autoScreenshot || autoDiag) && diagFrame == 60) {
      glm::vec3 hPos = g_hero.GetPosition();
      std::cout << "[Screenshot] Overriding camera to hero at (" << hPos.x
                << ", " << hPos.y << ", " << hPos.z << ") for capture."
                << std::endl;
      g_camera.SetPosition(hPos);
    }

    if (autoDiag && diagFrame >= 2) {
      int mode = (diagFrame - 2) / 2;
      if (mode < 6 && (diagFrame - 2) % 2 == 0) {
        g_terrain.SetDebugMode(mode);
      }
    }

    // Use framebuffer size for viewport (Retina displays are 2x window size)
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);

    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);
    glm::mat4 projection =
        g_camera.GetProjectionMatrix((float)winW, (float)winH);
    glm::mat4 view = g_camera.GetViewMatrix();
    glm::vec3 camPos = g_camera.GetPosition();

    // Sky renders first (behind everything, no depth write)
    g_sky.Render(view, projection, camPos);

    g_terrain.Render(view, projection, currentFrame, camPos);

    // Render world objects first (before grass, so tall grass billboards
    // don't block thin fence bar meshes via depth buffer)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_objectRenderer.Render(view, projection, g_camera.GetPosition(),
                            currentFrame);

    // Render grass billboards (after objects so grass doesn't occlude fences)
    {
      std::vector<GrassRenderer::PushSource> pushSources;
      pushSources.push_back({g_hero.GetPosition(), 100.0f});
      g_grass.Render(view, projection, currentFrame, camPos, pushSources);
    }

    // Update effects (VFX rendered after characters for correct layering)
    g_fireEffect.Update(deltaTime);
    g_vfxManager.Update(deltaTime);
    g_fireEffect.Render(view, projection);

    // Render NPC characters with shadows
    g_npcManager.RenderShadows(view, projection);
    g_npcManager.Render(view, projection, camPos, deltaTime);

    // Render NPC selection outline (green glow on hover)
    // if (g_hoveredNpc >= 0)
    //   g_npcManager.RenderOutline(g_hoveredNpc, view, projection);
    // Simplified hover UI: handled by RenderLabels background highlight

    // Render monsters with shadows
    g_monsterManager.RenderShadows(view, projection);
    g_monsterManager.Render(view, projection, camPos, deltaTime);

    // Render hero character, shadow, and click effect (after all world
    // geometry)
    g_clickEffect.Render(view, projection, deltaTime, g_hero.GetShader());
    g_hero.Render(view, projection, camPos, deltaTime);
    g_hero.RenderShadow(view, projection);

    // Render VFX (after all characters so particles layer on top)
    g_vfxManager.Render(view, projection);

    // Auto-GIF: capture with warmup for fire particle buildup
    // Capture BEFORE ImGui rendering so debug overlay is not in the output
    if (autoGif && !Screenshot::IsRecording() && diagFrame == 0) {
      std::string gifPath =
          !outputName.empty()
              ? "screenshots/" + outputName + ".gif"
              : (objectDebugName.empty()
                     ? "screenshots/fire_effect.gif"
                     : "screenshots/" + objectDebugName + ".gif");
      Screenshot::StartRecording(window, gifPath, gifFrameCount, gifDelay);
      std::cout << "[GIF] Starting capture (" << gifFrameCount << " frames)"
                << std::endl;
    }
    if (Screenshot::TickRecording(window)) {
      break; // GIF saved, exit
    }

    // Auto-screenshot flag (capture happens after ImGui render to include
    // HUD)
    bool captureScreenshot = (autoScreenshot && diagFrame == 60);

    // Start the Dear ImGui frame
    InventoryUI::ClearRenderQueue();
    InventoryUI::ResetPendingTooltip(); // Reset deferred tooltip each frame
    ImGui_ImplOpenGL3_NewFrame();

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Simplified ImGui HUD
    {
      ImGuiViewport *vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - 50),
                              ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(vp->Size.x, 50), ImGuiCond_Always);

      ImGuiWindowFlags HUD_FLAGS =
          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
          ImGuiWindowFlags_NoBackground;

      if (ImGui::Begin("SimpleHUD", nullptr, HUD_FLAGS)) {

        // HP bar
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
        {
          int curHP = g_hero.GetHP();
          int maxHP = g_hero.GetMaxHP();
          float hpFrac = maxHP > 0 ? (float)curHP / (float)maxHP : 0.0f;
          hpFrac = std::clamp(hpFrac, 0.0f, 1.0f);
          char hpLabel[32];
          snprintf(hpLabel, sizeof(hpLabel), "HP %d/%d", curHP, maxHP);
          ImGui::ProgressBar(hpFrac, ImVec2(180, 20), hpLabel);
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // MP bar
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              ImVec4(0.2f, 0.3f, 0.9f, 1.0f));
        {
          int curMP = g_hero.GetMana();
          int maxMP = g_hero.GetMaxMana();
          float mpFrac = maxMP > 0 ? (float)curMP / (float)maxMP : 0.0f;
          mpFrac = std::clamp(mpFrac, 0.0f, 1.0f);
          char mpLabel[32];
          snprintf(mpLabel, sizeof(mpLabel), "MP %d/%d", curMP, maxMP);
          ImGui::ProgressBar(mpFrac, ImVec2(120, 20), mpLabel);
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // Level
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Lv.%d",
                           g_serverLevel);
        ImGui::SameLine();

        // XP Bar (consistent source of truth from g_hero)
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              ImVec4(0.2f, 0.7f, 0.9f, 1.0f));
        {
          uint64_t curXp = g_hero.GetExperience();
          int curLv = g_hero.GetLevel();
          uint64_t nextXp = g_hero.GetNextExperience();
          uint64_t prevXp = g_hero.CalcXPForLevel(curLv);

          float xpFrac = 0.0f;
          if (nextXp > prevXp)
            xpFrac = (float)(curXp - prevXp) / (float)(nextXp - prevXp);
          xpFrac = std::clamp(xpFrac, 0.0f, 1.0f);

          char xpLabel[64];
          snprintf(xpLabel, sizeof(xpLabel), "XP %llu/%llu (%.1f%%)",
                   (unsigned long long)(curXp - prevXp),
                   (unsigned long long)(nextXp - prevXp), xpFrac * 100.0f);
          ImGui::ProgressBar(xpFrac, ImVec2(220, 20), xpLabel);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(vp->Size.x - 220);

        // Buttons
        if (ImGui::Button("Char (C)", ImVec2(100, 30))) {
          g_showCharInfo = !g_showCharInfo;
        }
        ImGui::SameLine();
        if (ImGui::Button("Inv (I)", ImVec2(100, 30))) {
          g_showInventory = !g_showInventory;
        }

        // Quick Slot (Q)
        ImGui::SameLine(vp->Size.x * 0.5f - 25);
        ImGui::BeginGroup();
        {
          ImVec2 qPos = ImGui::GetCursorScreenPos();
          ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 20, 30, 200));
          ImGui::BeginChild("QuickSlotQ", ImVec2(50, 50), true,
                            ImGuiWindowFlags_NoScrollbar);

          int itemCount = 0;
          if (g_quickSlotDefIndex != -1) {
            for (int i = 0; i < INVENTORY_SLOTS; i++) {
              if (g_inventory[i].occupied && g_inventory[i].primary &&
                  g_inventory[i].defIndex == g_quickSlotDefIndex) {
                itemCount += g_inventory[i].quantity;
              }
            }

            // Queue 3D render for quick slot icon
            auto it = g_itemDefs.find(g_quickSlotDefIndex);
            if (it != g_itemDefs.end() && itemCount > 0) {
              g_quickSlotPos = qPos; // Capture for post-3D overlay
              int qwinH = (int)ImGui::GetIO().DisplaySize.y;
              InventoryUI::AddRenderJob(
                  {it->second.modelFile, g_quickSlotDefIndex, (int)qPos.x + 5,
                   qwinH - (int)qPos.y - 45, 40, 40, false});

              // --- Cooldown Overlay ---
              if (false && g_potionCooldown > 0.0f) {
                ImVec2 p0 = qPos;
                ImVec2 p1 = ImVec2(qPos.x + 50, qPos.y + 50);

                // Dark semi-transparent overlay
                ImGui::GetForegroundDrawList()->AddRectFilled(
                    p0, p1, IM_COL32(20, 20, 20, 180));

                // Countdown text (bright white) - Show integer seconds
                char cdBuf[16];
                snprintf(cdBuf, sizeof(cdBuf), "%d",
                         (int)ceil(g_potionCooldown));
                ImVec2 txtSize = ImGui::CalcTextSize(cdBuf);
                ImGui::GetForegroundDrawList()->AddText(
                    ImVec2(p0.x + (50 - txtSize.x) * 0.5f,
                           p0.y + (50 - txtSize.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), cdBuf);
              }
            }
          }

          ImGui::EndChild();
          ImGui::PopStyleColor();

          // Overlay "Q" and count
          ImDrawList *foreground = ImGui::GetForegroundDrawList();
          foreground->AddText(ImVec2(qPos.x + 3, qPos.y + 2),
                              IM_COL32(255, 255, 255, 200), "Q");
          if (g_quickSlotDefIndex != -1 && itemCount > 0) {
            char cbuf[16];
            snprintf(cbuf, sizeof(cbuf), "%d", itemCount);
            ImVec2 tsz = ImGui::CalcTextSize(cbuf);
            foreground->AddText(ImVec2(qPos.x + 47 - tsz.x, qPos.y + 32),
                                IM_COL32(255, 210, 80, 255), cbuf);
          }
        }
        ImGui::EndGroup();
      }
      ImGui::End();

      ImDrawList *dl = ImGui::GetForegroundDrawList();

      // ── Floating damage numbers ──
      FloatingDamageRenderer::UpdateAndRender(
          g_floatingDmg, MAX_FLOATING_DAMAGE, deltaTime, dl, g_fontDefault,
          view, projection, winW, winH);

      // ── Monster nameplates ──
      g_monsterManager.RenderNameplates(dl, g_fontDefault, view, projection,
                                        winW, winH, camPos, g_hoveredMonster);

      // ── Ground item 3D models + physics ──
      GroundItemRenderer::RenderModels(
          g_groundItems, MAX_GROUND_ITEMS, deltaTime, view, projection,
          [](float x, float z) -> float { return g_terrain.GetHeight(x, z); });

      // ── Ground item labels + tooltips ──
      GroundItemRenderer::RenderLabels(
          g_groundItems, MAX_GROUND_ITEMS, dl, g_fontDefault, view, projection,
          winW, winH, camPos, g_hoveredGroundItem, g_itemDefs);
    }

    // Add some debug info
    ImGui::Begin("Terrain Debug");
    ImGui::Text("Camera Pos: %.1f, %.1f, %.1f", camPos.x, camPos.y, camPos.z);
    ImGui::Text("Camera Zoom: %.1f (Default: 800.0)", g_camera.GetZoom());
    ImGui::Text("Objects: %d instances, %d models",
                g_objectRenderer.GetInstanceCount(),
                g_objectRenderer.GetModelCount());
    ImGui::Text("Fire: %d emitters, %d particles",
                g_fireEffect.GetEmitterCount(),
                g_fireEffect.GetParticleCount());

    static int debugMode = 0;
    const char *debugModes[] = {"Normal",     "Tile Index", "Tile UV",
                                "Alpha",      "Lightmap",   "No Lightmap",
                                "Layer1 Only"};
    if (ImGui::Combo("Debug View", &debugMode, debugModes, 7)) {
      g_terrain.SetDebugMode(debugMode);
    }
    ImGui::End();

    // Hero coordinate overlay (top-left)
    {
      glm::vec3 hPos = g_hero.GetPosition();
      ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.5f);
      ImGui::Begin("##HeroCoords", nullptr,
                   ImGuiWindowFlags_NoDecoration |
                       ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoFocusOnAppearing |
                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
      float muX = hPos.z / 100.0f;
      float muY = hPos.x / 100.0f;
      ImGui::Text("World: %.0f, %.0f, %.0f", hPos.x, hPos.y, hPos.z);
      ImGui::Text("Grid:  %.1f, %.1f", muX, muY);
      ImGui::Text("Height: %.1f", hPos.y);
      ImGui::Text("State: %s", g_hero.IsMoving() ? "Walking" : "Idle");
      if (g_terrainDataPtr) {
        const int S = TerrainParser::TERRAIN_SIZE;
        int gz = (int)(hPos.x / 100.0f);
        int gx = (int)(hPos.z / 100.0f);
        if (gx >= 0 && gz >= 0 && gx < S && gz < S) {
          uint8_t attr = g_terrainDataPtr->mapping.attributes[gz * S + gx];
          ImGui::Text(
              "Attr: 0x%02X%s%s%s%s%s", attr, (attr & 0x01) ? " SAFE" : "",
              (attr & 0x04) ? " NOMOVE" : "", (attr & 0x08) ? " NOGROUND" : "",
              (attr & 0x10) ? " WATER" : "", (attr & 0x20) ? " ACTION" : "");
          uint8_t tile = g_terrainDataPtr->mapping.layer1[gz * S + gx];
          ImGui::Text("Tile: %d%s", tile, (tile == 4) ? " (ROOF HIDE)" : "");
          ImGui::Text("Roof: %.0f%%", g_typeAlpha[125] * 100.0f);
        }
      }
      ImGui::End();
    }

    // NPC name labels
    g_npcManager.RenderLabels(ImGui::GetForegroundDrawList(), view, projection,
                              winW, winH, camPos, g_hoveredNpc);

    // NPC click interaction dialog has been replaced with direct shop opening
    // through InputHandler.cpp (SendShopOpen). Optionally we could keep
    // g_selectedNpc for highlighting purposes without rendering a dialog.
    if (g_selectedNpc >= 0) {
      // Close selection on Escape
      if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        g_selectedNpc = -1;
    }

    // ── Character Info and Inventory panels ──
    ImDrawList *panelDl = ImGui::GetForegroundDrawList();
    if (g_shopOpen)
      InventoryUI::RenderShopPanel(panelDl, g_hudCoords);
    if (g_showCharInfo)
      InventoryUI::RenderCharInfoPanel(panelDl, g_hudCoords);
    if (g_showInventory || g_shopOpen) {
      bool wasShowInvent = g_showInventory;
      g_showInventory = true; // force flag for proper layout parsing
      InventoryUI::RenderInventoryPanel(panelDl, g_hudCoords);
      g_showInventory = wasShowInvent;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Flatten render queue (items on top of UI)
    // Scale logical pixel coords to physical framebuffer pixels (HiDPI/Retina
    // fix)
    {
      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      float scaleX = (float)fbW / ImGui::GetIO().DisplaySize.x;
      float scaleY = (float)fbH / ImGui::GetIO().DisplaySize.y;
      for (const auto &job : InventoryUI::GetRenderQueue()) {
        int px = (int)(job.x * scaleX);
        int py = (int)(job.y * scaleY);
        int pw = (int)(job.w * scaleX);
        int ph = (int)(job.h * scaleY);
        ItemModelManager::RenderItemUI(job.modelFile, job.defIndex, px, py, pw,
                                       ph, job.hovered);
      }
    }

    // Second ImGui pass: draw deferred tooltip and HUD overlays ON TOP of 3D
    // items
    if (InventoryUI::HasPendingTooltip() || g_potionCooldown > 0.0f ||
        InventoryUI::HasDeferredOverlays()) {
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      if (g_potionCooldown > 0.0f && g_quickSlotPos.x > 0) {
        ImVec2 p0 = g_quickSlotPos;
        ImVec2 p1 = ImVec2(p0.x + 50, p0.y + 50);

        // Dark semi-transparent overlay
        ImGui::GetForegroundDrawList()->AddRectFilled(
            p0, p1, IM_COL32(20, 20, 20, 180));

        // Countdown text (bright white)
        char cdBuf[16];
        snprintf(cdBuf, sizeof(cdBuf), "%d", (int)ceil(g_potionCooldown));
        ImVec2 txtSize = ImGui::CalcTextSize(cdBuf);
        ImGui::GetForegroundDrawList()->AddText(
            ImVec2(p0.x + (50 - txtSize.x) * 0.5f,
                   p0.y + (50 - txtSize.y) * 0.5f),
            IM_COL32(255, 255, 255, 255), cdBuf);
      }

      if (InventoryUI::HasDeferredOverlays()) {
        InventoryUI::FlushDeferredOverlays();
      }

      if (InventoryUI::HasPendingTooltip()) {
        InventoryUI::FlushPendingTooltip();
      }

      InventoryUI::UpdateAndRenderNotification(deltaTime);

      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    // Auto-screenshot: capture AFTER ImGui render (includes HUD overlay)
    if (captureScreenshot) {
      std::string ssPath;
      if (!outputName.empty()) {
        ssPath = "screenshots/" + outputName + ".jpg";
      } else if (!objectDebugName.empty()) {
        ssPath = "screenshots/" + objectDebugName + ".jpg";
      } else {
        ssPath =
            "screenshots/verif_" + std::to_string(std::time(nullptr)) + ".jpg";
      }
      int sw, sh;
      glfwGetFramebufferSize(window, &sw, &sh);
      std::vector<unsigned char> px(sw * sh * 3);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, sw, sh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
      std::vector<unsigned char> flipped(sw * sh * 3);
      for (int y = 0; y < sh; ++y)
        memcpy(&flipped[y * sw * 3], &px[(sh - 1 - y) * sw * 3], sw * 3);
      tjhandle comp = tjInitCompress();
      unsigned char *jbuf = nullptr;
      unsigned long jsize = 0;
      tjCompress2(comp, flipped.data(), sw, 0, sh, TJPF_RGB, &jbuf, &jsize,
                  TJSAMP_444, 95, TJFLAG_FASTDCT);
      std::filesystem::create_directories("screenshots");
      FILE *f = fopen(ssPath.c_str(), "wb");
      if (f) {
        fwrite(jbuf, 1, jsize, f);
        fclose(f);
      }
      tjFree(jbuf);
      tjDestroy(comp);
      std::cout << "[screenshot] Saved " << ssPath << std::endl;
      break;
    }

    // Auto-diagnostic: capture AFTER render, BEFORE swap (back buffer has
    // current frame)
    if (autoDiag && diagFrame >= 2) {
      int mode = (diagFrame - 2) / 2;
      if (mode < 6 && (diagFrame - 2) % 2 == 1) {
        std::string diagPath =
            "screenshots/diag_" + std::string(diagNames[mode]) + ".jpg";
        int sw, sh;
        glfwGetFramebufferSize(window, &sw, &sh);
        std::vector<unsigned char> px(sw * sh * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, sw, sh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
        std::vector<unsigned char> flipped(sw * sh * 3);
        for (int y = 0; y < sh; ++y)
          memcpy(&flipped[y * sw * 3], &px[(sh - 1 - y) * sw * 3], sw * 3);
        tjhandle comp = tjInitCompress();
        unsigned char *jbuf = nullptr;
        unsigned long jsize = 0;
        tjCompress2(comp, flipped.data(), sw, 0, sh, TJPF_RGB, &jbuf, &jsize,
                    TJSAMP_444, 95, TJFLAG_FASTDCT);
        std::filesystem::create_directories("screenshots");
        FILE *f = fopen(diagPath.c_str(), "wb");
        if (f) {
          fwrite(jbuf, 1, jsize, f);
          fclose(f);
        }
        tjFree(jbuf);
        tjDestroy(comp);
        std::cout << "[diag] Saved " << diagPath << std::endl;
      } else if (mode >= 6) {
        break;
      }
    }
    if (autoDiag || autoScreenshot || autoGif)
      diagFrame++;

    // Per-frame GL error check (only first 10 frames to avoid log spam)
    {
      static int frameNum = 0;
      if (frameNum < 10)
        checkGLError(("frame " + std::to_string(frameNum)).c_str());
      frameNum++;
    }

    glfwSwapBuffers(window);
  }

  // Save hero position via camera state for next launch (unless in diag mode)
  if (!autoDiag) {
    g_camera.SetPosition(g_hero.GetPosition());
    g_camera.SaveState("camera_save.txt");
  }

  // Save character stats to server before disconnecting
  g_server.SendCharSave(
      1, (uint16_t)g_serverLevel, (uint16_t)g_serverStr, (uint16_t)g_serverDex,
      (uint16_t)g_serverVit, (uint16_t)g_serverEne, (uint16_t)g_serverHP,
      (uint16_t)g_serverMaxHP, (uint16_t)g_serverLevelUpPoints,
      (uint64_t)g_serverXP, g_quickSlotDefIndex);
  g_server.Flush();

  // Disconnect from server
  g_server.Disconnect();
  // Cleanup
  // Cleanup handled by RAII/End
  g_monsterManager.Cleanup();
  g_npcManager.Cleanup();
  g_hero.Cleanup();
  g_clickEffect.Cleanup();
  g_sky.Cleanup();
  g_fireEffect.Cleanup();
  g_objectRenderer.Cleanup();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  // StreamRedirector handles restoration and deletion

  return 0;
}
