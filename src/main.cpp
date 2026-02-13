#include "Camera.hpp"
#include "Screenshot.hpp"
#include "Terrain.hpp"
#include "TerrainParser.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <filesystem>
#include <iostream>
#include <turbojpeg.h>

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
bool g_firstMouse = true;
float g_lastX = 1280.0f / 2.0f;
float g_lastY = 720.0f / 2.0f;

void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
  ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);

  if (g_firstMouse) {
    g_lastX = xpos;
    g_lastY = ypos;
    g_firstMouse = false;
  }

  float xoffset = xpos - g_lastX;
  float yoffset =
      g_lastY - ypos; // reversed since y-coordinates go from bottom to top

  g_lastX = xpos;
  g_lastY = ypos;

  if (!ImGui::GetIO().WantCaptureMouse &&
      glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
    g_camera.ProcessMouseRotation(xoffset, yoffset);
  }
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
  g_terrain.Load(terrainData, 1, data_path);
  std::cout << "Loaded Map 1 (Lorencia): " << terrainData.heightmap.size()
            << " height samples" << std::endl;

  // Set a reasonable initial camera position if no state loaded
  if (g_camera.GetPosition().y < 1.0f) {
    g_camera.SetPosition(glm::vec3(12750.0f, 1000.0f, 12750.0f));
  }

  // Load camera state if exists
  g_camera.LoadState("camera_save.txt");

  // Auto-diagnostic mode: --diag flag captures all debug views and exits
  bool autoDiag = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--diag")
      autoDiag = true;
  }
  if (autoDiag) {
    // Top-down zoomed view over Lorencia tavern area for clear tile diagnostics
    g_camera.SetPosition(glm::vec3(12800.0f, 3000.0f, 12800.0f));
    g_camera.SetAngles(0.0f, -89.0f); // Look straight down
    g_camera.SetZoom(3000.0f);
  }
  int diagFrame = 0;
  const char *diagNames[] = {"normal", "tileindex", "tileuv",
                             "alpha",  "lightmap",  "nolightmap"};

  glEnable(GL_DEPTH_TEST);

  ImVec4 clear_color = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
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
    g_terrain.Render(view, projection, currentFrame);

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Add some debug info
    ImGui::Begin("Terrain Debug");
    glm::vec3 camPos = g_camera.GetPosition();
    ImGui::Text("Camera Pos: %.1f, %.1f, %.1f", camPos.x, camPos.y, camPos.z);
    ImGui::Text("Zoom: %.1f", g_camera.GetZoom());

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
    if (autoDiag)
      diagFrame++;

    glfwSwapBuffers(window);
  }

  // Save camera state before exit
  g_camera.SaveState("camera_save.txt");

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
