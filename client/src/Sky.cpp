#include "Sky.hpp"
#include "TextureLoader.hpp"
#include <cmath>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

// Generate a soft radial glow texture for the sun
static GLuint createSunTexture() {
  const int SIZE = 64;
  std::vector<uint8_t> pixels(SIZE * SIZE * 4);
  float center = SIZE * 0.5f;
  for (int y = 0; y < SIZE; y++) {
    for (int x = 0; x < SIZE; x++) {
      float dx = (x + 0.5f - center) / center;
      float dy = (y + 0.5f - center) / center;
      float dist = sqrtf(dx * dx + dy * dy);
      // Soft core + wider halo
      float core = std::max(0.0f, 1.0f - dist * 2.5f); // Bright center
      float halo = std::max(0.0f, 1.0f - dist);         // Wider glow
      halo *= halo;
      float brightness = core * 0.8f + halo * 0.4f;
      brightness = std::min(brightness, 1.0f);
      int idx = (y * SIZE + x) * 4;
      pixels[idx + 0] = (uint8_t)(255 * brightness);
      pixels[idx + 1] = (uint8_t)(240 * brightness);
      pixels[idx + 2] = (uint8_t)(200 * brightness);
      pixels[idx + 3] = (uint8_t)(255 * brightness);
    }
  }
  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SIZE, SIZE, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return tex;
}

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

  // Sun billboard: generate glow texture + simple quad VAO
  sunTexture = createSunTexture();
  {
    // Unit quad centered at origin: will be positioned via model matrix
    float sunVerts[] = {
        // pos (3)       uv (2)
        -1, -1, 0, 0, 0, //
         1, -1, 0, 1, 0, //
         1,  1, 0, 1, 1, //
        -1,  1, 0, 0, 1, //
    };
    unsigned int sunIdx[] = {0, 1, 2, 0, 2, 3};

    glGenVertexArrays(1, &sunVAO);
    glGenBuffers(1, &sunVBO);
    glGenBuffers(1, &sunEBO);
    glBindVertexArray(sunVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(sunVerts), sunVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sunEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sunIdx), sunIdx,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // Use attribute 2 (alpha) with a default value of 1.0
    glDisableVertexAttribArray(2);
    glVertexAttrib1f(2, 1.0f);
    glBindVertexArray(0);
  }

  std::cout << "[Sky] Initialized with " << SEGMENTS << " segments, radius "
            << RADIUS << std::endl;
}

void Sky::Render(const glm::mat4 &view, const glm::mat4 &projection,
                 const glm::vec3 &cameraPos, float luminosity) {
  if (!texture || !shader || indexCount == 0)
    return;

  shader->use();

  // Center the sky cylinder on the camera (horizontal position only)
  glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                    glm::vec3(cameraPos.x, 0.0f, cameraPos.z));
  shader->setMat4("model", model);
  shader->setMat4("view", view);
  shader->setMat4("projection", projection);
  shader->setFloat("luminosity", luminosity);
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

  // ─── Sun sprite ─────────────────────────────────────────────────
  // Main 5.2 RenderSun: position = camera + (-900, CameraViewFar*0.9, 550)
  // We place the sun at a fixed offset from camera, inside the sky cylinder
  if (sunVAO && sunTexture) {
    float sunScale = 400.0f; // Size of the sun billboard
    // Sun position: offset from camera (fixed direction in world space)
    glm::vec3 sunOffset(-900.0f, 550.0f, RADIUS * 0.85f);
    glm::vec3 sunPos = cameraPos + sunOffset;

    // Billboard: face camera by extracting right/up from inverse view
    glm::vec3 camRight =
        glm::vec3(view[0][0], view[1][0], view[2][0]); // View row 0
    glm::vec3 camUp =
        glm::vec3(view[0][1], view[1][1], view[2][1]); // View row 1

    glm::mat4 sunModel(1.0f);
    sunModel[0] = glm::vec4(camRight * sunScale, 0.0f);
    sunModel[1] = glm::vec4(camUp * sunScale, 0.0f);
    sunModel[2] = glm::vec4(glm::cross(camRight, camUp) * sunScale, 0.0f);
    sunModel[3] = glm::vec4(sunPos, 1.0f);

    shader->setMat4("model", sunModel);
    // Sun uses luminosity — dimmer at night, bright in day
    // Override fog color to black so sun renders as pure additive glow
    shader->setVec3("fogColor", 0.0f, 0.0f, 0.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sunTexture);

    // Additive blending for sun glow
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glBindVertexArray(sunVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    // Restore blend mode
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

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
  if (sunVAO)
    glDeleteVertexArrays(1, &sunVAO);
  if (sunVBO)
    glDeleteBuffers(1, &sunVBO);
  if (sunEBO)
    glDeleteBuffers(1, &sunEBO);
  if (sunTexture)
    glDeleteTextures(1, &sunTexture);
  VAO = VBO = EBO = texture = 0;
  sunVAO = sunVBO = sunEBO = sunTexture = 0;
  shader.reset();
}
