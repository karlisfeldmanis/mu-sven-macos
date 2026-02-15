#include "ViewerCommon.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <cmath>

// ---------- Line shader GLSL ----------

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

GLuint CompileLineShader() {
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

// ---------- OrbitCamera ----------

glm::vec3 OrbitCamera::GetEyePosition() const {
  float yawRad = glm::radians(yaw);
  float pitchRad = glm::radians(pitch);

  glm::vec3 offset;
  offset.x = distance * cos(pitchRad) * cos(yawRad);
  offset.y = -distance * sin(pitchRad);
  offset.z = distance * cos(pitchRad) * sin(yawRad);

  return center + offset;
}

glm::mat4 OrbitCamera::GetViewMatrix() const {
  return glm::lookAt(GetEyePosition(), center, glm::vec3(0, 1, 0));
}

// ---------- DebugAxes ----------

void DebugAxes::Init() {
  program = CompileLineShader();

  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
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

void DebugAxes::UpdateGeometry() {
  float L = length;
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
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
}

void DebugAxes::Draw(const glm::mat4 &mvp) {
  if (!program)
    return;
  glUseProgram(program);
  glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE,
                     &mvp[0][0]);
  glLineWidth(2.0f);
  glBindVertexArray(vao);
  glDrawArrays(GL_LINES, 0, 6);
  glBindVertexArray(0);
}

void DebugAxes::Cleanup() {
  if (program) {
    glDeleteProgram(program);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    program = 0;
    vao = vbo = 0;
  }
}

// ---------- Mesh upload / retransform ----------

void UploadMeshWithBones(const Mesh_t &mesh, const std::string &textureDir,
                         const std::vector<BoneWorldMatrix> &bones,
                         std::vector<MeshBuffers> &out, AABB &aabb,
                         bool dynamic) {
  MeshBuffers mb;
  mb.texture = 0;

  std::vector<ViewerVertex> vertices;
  std::vector<unsigned int> indices;

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    int startIdx = vertices.size();
    for (int v = 0; v < 3; ++v) {
      ViewerVertex vert;
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
      indices.push_back(startIdx + v);

      aabb.min = glm::min(aabb.min, vert.pos);
      aabb.max = glm::max(aabb.max, vert.pos);
    }
    if (steps == 4) {
      int quadIndices[3] = {0, 2, 3};
      for (int v : quadIndices) {
        ViewerVertex vert;
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
        indices.push_back(vertices.size() - 1);
      }
    }
  }

  mb.indexCount = indices.size();
  mb.vertexCount = vertices.size();
  mb.isDynamic = dynamic;
  if (mb.indexCount == 0) {
    out.push_back(mb);
    return;
  }

  GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;

  glGenVertexArrays(1, &mb.vao);
  glGenBuffers(1, &mb.vbo);
  glGenBuffers(1, &mb.ebo);

  glBindVertexArray(mb.vao);
  glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(ViewerVertex),
               vertices.data(), usage);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mb.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ViewerVertex),
                        (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ViewerVertex),
                        (void *)(sizeof(float) * 3));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ViewerVertex),
                        (void *)(sizeof(float) * 6));
  glEnableVertexAttribArray(2);

  // Load texture
  auto texResult = TextureLoader::ResolveWithInfo(textureDir, mesh.TextureName);
  mb.texture = texResult.textureID;
  mb.hasAlpha = texResult.hasAlpha;

  // Parse texture script flags
  auto scriptFlags = TextureLoader::ParseScriptFlags(mesh.TextureName);
  mb.noneBlend = scriptFlags.noneBlend;
  mb.hidden = scriptFlags.hidden;
  mb.bright = scriptFlags.bright;

  mb.bmdTextureId = mesh.Texture;

  out.push_back(mb);
}

void RetransformMeshWithBones(const Mesh_t &mesh,
                              const std::vector<BoneWorldMatrix> &bones,
                              MeshBuffers &mb) {
  if (!mb.isDynamic || mb.vertexCount == 0 || mb.vbo == 0)
    return;

  std::vector<ViewerVertex> vertices;
  vertices.reserve(mb.vertexCount);

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    for (int v = 0; v < 3; ++v) {
      ViewerVertex vert;
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
        ViewerVertex vert;
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
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(ViewerVertex),
                  vertices.data());
}

// ---------- Mesh cleanup ----------

void CleanupMeshBuffers(std::vector<MeshBuffers> &buffers) {
  for (auto &mb : buffers) {
    if (mb.vao)
      glDeleteVertexArrays(1, &mb.vao);
    if (mb.vbo)
      glDeleteBuffers(1, &mb.vbo);
    if (mb.ebo)
      glDeleteBuffers(1, &mb.ebo);
    if (mb.texture)
      glDeleteTextures(1, &mb.texture);
  }
  buffers.clear();
}

// ---------- ImGui ----------

void InitImGui(GLFWwindow *window) {
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

// ---------- macOS activation ----------

#ifdef __APPLE__
#include <objc/message.h>
#include <objc/runtime.h>
#endif

void ActivateMacOSApp() {
#ifdef __APPLE__
  id app =
      ((id(*)(id, SEL))objc_msgSend)((id)objc_getClass("NSApplication"),
                                     sel_registerName("sharedApplication"));
  ((void (*)(id, SEL, long))objc_msgSend)(
      app, sel_registerName("setActivationPolicy:"),
      0); // NSApplicationActivationPolicyRegular
  ((void (*)(id, SEL, BOOL))objc_msgSend)(
      app, sel_registerName("activateIgnoringOtherApps:"), YES);
#endif
}
