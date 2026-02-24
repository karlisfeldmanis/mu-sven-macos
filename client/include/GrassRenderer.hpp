#ifndef GRASS_RENDERER_HPP
#define GRASS_RENDERER_HPP

#include "TerrainParser.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class GrassRenderer {
public:
  struct PushSource {
    glm::vec3 pos;
    float radius;
  };

  void Init();
  void Load(const TerrainData &data, int worldID, const std::string &dataPath);
  void Render(const glm::mat4 &view, const glm::mat4 &projection, float time,
              const glm::vec3 &viewPos,
              const std::vector<PushSource> &pushSources = {});
  void Cleanup();

  void SetFogColor(const glm::vec3 &c) { fogColor = c; }
  void SetFogRange(float near_, float far_) { fogNear = near_; fogFar = far_; }

private:
  GLuint VAO = 0, VBO = 0, EBO = 0;
  GLuint shaderProgram = 0;
  GLuint grassTextures[3] = {};
  int indexCount = 0;

  struct GrassVertex {
    glm::vec3 position;
    glm::vec2 texCoord;
    float windWeight;
    float gridX;
    glm::vec3 color;
    float texLayer;
  };

  void setupShader();

  glm::vec3 fogColor = glm::vec3(0.117f, 0.078f, 0.039f); // Default: MU brown
  float fogNear = 1500.0f;
  float fogFar = 3500.0f;

  // Cached uniform locations
  GLint u_view = -1, u_projection = -1, u_uTime = -1, u_viewPos = -1;
  GLint u_fogColor = -1, u_fogNear = -1, u_fogFar = -1;
  GLint u_numPushers = -1;
  GLint u_pushPos[17] = {}, u_pushRadius[17] = {};
  GLint u_grassTex[3] = {};
};

#endif // GRASS_RENDERER_HPP
