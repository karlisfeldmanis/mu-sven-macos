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
};

#endif // GRASS_RENDERER_HPP
