#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "FireEffect.hpp"
#include "MeshBuffers.hpp"
#include "Screenshot.hpp"
#include "Shader.hpp"
#include "TextureLoader.hpp"
#include <fstream>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>

// Inline GLSL for colored line rendering
static const char *kLineVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
void main() {
  gl_Position = uMVP * vec4(aPos, 1.0);
  vColor = aColor;
}
)";

static const char *kLineFragSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
  FragColor = vec4(vColor, 1.0);
}
)";

static GLuint CompileLineShader() {
  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &kLineVertSrc, nullptr);
  glCompileShader(vs);

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &kLineFragSrc, nullptr);
  glCompileShader(fs);

  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);

  glDeleteShader(vs);
  glDeleteShader(fs);
  return prog;
}

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

// BlendMesh texture ID lookup by BMD filename (model viewer has no type IDs)
static int GetBlendMeshTexIdFromFilename(const std::string &filename) {
  if (filename == "House03.bmd")
    return 4;
  if (filename == "House04.bmd")
    return 8;
  if (filename == "House05.bmd")
    return 2;
  if (filename == "HouseWall02.bmd")
    return 4;
  if (filename == "Bonfire01.bmd")
    return 1;
  if (filename == "StreetLight01.bmd")
    return 1;
  if (filename == "Candle01.bmd")
    return 1;
  if (filename == "Carriage01.bmd")
    return 2;
  if (filename == "Waterspout01.bmd")
    return 3;
  return -1;
}

// UV scroll animation for specific models
static bool HasUVScrollAnimation(const std::string &filename) {
  return filename == "House04.bmd" || filename == "House05.bmd" ||
         filename == "Waterspout01.bmd";
}

namespace fs = std::filesystem;

static const std::string DATA_PATH =
    "/Users/karlisfeldmanis/Desktop/mu_remaster/"
    "references/other/MuMain/src/bin/Data/Object1/";
static const std::string EFFECT_PATH =
    "/Users/karlisfeldmanis/Desktop/mu_remaster/"
    "references/other/MuMain/src/bin/Data/Effect";
static const int WIN_WIDTH = 1280;
static const int WIN_HEIGHT = 720;

class ObjectBrowser {
public:
  void Run() {
    if (!InitWindow())
      return;

#ifdef __APPLE__
    activateMacOSApp();
#endif

    InitImGui();
    ScanDirectory();

    if (bmdFiles.empty()) {
      std::cerr << "[ObjectBrowser] No BMD files found in " << DATA_PATH
                << std::endl;
      ShutdownImGui();
      glfwDestroyWindow(window);
      glfwTerminate();
      return;
    }

    std::ifstream shaderTest("shaders/model.vert");
    Shader shader(shaderTest.good() ? "shaders/model.vert"
                                    : "../shaders/model.vert",
                  shaderTest.good() ? "shaders/model.frag"
                                    : "../shaders/model.frag");
    InitAxes();
    fireEffect.Init(EFFECT_PATH);
    LoadObject(0);

    while (!glfwWindowShouldClose(window)) {
      float currentFrame = glfwGetTime();
      deltaTime = currentFrame - lastFrame;
      lastFrame = currentFrame;

      glfwPollEvents();
      RenderScene(shader);

      Screenshot::TickRecording(window);

      RenderUI();
      glfwSwapBuffers(window);
    }

    UnloadObject();
    fireEffect.Cleanup();
    if (axisProgram) {
      glDeleteProgram(axisProgram);
      glDeleteVertexArrays(1, &axisVAO);
      glDeleteBuffers(1, &axisVBO);
    }
    ShutdownImGui();
    glfwDestroyWindow(window);
    glfwTerminate();
  }

private:
  GLFWwindow *window = nullptr;

  // File list
  std::vector<std::string> bmdFiles;
  int currentIndex = 0;

  // Currently loaded model
  std::unique_ptr<BMDData> currentBMD;
  std::vector<MeshBuffers> meshBuffers;
  AABB currentAABB;

  // Bone world matrices for current model
  std::vector<BoneWorldMatrix> boneMatrices;

  // Orbit camera
  float orbitYaw = 45.0f;
  float orbitPitch = -25.0f;
  float orbitDistance = 500.0f;
  glm::vec3 orbitCenter{0.0f};

  // Mouse state
  bool dragging = false;
  double lastMouseX = 0, lastMouseY = 0;

  // Timing
  float deltaTime = 0.0f;
  float lastFrame = 0.0f;

  // ImGui filter
  char filterBuf[128] = "";

  // Debug axis
  GLuint axisVAO = 0, axisVBO = 0;
  GLuint axisProgram = 0;
  float axisLength = 100.0f;

  // Fire effects
  FireEffect fireEffect;

  // BlendMesh: per-model window light state
  int blendMeshTexId = -1;
  bool hasUVScroll = false;

  // Animation state
  bool currentIsAnimated = false;
  int currentNumKeys = 0;
  float currentAnimFrame = 0.0f;
  bool animationEnabled = true;
  float animSpeed = 4.0f; // keyframes/sec (reference: 0.16 * 25fps)

  // GIF recording (state managed by Screenshot module)
  int gifFrameTarget = 72;
  float gifScaleSetting = 0.5f;
  int gifFpsSetting = 12;
  int gifSkipSetting = 1; // 1 means 12.5fps if render is 25fps

  // --- Initialization ---

  bool InitWindow() {
    if (!glfwInit())
      return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    window = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, "MU Object Browser",
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

  void InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init("#version 150");
  }

  void ShutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }

  void InitAxes() {
    axisProgram = CompileLineShader();

    glGenVertexArrays(1, &axisVAO);
    glGenBuffers(1, &axisVBO);
    glBindVertexArray(axisVAO);
    glBindBuffer(GL_ARRAY_BUFFER, axisVBO);
    // Will be filled dynamically in UpdateAxisGeometry
    glBufferData(GL_ARRAY_BUFFER, 6 * 6 * sizeof(float), nullptr,
                 GL_DYNAMIC_DRAW);
    // pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(0);
    // color
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
  }

  void UpdateAxisGeometry() {
    // 6 vertices: 2 per axis (origin -> tip), each with pos(3) + color(3)
    float L = axisLength;
    // clang-format off
    float verts[] = {
      // X axis - Red
      0, 0, 0,   1, 0, 0,
      L, 0, 0,   1, 0, 0,
      // Y axis - Green
      0, 0, 0,   0, 1, 0,
      0, L, 0,   0, 1, 0,
      // Z axis - Blue
      0, 0, 0,   0, 0, 1,
      0, 0, L,   0, 0, 1,
    };
    // clang-format on
    glBindBuffer(GL_ARRAY_BUFFER, axisVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
  }

  // --- Directory scanning ---

  void ScanDirectory() {
    for (auto &entry : fs::directory_iterator(DATA_PATH)) {
      if (entry.is_regular_file()) {
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".bmd") {
          bmdFiles.push_back(entry.path().filename().string());
        }
      }
    }
    std::sort(bmdFiles.begin(), bmdFiles.end());
    std::cout << "[ObjectBrowser] Found " << bmdFiles.size() << " BMD files"
              << std::endl;
  }

  // --- Object loading / unloading ---

  void UnloadObject() {
    for (auto &mb : meshBuffers) {
      glDeleteVertexArrays(1, &mb.vao);
      glDeleteBuffers(1, &mb.vbo);
      glDeleteBuffers(1, &mb.ebo);
      if (mb.texture != 0) {
        glDeleteTextures(1, &mb.texture);
      }
    }
    meshBuffers.clear();
    currentBMD.reset();
    boneMatrices.clear();
  }

  void LoadObject(int index) {
    UnloadObject();
    currentIndex = index;

    std::string fullPath = DATA_PATH + bmdFiles[index];
    currentBMD = BMDParser::Parse(fullPath);

    if (!currentBMD) {
      std::cerr << "[ObjectBrowser] Failed to parse: " << bmdFiles[index]
                << std::endl;
      std::string title = "MU Object Browser - FAILED: " + bmdFiles[index];
      glfwSetWindowTitle(window, title.c_str());
      return;
    }

    // Compute bone world matrices for rest pose
    boneMatrices = ComputeBoneMatrices(currentBMD.get());

    // Detect animated models (>1 keyframe in first action)
    currentIsAnimated = false;
    currentNumKeys = 0;
    currentAnimFrame = 0.0f;
    if (!currentBMD->Actions.empty() &&
        currentBMD->Actions[0].NumAnimationKeys > 1) {
      currentIsAnimated = true;
      currentNumKeys = currentBMD->Actions[0].NumAnimationKeys;
    }

    // Upload meshes with bone-transformed vertices
    currentAABB = AABB{};
    for (auto &mesh : currentBMD->Meshes) {
      UploadMesh(mesh, DATA_PATH, currentIsAnimated);
    }

    // Resolve BlendMesh for this model
    blendMeshTexId = GetBlendMeshTexIdFromFilename(bmdFiles[index]);
    hasUVScroll = HasUVScrollAnimation(bmdFiles[index]);
    if (blendMeshTexId >= 0) {
      for (auto &mb : meshBuffers) {
        if (mb.bmdTextureId == blendMeshTexId) {
          mb.isWindowLight = true;
        }
      }
    }

    AutoFrame();

    // Register fire emitters if this is a fire-type model
    fireEffect.ClearEmitters();
    int fireType = GetFireTypeFromFilename(bmdFiles[index]);
    if (fireType >= 0) {
      // Model viewer uses Rx(-90°) to convert MU Z-up → GL Y-up
      glm::mat4 modelMat = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                                       glm::vec3(1.0f, 0.0f, 0.0f));
      auto &offsets = GetFireOffsets(fireType);
      for (auto &off : offsets) {
        glm::vec3 worldPos = glm::vec3(modelMat * glm::vec4(off, 1.0f));
        fireEffect.AddEmitter(worldPos);
      }
    }

    std::string title = "MU Object Browser - " + bmdFiles[index] + " (" +
                        std::to_string(index + 1) + "/" +
                        std::to_string(bmdFiles.size()) + ")";
    glfwSetWindowTitle(window, title.c_str());
  }

  void UploadMesh(const Mesh_t &mesh, const std::string &baseDir,
                  bool dynamic = false) {
    MeshBuffers mb;
    mb.texture = 0;

    struct Vertex {
      glm::vec3 pos;
      glm::vec3 normal;
      glm::vec2 tex;
    };
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    for (int i = 0; i < mesh.NumTriangles; ++i) {
      auto &tri = mesh.Triangles[i];
      int steps = (tri.Polygon == 3) ? 3 : 4;
      int startIdx = vertices.size();
      for (int v = 0; v < 3; ++v) {
        Vertex vert;
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

        // Apply bone transform
        int boneIdx = srcVert.Node;
        if (boneIdx >= 0 && boneIdx < (int)boneMatrices.size()) {
          const auto &bm = boneMatrices[boneIdx];
          vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                            srcVert.Position);
          vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(),
                                             srcNorm.Normal);
        } else {
          vert.pos = srcVert.Position;
          vert.normal = srcNorm.Normal;
        }

        vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                             mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
        vertices.push_back(vert);
        indices.push_back(startIdx + v);

        // Update AABB with transformed position
        currentAABB.min = glm::min(currentAABB.min, vert.pos);
        currentAABB.max = glm::max(currentAABB.max, vert.pos);
      }
      if (steps == 4) {
        int quadIndices[3] = {0, 2, 3};
        for (int v : quadIndices) {
          Vertex vert;
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

          int boneIdx = srcVert.Node;
          if (boneIdx >= 0 && boneIdx < (int)boneMatrices.size()) {
            const auto &bm = boneMatrices[boneIdx];
            vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                              srcVert.Position);
            vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(),
                                               srcNorm.Normal);
          } else {
            vert.pos = srcVert.Position;
            vert.normal = srcNorm.Normal;
          }

          vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                               mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
          vertices.push_back(vert);
          indices.push_back(vertices.size() - 1);
        }
      }
    }

    mb.indexCount = indices.size();
    mb.vertexCount = vertices.size();
    mb.isDynamic = dynamic;
    if (mb.indexCount == 0) {
      meshBuffers.push_back(mb);
      return;
    }

    GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;

    glGenVertexArrays(1, &mb.vao);
    glGenBuffers(1, &mb.vbo);
    glGenBuffers(1, &mb.ebo);

    glBindVertexArray(mb.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
                 vertices.data(), usage);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mb.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
                 indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)(sizeof(float) * 3));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)(sizeof(float) * 6));
    glEnableVertexAttribArray(2);

    // Load texture via shared resolver
    auto texResult = TextureLoader::ResolveWithInfo(baseDir, mesh.TextureName);
    mb.texture = texResult.textureID;
    mb.hasAlpha = texResult.hasAlpha;

    // Parse texture script flags from name
    auto scriptFlags = TextureLoader::ParseScriptFlags(mesh.TextureName);
    mb.noneBlend = scriptFlags.noneBlend;
    mb.hidden = scriptFlags.hidden;
    mb.bright = scriptFlags.bright;

    mb.bmdTextureId = mesh.Texture; // Store BMD texture slot for BlendMesh matching

    meshBuffers.push_back(mb);
  }

  void AutoFrame() {
    glm::vec3 c = currentAABB.center();
    // Apply the same Z-up → Y-up rotation (-90° around X): (x, y, z) → (x, z,
    // -y)
    orbitCenter = glm::vec3(c.x, c.z, -c.y);
    float radius = currentAABB.radius();
    if (radius < 0.001f)
      radius = 100.0f;

    orbitDistance = radius * 2.6f;
    orbitYaw = 45.0f;
    orbitPitch = -25.0f;

    axisLength = radius * 0.5f;
    UpdateAxisGeometry();
  }

  // --- Animation ---

  void RetransformMeshLocal(const Mesh_t &mesh,
                            const std::vector<BoneWorldMatrix> &bones,
                            MeshBuffers &mb) {
    if (!mb.isDynamic || mb.vertexCount == 0 || mb.vbo == 0)
      return;

    struct Vertex {
      glm::vec3 pos;
      glm::vec3 normal;
      glm::vec2 tex;
    };
    std::vector<Vertex> vertices;
    vertices.reserve(mb.vertexCount);

    for (int i = 0; i < mesh.NumTriangles; ++i) {
      auto &tri = mesh.Triangles[i];
      int steps = (tri.Polygon == 3) ? 3 : 4;
      for (int v = 0; v < 3; ++v) {
        Vertex vert;
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];
        int boneIdx = srcVert.Node;
        if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
          const auto &bm = bones[boneIdx];
          vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                            srcVert.Position);
          vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(),
                                             srcNorm.Normal);
        } else {
          vert.pos = srcVert.Position;
          vert.normal = srcNorm.Normal;
        }
        vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                              mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
        vertices.push_back(vert);
      }
      if (steps == 4) {
        int quadIndices[3] = {0, 2, 3};
        for (int v : quadIndices) {
          Vertex vert;
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];
          int boneIdx = srcVert.Node;
          if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
            const auto &bm = bones[boneIdx];
            vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                              srcVert.Position);
            vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(),
                                               srcNorm.Normal);
          } else {
            vert.pos = srcVert.Position;
            vert.normal = srcNorm.Normal;
          }
          vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                                mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
          vertices.push_back(vert);
        }
      }
    }

    glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(Vertex),
                    vertices.data());
  }

  // --- Orbit camera ---

  glm::vec3 GetEyePosition() const {
    float yawRad = glm::radians(orbitYaw);
    float pitchRad = glm::radians(orbitPitch);

    glm::vec3 offset;
    offset.x = orbitDistance * cos(pitchRad) * cos(yawRad);
    offset.y = -orbitDistance * sin(pitchRad);
    offset.z = orbitDistance * cos(pitchRad) * sin(yawRad);

    return orbitCenter + offset;
  }

  glm::mat4 GetViewMatrix() const {
    return glm::lookAt(GetEyePosition(), orbitCenter, glm::vec3(0, 1, 0));
  }

  // --- Rendering ---

  void RenderScene(Shader &shader) {
    glClearColor(0.15f, 0.18f, 0.22f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (meshBuffers.empty())
      return;

    // Advance skeletal animation if model is animated
    if (currentIsAnimated && animationEnabled && currentBMD) {
      currentAnimFrame += animSpeed * deltaTime;
      if (currentAnimFrame >= (float)currentNumKeys)
        currentAnimFrame = std::fmod(currentAnimFrame, (float)currentNumKeys);

      auto bones =
          ComputeBoneMatricesInterpolated(currentBMD.get(), 0, currentAnimFrame);
      for (int mi = 0;
           mi < (int)meshBuffers.size() && mi < (int)currentBMD->Meshes.size();
           ++mi) {
        RetransformMeshLocal(currentBMD->Meshes[mi], bones, meshBuffers[mi]);
      }
    }

    shader.use();

    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), (float)fbWidth / (float)fbHeight, 0.1f, 100000.0f);
    glm::mat4 view = GetViewMatrix();
    // MU Online uses Z-up; rotate -90° around X to convert to OpenGL Y-up
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                                  glm::vec3(1.0f, 0.0f, 0.0f));

    shader.setMat4("projection", projection);
    shader.setMat4("view", view);
    shader.setMat4("model", model);

    glm::vec3 eye = GetEyePosition();
    shader.setVec3("lightPos", eye + glm::vec3(0, 200, 0));
    shader.setVec3("lightColor", 1.0f, 1.0f, 1.0f);
    shader.setVec3("viewPos", eye);
    shader.setBool("useFog", false);
    shader.setFloat("blendMeshLight", 1.0f);
    shader.setVec3("terrainLight", 1.0f, 1.0f, 1.0f);
    shader.setVec2("texCoordOffset", glm::vec2(0.0f));
    shader.setInt("numPointLights", 0);

    // BlendMesh animation state
    float currentTime = (float)glfwGetTime();
    float flickerBase =
        0.55f + 0.15f * std::sin(currentTime * 7.3f) *
                    std::sin(currentTime * 11.1f + 2.0f);
    float uvScroll = -std::fmod(currentTime, 1.0f);

    for (auto &mb : meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;

      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);

      if (mb.isWindowLight && blendMeshTexId >= 0) {
        // BlendMesh: additive blending with intensity flicker
        shader.setFloat("blendMeshLight", flickerBase);
        if (hasUVScroll)
          shader.setVec2("texCoordOffset", glm::vec2(0.0f, uvScroll));
        else
          shader.setVec2("texCoordOffset", glm::vec2(0.0f));

        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        shader.setFloat("blendMeshLight", 1.0f);
        shader.setVec2("texCoordOffset", glm::vec2(0.0f));
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

    // Update and render fire effects
    fireEffect.Update(deltaTime);
    fireEffect.Render(view, projection);

    // Draw XYZ debug axes at world origin (same rotation as model)
    if (axisProgram) {
      glUseProgram(axisProgram);
      glm::mat4 mvp = projection * view * model;
      glUniformMatrix4fv(glGetUniformLocation(axisProgram, "uMVP"), 1, GL_FALSE,
                         &mvp[0][0]);
      glLineWidth(2.0f);
      glBindVertexArray(axisVAO);
      glDrawArrays(GL_LINES, 0, 6);
      glBindVertexArray(0);
    }
  }

  void RenderUI() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(250, (float)winH));
    ImGui::Begin("Objects", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse);

    ImGui::InputText("Filter", filterBuf, sizeof(filterBuf));

    std::string filterStr(filterBuf);
    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(),
                   ::tolower);

    ImGui::BeginChild("FileList", ImVec2(0, (float)winH * 0.5f), true);
    for (int i = 0; i < (int)bmdFiles.size(); ++i) {
      std::string lower = bmdFiles[i];
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      if (!filterStr.empty() && lower.find(filterStr) == std::string::npos)
        continue;

      bool selected = (i == currentIndex);
      if (ImGui::Selectable(bmdFiles[i].c_str(), selected)) {
        LoadObject(i);
      }
      if (selected && ImGui::IsWindowAppearing()) {
        ImGui::SetScrollHereY(0.5f);
      }
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (currentBMD) {
      ImGui::Text("Name: %s", currentBMD->Name.c_str());
      ImGui::Text("Meshes: %d", (int)currentBMD->Meshes.size());
      int totalVerts = 0, totalTris = 0;
      for (auto &m : currentBMD->Meshes) {
        totalVerts += m.NumVertices;
        totalTris += m.NumTriangles;
      }
      ImGui::Text("Vertices: %d", totalVerts);
      ImGui::Text("Triangles: %d", totalTris);
      ImGui::Text("Bones: %d", (int)currentBMD->Bones.size());
      ImGui::Text("Actions: %d", (int)currentBMD->Actions.size());

      ImGui::Separator();
      ImGui::Text("Textures:");
      for (auto &m : currentBMD->Meshes) {
        ImGui::BulletText("%s", m.TextureName.c_str());
      }
    } else {
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Failed to load");
    }

    // Animation controls
    if (currentIsAnimated) {
      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Animated");
      ImGui::Text("Keyframes: %d", currentNumKeys);
      ImGui::Checkbox("Play", &animationEnabled);
      ImGui::SliderFloat("Speed", &animSpeed, 0.5f, 20.0f, "%.1f k/s");
      float frameVal = currentAnimFrame;
      if (ImGui::SliderFloat("Frame", &frameVal, 0.0f,
                              (float)(currentNumKeys - 1), "%.1f")) {
        currentAnimFrame = frameVal;
      }
    }

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
        gifSkipSetting =
            25 / gifFpsSetting; // Assume 25fps render for skip calculation
        Screenshot::StartRecording(window, "screenshots/capture.gif",
                                   gifFrameTarget, 100 / gifFpsSetting,
                                   gifScaleSetting, gifSkipSetting - 1);
      }
    }

    ImGui::Separator();
    ImGui::TextWrapped("LMB drag: Rotate\nScroll: Zoom\nArrows: Prev/Next");

    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  // --- GLFW Callbacks ---

  static void ScrollCallback(GLFWwindow *w, double xoff, double yoff) {
    auto *self = static_cast<ObjectBrowser *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_ScrollCallback(w, xoff, yoff);
    if (ImGui::GetIO().WantCaptureMouse)
      return;

    self->orbitDistance -= (float)yoff * self->orbitDistance * 0.15f;
    self->orbitDistance = glm::clamp(self->orbitDistance, 1.0f, 50000.0f);
  }

  static void MouseButtonCallback(GLFWwindow *w, int button, int action,
                                  int mods) {
    auto *self = static_cast<ObjectBrowser *>(glfwGetWindowUserPointer(w));
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
    auto *self = static_cast<ObjectBrowser *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_CursorPosCallback(w, x, y);

    if (self->dragging && !ImGui::GetIO().WantCaptureMouse) {
      float dx = (float)(x - self->lastMouseX);
      float dy = (float)(y - self->lastMouseY);
      self->lastMouseX = x;
      self->lastMouseY = y;

      self->orbitYaw += dx * 0.3f;
      self->orbitPitch += dy * 0.3f;
      self->orbitPitch = glm::clamp(self->orbitPitch, -89.0f, -5.0f);
    }
  }

  static void KeyCallback(GLFWwindow *w, int key, int scancode, int action,
                          int mods) {
    auto *self = static_cast<ObjectBrowser *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);
    if (ImGui::GetIO().WantCaptureKeyboard)
      return;

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
      if (key == GLFW_KEY_LEFT || key == GLFW_KEY_UP) {
        int newIdx = (self->currentIndex - 1 + (int)self->bmdFiles.size()) %
                     (int)self->bmdFiles.size();
        self->LoadObject(newIdx);
      }
      if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_DOWN) {
        int newIdx = (self->currentIndex + 1) % (int)self->bmdFiles.size();
        self->LoadObject(newIdx);
      }
      if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(w, true);
      }
    }
  }

  static void CharCallback(GLFWwindow *w, unsigned int c) {
    ImGui_ImplGlfw_CharCallback(w, c);
  }
};

int main(int argc, char **argv) {
  ObjectBrowser browser;
  browser.Run();
  return 0;
}
