#ifndef SKY_HPP
#define SKY_HPP

#include "Shader.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>

class Sky {
public:
  void Init(const std::string &dataPath);
  void Render(const glm::mat4 &view, const glm::mat4 &projection,
              const glm::vec3 &cameraPos);
  void Cleanup();

private:
  std::unique_ptr<Shader> shader;
  GLuint VAO = 0, VBO = 0, EBO = 0;
  GLuint texture = 0;
  int indexCount = 0;

  static constexpr int SEGMENTS = 36;
  static constexpr float RADIUS = 3200.0f;
  static constexpr float BAND_BOTTOM = -1000.0f;
  static constexpr float BAND_TOP = 800.0f;
};

#endif
