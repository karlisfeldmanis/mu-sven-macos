#include "Camera.hpp"
#include "FireEffect.hpp"
#include "GrassRenderer.hpp"
#include "ObjectRenderer.hpp"
#include "Screenshot.hpp"
#include "Sky.hpp"
#include "Terrain.hpp"
#include "TerrainParser.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <ctime>
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

Camera g_camera(glm::vec3(12800.0f, 0.0f, 12800.0f));
Terrain g_terrain;
ObjectRenderer g_objectRenderer;
FireEffect g_fireEffect;
Sky g_sky;
GrassRenderer g_grass;

// Point lights collected from light-emitting world objects
static const int MAX_POINT_LIGHTS = 64;
struct PointLight {
  glm::vec3 position;
  glm::vec3 color;
  float range;
};
static std::vector<PointLight> g_pointLights;

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

void mouse_button_callback(GLFWwindow *window, int button, int action,
                           int mods) {
  ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
}

void key_callback(GLFWwindow *window, int key, int scancode, int action,
                  int mods) {
  ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

void char_callback(GLFWwindow *window, unsigned int c) {
  ImGui_ImplGlfw_CharCallback(window, c);
}

void processInput(GLFWwindow *window, float deltaTime) {
  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
    g_camera.ProcessKeyboard(0, deltaTime);
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    g_camera.ProcessKeyboard(1, deltaTime);
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
    g_camera.ProcessKeyboard(2, deltaTime);
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
    g_camera.ProcessKeyboard(3, deltaTime);

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

  g_terrain.Init(); // Initialize OpenGL resources for terrain

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

  // Reconstruct TW_NOGROUND for bridge cells over water.
  // Original engine reads this from .att file (ZzzLodTerrain.cpp:176) and
  // RenderTerrainTile() skips tiles with TW_NOGROUND (line 1665).
  // Our .att version lacks these flags, so reconstruct from bridge object
  // positions. Must run BEFORE g_terrain.Load() so modified attributes reach
  // the GPU. Use oriented rectangle matching bridge direction (narrow
  // perpendicular to deck).
  {
    const int S = TerrainParser::TERRAIN_SIZE;
    int count = 0;
    for (const auto &obj : terrainData.objects) {
      if (obj.type != 80)
        continue; // MODEL_BRIDGE only
      std::cout << "[Bridge] Type 80 at gl_pos=(" << obj.position.x << ", "
                << obj.position.y << ", " << obj.position.z << ")" << std::endl;
      int gz = (int)(obj.position.x / 100.0f);
      int gx = (int)(obj.position.z / 100.0f);
      // Bridge orientation from Z rotation:
      // angle Z ≈ ±90: bridge spans along gz (N-S), narrow in gx
      // angle Z ≈ 0/180: bridge spans along gx (E-W), narrow in gz
      float angZ =
          std::abs(std::fmod(glm::degrees(obj.rotation.z) + 360.0f, 180.0f));
      bool spanAlongGZ = (std::abs(angZ - 90.0f) < 45.0f);
      int rGZ = spanAlongGZ ? 4 : 2; // Increased radius to ensure coverage
      int rGX = spanAlongGZ ? 2 : 4;
      for (int dz = -rGZ; dz <= rGZ; ++dz) {
        for (int dx = -rGX; dx <= rGX; ++dx) {
          int cz = gz + dz, cx = gx + dx;
          if (cz < 0 || cz >= S || cx < 0 || cx >= S)
            continue;
          int idx = cz * S + cx;
          // Mark all cells under/near bridge as submerged bridge supports
          terrainData.mapping.attributes[idx] |= 0x18; // TW_NOGROUND | TW_WATER
          count++;
          if (dz == 0 && dx == 0) {
            std::cout << "[Bridge] Marked center cell (" << cz << "," << cx
                      << ") at idx " << idx << std::endl;
          }
        }
      }
    }
    if (count > 0)
      std::cout << "[Terrain] Reconstructed " << count
                << " bridge support cells (TW_NOGROUND|TW_WATER)" << std::endl;
  }

  g_terrain.Load(terrainData, 1, data_path);
  std::cout << "Loaded Map 1 (Lorencia): " << terrainData.heightmap.size()
            << " height samples, " << terrainData.objects.size() << " objects"
            << std::endl;

  // Load world objects
  g_objectRenderer.Init();
  g_objectRenderer.SetTerrainLightmap(terrainData.lightmap);
  std::string object1_path = data_path + "/Object1";
  g_objectRenderer.LoadObjects(terrainData.objects, object1_path);

  // Initialize grass billboards
  g_grass.Init();
  g_grass.Load(terrainData, 1, data_path);

  // Initialize sky
  g_sky.Init(data_path + "/");

  // Initialize fire effects and register emitters from fire-type objects
  g_fireEffect.Init(data_path + "/Effect");
  for (auto &inst : g_objectRenderer.GetInstances()) {
    auto &offsets = GetFireOffsets(inst.type);
    for (auto &off : offsets) {
      glm::vec3 worldPos = glm::vec3(inst.modelMatrix * glm::vec4(off, 1.0f));
      g_fireEffect.AddEmitter(worldPos);
    }
  }
  std::cout << "[FireEffect] Registered " << g_fireEffect.GetEmitterCount()
            << " fire emitters" << std::endl;
  // Print fire-type objects for debugging/testing
  for (int i = 0; i < (int)terrainData.objects.size(); ++i) {
    int t = terrainData.objects[i].type;
    if (t == 50 || t == 51 || t == 52 || t == 55 || t == 80)
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

  // Load saved camera state (persists position/angle/zoom across restarts)
  g_camera.LoadState("camera_save.txt");

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
    g_camera.SetPosition(glm::vec3(customX, customY, customZ));
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
      g_camera.SetPosition(glm::vec3(objPos.x, objPos.y, objPos.z));
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
    g_grass.Render(view, projection, currentFrame, camPos);

    // Update and render fire effects
    g_fireEffect.Update(deltaTime);
    g_fireEffect.Render(view, projection);

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

    glfwSwapBuffers(window);
  }

  // Save camera state for next launch
  g_camera.SaveState("camera_save.txt");

  // Cleanup
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
