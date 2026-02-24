#ifndef TERRAIN_HPP
#define TERRAIN_HPP

#include "TerrainParser.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

class Terrain {
public:
  Terrain();
  ~Terrain();

  void Init(); // Added Init for OpenGL setup
  void Load(const TerrainData &data, int worldID, const std::string &data_path);
  void Render(const glm::mat4 &view, const glm::mat4 &projection, float time,
              const glm::vec3 &viewPos = glm::vec3(0.0f));
  void SetDebugMode(int mode) { debugMode = mode; }
  int GetDebugMode() const { return debugMode; }
  void SetPointLights(const std::vector<glm::vec3> &positions,
                      const std::vector<glm::vec3> &colors,
                      const std::vector<float> &ranges,
                      const std::vector<int> &objectTypes = {});
  void SetLuminosity(float l) { m_luminosity = l; }
  void SetFogColor(const glm::vec3 &c) { m_fogColor = c; }
  void SetFogRange(float near_, float far_) { m_fogNear = near_; m_fogFar = far_; }
  void SetFogHeight(float base, float fade) { m_fogHeightBase = base; m_fogHeightFade = fade; }

  // Physics helper
  float GetHeight(float x, float y);

private:
  void setupMesh(const std::vector<float> &heightmap,
                 const std::vector<glm::vec3> &lightmap);
  void setupShader();
  void setupTextures(const TerrainData &data, const std::string &base_path);
  void
  applyDynamicLights(); // CPU-side AddTerrainLight (matches original engine)

  int debugMode = 0;
  float m_luminosity = 1.0f;
  glm::vec3 m_fogColor = glm::vec3(0.117f, 0.078f, 0.039f); // Default: MU brown
  float m_fogNear = 1500.0f;
  float m_fogFar = 3500.0f;
  float m_fogHeightBase = -99999.0f; // Disabled by default
  float m_fogHeightFade = 1.0f;
  std::vector<glm::vec3> plPositions, plColors;
  std::vector<float> plRanges;
  std::vector<int> plObjectTypes; // Object type per light (for flicker)
  int plCount = 0;
  GLuint VAO, VBO, EBO;
  GLuint shaderProgram;
  GLuint tileTextureArray;
  GLuint layer1InfoMap, layer2InfoMap, alphaMap, attributeMap, symmetryMap,
      lightmapTex;
  size_t indexCount;

  // Cached uniform locations (avoid per-frame glGetUniformLocation)
  GLint u_model = -1, u_view = -1, u_projection = -1;
  GLint u_uTime = -1, u_debugMode = -1, u_luminosity = -1, u_viewPos = -1;
  GLint u_fogColor = -1, u_fogNear = -1, u_fogFar = -1;
  GLint u_fogHeightBase = -1, u_fogHeightFade = -1;
  GLint u_tileTextures = -1, u_layer1Map = -1, u_layer2Map = -1;
  GLint u_alphaMap = -1, u_attributeMap = -1, u_symmetryMap = -1, u_lightMap = -1;
  int worldID;

  // Heightmap for physics
  std::vector<float> m_heightmap;

  // CPU-side dynamic lightmap (matches original AddTerrainLight system)
  std::vector<float> m_baselineLightRGB; // Baked lightmap (reset source)
  std::vector<float>
      m_workingLightRGB; // Modified per-frame with dynamic lights

  struct Vertex {
    glm::vec3 position;
    glm::vec2 texCoord;
    glm::vec3 color;
  };
};

#endif
