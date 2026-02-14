#include "Sky.hpp"
#include "TextureLoader.hpp"
#include <cmath>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>

void Sky::Init(const std::string &dataPath) {
  // Load sky texture from Object63/sky.OZJ
  std::string skyTexPath = dataPath + "Object63/sky.OZJ";
  texture = TextureLoader::LoadOZJ(skyTexPath);
  if (!texture) {
    std::cerr << "[Sky] Failed to load sky texture: " << skyTexPath << std::endl;
    return;
  }

  // Set texture to repeat horizontally for seamless wrapping
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Build cylinder geometry: a ring of quads around the camera
  // Each vertex: position (3) + texcoord (2) + alpha (1)
  struct Vertex {
    glm::vec3 pos;
    glm::vec2 uv;
    float alpha;
  };

  std::vector<Vertex> vertices;
  std::vector<unsigned int> indices;

  for (int i = 0; i <= SEGMENTS; ++i) {
    float angle = (float)i / SEGMENTS * 2.0f * M_PI;
    float x = cosf(angle) * RADIUS;
    float z = sinf(angle) * RADIUS;
    float u = (float)i / SEGMENTS * 2.0f; // Repeat texture twice around

    // Bottom vertex (full opacity)
    vertices.push_back({{x, BAND_BOTTOM, z}, {u, 0.0f}, 1.0f});
    // Top vertex (fade out to transparent)
    vertices.push_back({{x, BAND_TOP, z}, {u, 1.0f}, 0.0f});
  }

  for (int i = 0; i < SEGMENTS; ++i) {
    int base = i * 2;
    // Two triangles per quad
    indices.push_back(base);
    indices.push_back(base + 1);
    indices.push_back(base + 2);

    indices.push_back(base + 1);
    indices.push_back(base + 3);
    indices.push_back(base + 2);
  }

  // Bottom cap disc: separate vertices with alpha=2.0 (shader renders as fog color)
  unsigned int capStart = vertices.size();
  vertices.push_back({{0.0f, BAND_BOTTOM, 0.0f}, {0.5f, 0.0f}, 2.0f}); // center
  for (int i = 0; i < SEGMENTS; ++i) {
    float angle = (float)i / SEGMENTS * 2.0f * M_PI;
    float x = cosf(angle) * RADIUS;
    float z = sinf(angle) * RADIUS;
    vertices.push_back({{x, BAND_BOTTOM, z}, {0.5f, 0.0f}, 2.0f});
  }
  for (int i = 0; i < SEGMENTS; ++i) {
    indices.push_back(capStart); // center
    indices.push_back(capStart + 1 + ((i + 1) % SEGMENTS));
    indices.push_back(capStart + 1 + i);
  }

  indexCount = indices.size();

  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);
  glGenBuffers(1, &EBO);

  glBindVertexArray(VAO);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  // position
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)0);
  glEnableVertexAttribArray(0);
  // texcoord
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)(sizeof(float) * 3));
  glEnableVertexAttribArray(1);
  // alpha
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)(sizeof(float) * 5));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);

  {
    std::ifstream test("shaders/sky.vert");
    if (test.good())
      shader = std::make_unique<Shader>("shaders/sky.vert", "shaders/sky.frag");
    else
      shader = std::make_unique<Shader>("../shaders/sky.vert",
                                        "../shaders/sky.frag");
  }

  std::cout << "[Sky] Initialized with " << SEGMENTS << " segments, radius "
            << RADIUS << std::endl;
}

void Sky::Render(const glm::mat4 &view, const glm::mat4 &projection,
                 const glm::vec3 &cameraPos) {
  if (!texture || !shader || indexCount == 0)
    return;

  shader->use();

  // Center the sky cylinder on the camera (horizontal position only)
  glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                    glm::vec3(cameraPos.x, 0.0f, cameraPos.z));
  shader->setMat4("model", model);
  shader->setMat4("view", view);
  shader->setMat4("projection", projection);
  shader->setVec3("fogColor", 0.117f, 0.078f, 0.039f);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  shader->setInt("skyTexture", 0);

  // Render with blending, no depth write (sky is behind everything)
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);

  glBindVertexArray(VAO);
  glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  glDepthMask(GL_TRUE);
}

void Sky::Cleanup() {
  if (VAO)
    glDeleteVertexArrays(1, &VAO);
  if (VBO)
    glDeleteBuffers(1, &VBO);
  if (EBO)
    glDeleteBuffers(1, &EBO);
  if (texture)
    glDeleteTextures(1, &texture);
  VAO = VBO = EBO = texture = 0;
  shader.reset();
}
