#ifndef VIEWER_COMMON_HPP
#define VIEWER_COMMON_HPP

#include "BMDStructs.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "TextureLoader.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

// Interleaved vertex for mesh upload (pos + normal + texcoord)
struct ViewerVertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 tex;
};

// Orbit camera for 3D model viewing (LMB drag rotate, scroll zoom)
struct OrbitCamera {
  float yaw = 45.0f;
  float pitch = -25.0f;
  float distance = 500.0f;
  glm::vec3 center{0.0f};

  glm::vec3 GetEyePosition() const;
  glm::mat4 GetViewMatrix() const;
};

// RGB XYZ debug axes (colored lines at origin)
struct DebugAxes {
  GLuint vao = 0, vbo = 0;
  GLuint program = 0;
  float length = 100.0f;

  void Init();
  void UpdateGeometry();
  void Draw(const glm::mat4 &mvp);
  void Cleanup();
};

// Compile the simple colored-line shader used by DebugAxes
GLuint CompileLineShader();

// Upload a BMD mesh to GPU with bone transforms applied to vertices.
// Appends one MeshBuffers to 'out'. Updates aabb with transformed positions.
void UploadMeshWithBones(const Mesh_t &mesh, const std::string &textureDir,
                         const std::vector<BoneWorldMatrix> &bones,
                         std::vector<MeshBuffers> &out, AABB &aabb,
                         bool dynamic = false);

// Re-skin an already-uploaded dynamic mesh with new bone matrices.
// Uses glBufferSubData (safe on macOS Metal).
void RetransformMeshWithBones(const Mesh_t &mesh,
                              const std::vector<BoneWorldMatrix> &bones,
                              MeshBuffers &mb);

// Delete GL resources for a list of MeshBuffers
void CleanupMeshBuffers(std::vector<MeshBuffers> &buffers);

// ImGui lifecycle helpers (pass installCallbacks=false; we do our own)
void InitImGui(GLFWwindow *window);
void ShutdownImGui();

// macOS: bring GLFW window to foreground (no-op on other platforms)
void ActivateMacOSApp();

#endif // VIEWER_COMMON_HPP
