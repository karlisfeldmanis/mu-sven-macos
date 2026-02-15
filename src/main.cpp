#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "Camera.hpp"
#include "ClickEffect.hpp"
#include "FireEffect.hpp"
#include "GrassRenderer.hpp"
#include "HeroCharacter.hpp"
#include "ObjectRenderer.hpp"
#include "Screenshot.hpp"
#include "Shader.hpp"
#include "Sky.hpp"
#include "Terrain.hpp"
#include "TerrainParser.hpp"
#include "ViewerCommon.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <streambuf>
#include <turbojpeg.h>

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

// Point lights collected from light-emitting world objects
static const int MAX_POINT_LIGHTS = 64;
static std::vector<PointLight> g_pointLights;

// Hero character and click-to-move effect
static HeroCharacter g_hero;
static ClickEffect g_clickEffect;

static const TerrainData *g_terrainDataPtr = nullptr;

// Roof hiding: types 125 (HouseWall05) and 126 (HouseWall06) fade when
// hero stands on layer1 tile == 4 (building interior). Original: ZzzObject.cpp:3744
static std::unordered_map<int, float> g_typeAlpha = {{125, 1.0f}, {126, 1.0f}};
static std::unordered_map<int, float> g_typeAlphaTarget = {{125, 1.0f}, {126, 1.0f}};

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

void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
  ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
  // Camera rotation disabled — fixed isometric angle like original MU
}

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
  ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
  g_camera.ProcessMouseScroll(yoffset);
}

// --- Terrain helpers ---

static float getTerrainHeight(const TerrainData &td, float worldX,
                              float worldZ) {
  const int S = TerrainParser::TERRAIN_SIZE;
  // World → grid: WorldX maps to gridZ, WorldZ maps to gridX
  float gz = worldX / 100.0f;
  float gx = worldZ / 100.0f;
  gz = std::clamp(gz, 0.0f, (float)(S - 2));
  gx = std::clamp(gx, 0.0f, (float)(S - 2));
  int xi = (int)gx, zi = (int)gz;
  float xd = gx - (float)xi, zd = gz - (float)zi;
  float h00 = td.heightmap[zi * S + xi];
  float h10 = td.heightmap[zi * S + (xi + 1)];
  float h01 = td.heightmap[(zi + 1) * S + xi];
  float h11 = td.heightmap[(zi + 1) * S + (xi + 1)];
  return (h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
          h01 * (1 - xd) * zd + h11 * xd * zd);
}

static bool isWalkable(const TerrainData &td, float worldX, float worldZ) {
  const int S = TerrainParser::TERRAIN_SIZE;
  int gz = (int)(worldX / 100.0f);
  int gx = (int)(worldZ / 100.0f);
  if (gx < 0 || gz < 0 || gx >= S || gz >= S)
    return false;
  uint8_t attr = td.mapping.attributes[gz * S + gx];
  // Only TW_NOMOVE (0x04) blocks character movement.
  // TW_NOGROUND (0x08) is our bridge shader flag, not a movement blocker —
  // characters walk on bridges. Original uses iWall > byMapAttribute where
  // the raw .att data has TW_NOMOVE on actual walls/obstacles.
  return (attr & 0x04) == 0;
}

// --- Ray-terrain intersection for click-to-move ---

static bool screenToTerrain(GLFWwindow *window, double mouseX, double mouseY,
                            glm::vec3 &outWorld) {
  if (!g_terrainDataPtr)
    return false;

  int winW, winH;
  glfwGetWindowSize(window, &winW, &winH);

  // NDC coordinates
  float ndcX = (float)(2.0 * mouseX / winW - 1.0);
  float ndcY = (float)(1.0 - 2.0 * mouseY / winH);

  glm::mat4 proj = g_camera.GetProjectionMatrix((float)winW, (float)winH);
  glm::mat4 view = g_camera.GetViewMatrix();
  glm::mat4 invVP = glm::inverse(proj * view);

  // Unproject near and far points
  glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  nearPt /= nearPt.w;
  farPt /= farPt.w;

  glm::vec3 rayOrigin = glm::vec3(nearPt);
  glm::vec3 rayDir = glm::normalize(glm::vec3(farPt) - rayOrigin);

  // March along ray, find where it crosses the terrain
  float stepSize = 50.0f;
  float maxDist = 10000.0f;
  float prevT = 0.0f;
  float prevAbove = rayOrigin.y - getTerrainHeight(*g_terrainDataPtr, rayOrigin.x, rayOrigin.z);

  for (float t = stepSize; t < maxDist; t += stepSize) {
    glm::vec3 p = rayOrigin + rayDir * t;
    // Bounds check
    if (p.x < 0 || p.z < 0 || p.x > 25500.0f || p.z > 25500.0f)
      continue;
    float terrH = getTerrainHeight(*g_terrainDataPtr, p.x, p.z);
    float above = p.y - terrH;

    if (above < 0.0f) {
      // Crossed below terrain — binary search for precise intersection
      float lo = prevT, hi = t;
      for (int i = 0; i < 8; ++i) {
        float mid = (lo + hi) * 0.5f;
        glm::vec3 mp = rayOrigin + rayDir * mid;
        float mh = getTerrainHeight(*g_terrainDataPtr, mp.x, mp.z);
        if (mp.y > mh)
          lo = mid;
        else
          hi = mid;
      }
      glm::vec3 hit = rayOrigin + rayDir * ((lo + hi) * 0.5f);
      outWorld = glm::vec3(hit.x,
                           getTerrainHeight(*g_terrainDataPtr, hit.x, hit.z),
                           hit.z);
      return true;
    }
    prevT = t;
    prevAbove = above;
  }
  return false;
}

// --- Click-to-move mouse handler ---

void mouse_button_callback(GLFWwindow *window, int button, int action,
                           int mods) {
  ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

  // Click-to-move on left click
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    if (!ImGui::GetIO().WantCaptureMouse && g_terrainDataPtr) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);
      glm::vec3 target;
      if (screenToTerrain(window, mx, my, target)) {
        if (isWalkable(*g_terrainDataPtr, target.x, target.z)) {
          g_hero.MoveTo(target);
          g_clickEffect.Show(target);
        }
      }
    }
  }
}

void key_callback(GLFWwindow *window, int key, int scancode, int action,
                  int mods) {
  ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

void char_callback(GLFWwindow *window, unsigned int c) {
  ImGui_ImplGlfw_CharCallback(window, c);
}

// --- Process input: hero movement + screenshot ---

void processInput(GLFWwindow *window, float deltaTime) {
  bool wasMoving = g_hero.IsMoving();
  g_hero.ProcessMovement(deltaTime);

  // Hide click effect when hero stops moving
  if (wasMoving && !g_hero.IsMoving())
    g_clickEffect.Hide();

  // Camera follows hero
  if (wasMoving)
    g_camera.SetPosition(g_hero.GetPosition());

  static bool pPressed = false;
  if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) {
    if (!pPressed) {
      Screenshot::Capture(window);
      pPressed = true;
    }
  } else {
    pPressed = false;
  }
}

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
      1280, 720, "Mu Online Remaster (Native macOS C++)", nullptr, nullptr);
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
  glfwSetCursorPosCallback(window, mouse_callback);
  glfwSetScrollCallback(window, scroll_callback);
  glfwSetMouseButtonCallback(window, mouse_button_callback);
  glfwSetKeyCallback(window, key_callback);
  glfwSetCharCallback(window, char_callback);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Load Terrain for testing
  std::string data_path = "/Users/karlisfeldmanis/Desktop/mu_remaster/"
                          "references/other/MuMain/src/bin/Data";
  TerrainData terrainData = TerrainParser::LoadWorld(1, data_path);

  // Reconstruct TW_NOGROUND for bridge cells.
  // The .att file for this data version lacks these flags (verified: 0 cells).
  // Original engine reads them from .att (ZzzLodTerrain.cpp:1665).
  // We reconstruct from bridge objects (type 80) with orientation awareness.
  {
    const int S = TerrainParser::TERRAIN_SIZE;
    int count = 0;
    for (const auto &obj : terrainData.objects) {
      if (obj.type != 80)
        continue;
      int gz = (int)(obj.position.x / 100.0f);
      int gx = (int)(obj.position.z / 100.0f);
      float angZ = std::abs(
          std::fmod(glm::degrees(obj.rotation.z) + 360.0f, 180.0f));
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
  checkGLError("fire init");
  for (auto &inst : g_objectRenderer.GetInstances()) {
    auto &offsets = GetFireOffsets(inst.type);
    for (auto &off : offsets) {
      // Extract rotation without scale (original CreateFire only rotates, no scale)
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
  g_hero.SetTerrainLightmap(terrainData.lightmap);
  g_hero.SetPointLights(g_pointLights);
  g_hero.SnapToTerrain();

  g_clickEffect.Init();
  g_clickEffect.LoadAssets(data_path);
  g_clickEffect.SetTerrainData(&terrainData);
  checkGLError("hero init");

  // Load saved camera state (persists position/angle/zoom across restarts)
  g_camera.LoadState("camera_save.txt");

  // Sync hero position from loaded camera state
  g_hero.SetPosition(g_camera.GetPosition());
  g_hero.SnapToTerrain();
  g_camera.SetPosition(g_hero.GetPosition());

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
  if (autoDiag) {
    // Top-down zoomed view over Lorencia tavern area for clear tile diagnostics
    g_camera.SetPosition(glm::vec3(12800.0f, 3000.0f, 12800.0f));
    g_camera.SetAngles(0.0f, -89.0f); // Look straight down
    g_camera.SetZoom(3000.0f);
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
      g_camera.SetPosition(glm::vec3(objPos.x, objPos.y + 1500.0f, objPos.z));
      g_camera.SetAngles(0.0f, -89.0f);
      g_camera.SetZoom(1500.0f);
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
    processInput(window, deltaTime);
    g_camera.Update(deltaTime);

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
    }

    // Auto-diagnostic: set debug mode BEFORE render
    if (autoDiag && diagFrame >= 2) {
      int mode = (diagFrame - 2) / 2;
      if (mode < 6 && (diagFrame - 2) % 2 == 0) {
        g_terrain.SetDebugMode(mode);
      }
    }

    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 projection = g_camera.GetProjectionMatrix(1280.0f, 720.0f);
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
    g_grass.Render(view, projection, currentFrame, camPos, g_hero.GetPosition());

    // Update and render fire effects
    g_fireEffect.Update(deltaTime);
    g_fireEffect.Render(view, projection);

    // Render hero character, shadow, and click effect (after all world geometry)
    g_clickEffect.Render(view, projection, deltaTime, g_hero.GetShader());
    g_hero.Render(view, projection, camPos, deltaTime);
    g_hero.RenderShadow(view, projection);

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

    // Auto-screenshot: capture after enough frames for fire particles to build
    // up Capture BEFORE ImGui rendering so debug overlay is not in the output
    if (autoScreenshot && diagFrame == 60) {
      std::string ssPath;
      if (!outputName.empty()) {
        ssPath = "screenshots/" + outputName + ".jpg";
      } else if (!objectDebugName.empty()) {
        ssPath = "screenshots/" + objectDebugName + ".jpg";
      } else {
        // Use a timestamp to ensure uniqueness
        ssPath =
            "screenshots/verif_" + std::to_string(std::time(nullptr)) + ".jpg";
      }
      int sw, sh;
      glfwGetFramebufferSize(window, &sw, &sh);
      std::vector<unsigned char> px(sw * sh * 3);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, sw, sh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
      // ... (capture logic)
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

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Add some debug info
    ImGui::Begin("Terrain Debug");
    ImGui::Text("Camera Pos: %.1f, %.1f, %.1f", camPos.x, camPos.y, camPos.z);
    ImGui::Text("Zoom: %.1f", g_camera.GetZoom());
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
          ImGui::Text("Attr: 0x%02X%s%s%s%s%s", attr,
                      (attr & 0x01) ? " SAFE" : "",
                      (attr & 0x04) ? " NOMOVE" : "",
                      (attr & 0x08) ? " NOGROUND" : "",
                      (attr & 0x10) ? " WATER" : "",
                      (attr & 0x20) ? " ACTION" : "");
          uint8_t tile = g_terrainDataPtr->mapping.layer1[gz * S + gx];
          ImGui::Text("Tile: %d%s", tile,
                      (tile == 4) ? " (ROOF HIDE)" : "");
          ImGui::Text("Roof: %.0f%%", g_typeAlpha[125] * 100.0f);
        }
      }
      ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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

  // Save hero position via camera state for next launch
  g_camera.SetPosition(g_hero.GetPosition());
  g_camera.SaveState("camera_save.txt");

  // Cleanup
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

  // Restore original stream buffers before log file closes
  if (origCout)
    std::cout.rdbuf(origCout);
  if (origCerr)
    std::cerr.rdbuf(origCerr);
  delete coutTee;
  delete cerrTee;

  return 0;
}
