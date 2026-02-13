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
  void Load(const TerrainData &data, int worldID,
            const std::string &data_path);
  void Render(const glm::mat4 &view, const glm::mat4 &projection, float time);
  void SetDebugMode(int mode) { debugMode = mode; }
  int GetDebugMode() const { return debugMode; }

private:
  void setupMesh(const std::vector<float> &heightmap,
                 const std::vector<glm::vec3> &lightmap);
  void setupShader();
  void setupTextures(const TerrainData &data, const std::string &base_path);

  int debugMode = 0;
  GLuint VAO, VBO, EBO;
  GLuint shaderProgram;
  GLuint tileTextureArray;
  GLuint layer1InfoMap, layer2InfoMap, alphaMap, attributeMap, symmetryMap,
      lightmapTex;
  size_t indexCount;
  int worldID;

  struct Vertex {
    glm::vec3 position;
    glm::vec2 texCoord;
    glm::vec3 color;
  };
};

#endif
