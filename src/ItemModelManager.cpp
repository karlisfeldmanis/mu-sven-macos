#include "ItemModelManager.hpp"
#include "BMDUtils.hpp"
#include "ItemDatabase.hpp"
#include "Shader.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>

std::map<std::string, LoadedItemModel> ItemModelManager::s_cache;
Shader *ItemModelManager::s_shader = nullptr;
std::string ItemModelManager::s_dataPath;

void ItemModelManager::Init(Shader *shader, const std::string &dataPath) {
  s_shader = shader;
  s_dataPath = dataPath;
}

static void UploadStaticMesh(const Mesh_t &mesh, const std::string &texPath,
                             const std::vector<BoneWorldMatrix> &bones,
                             const std::string &modelFile,
                             std::vector<MeshBuffers> &outBuffers) {
  MeshBuffers mb;
  mb.isDynamic = false;

  // Resolve texture
  auto texInfo = TextureLoader::ResolveWithInfo(texPath, mesh.TextureName);
  mb.texture = texInfo.textureID;
  mb.hasAlpha = texInfo.hasAlpha;

  // Parse script flags from texture name
  auto flags = TextureLoader::ParseScriptFlags(mesh.TextureName);
  mb.bright = flags.bright;
  mb.hidden = flags.hidden;
  mb.noneBlend = flags.noneBlend;

  // Force additive blending for Wings and specific pets to hide black JPEG
  // backgrounds
  {
    std::string texLower = mesh.TextureName;
    std::transform(texLower.begin(), texLower.end(), texLower.begin(),
                   ::tolower);
    std::string modelLower = modelFile;
    std::transform(modelLower.begin(), modelLower.end(), modelLower.begin(),
                   ::tolower);

    if (texLower.find("wing") != std::string::npos ||
        modelLower.find("wing") != std::string::npos ||
        texLower.find("fairy2") != std::string::npos ||
        texLower.find("satan2") != std::string::npos ||
        texLower.find("unicon01") != std::string::npos ||
        texLower.find("flail00") != std::string::npos) {
      mb.bright = true;
    }
  }

  if (mb.hidden)
    return;

  // Expand vertices per-triangle-corner (matching ObjectRenderer::UploadMesh).
  // BMD stores separate VertexIndex, NormalIndex, TexCoordIndex per triangle
  // corner — we must create a unique vertex for each corner to preserve
  // per-face normals and UVs.
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
    int startIdx = (int)vertices.size();

    // First triangle (0,1,2)
    for (int v = 0; v < 3; ++v) {
      Vertex vert;
      auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
      auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

      int boneIdx = srcVert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        const auto &bm = bones[boneIdx];
        vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                          srcVert.Position);
        vert.normal =
            MuMath::RotateVector((const float(*)[4])bm.data(), srcNorm.Normal);
      } else {
        vert.pos = srcVert.Position;
        vert.normal = srcNorm.Normal;
      }

      vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                           mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
      vertices.push_back(vert);
      indices.push_back(startIdx + v);
    }

    // Second triangle for quads (0,2,3)
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
        indices.push_back((int)vertices.size() - 1);
      }
    }
  }

  mb.vertexCount = (int)vertices.size();
  mb.indexCount = (int)indices.size();

  if (mb.indexCount == 0) {
    outBuffers.push_back(mb);
    return;
  }

  // Upload to GPU
  glGenVertexArrays(1, &mb.vao);
  glGenBuffers(1, &mb.vbo);
  glGenBuffers(1, &mb.ebo);

  glBindVertexArray(mb.vao);

  glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mb.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  // Layout: Pos(3) + Norm(3) + UV(2) = 8 floats stride
  GLsizei stride = sizeof(Vertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                        (void *)(sizeof(float) * 3));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                        (void *)(sizeof(float) * 6));

  glBindVertexArray(0);
  outBuffers.push_back(mb);
}

LoadedItemModel *ItemModelManager::Get(const std::string &filename) {
  if (filename.empty())
    return nullptr;
  auto it = s_cache.find(filename);
  if (it != s_cache.end())
    return &it->second;

  // Load new — try Item/ first, then Player/ (armor models live there)
  LoadedItemModel model;
  std::string foundDir = "Item"; // default
  const char *searchDirs[] = {"Item", "Player"};
  for (const char *dir : searchDirs) {
    std::string path = s_dataPath + "/" + dir + "/" + filename;
    model.bmd = BMDParser::Parse(path);
    if (model.bmd) {
      foundDir = dir;
      break;
    }
  }
  if (!model.bmd) {
    std::cerr << "[Item] Failed to load " << filename
              << " (searched Item/ and Player/)" << std::endl;
    s_cache[filename] = {}; // Cache empty to avoid retry
    return nullptr;
  }

  // Compute static bind pose
  auto bones =
      ComputeBoneMatrices(model.bmd.get(), 0, 0); // Action 0, Frame 0
  std::string texPath = s_dataPath + "/" + foundDir + "/";

  // Compute transformed AABB from bone-transformed vertices
  glm::vec3 tMin(1e9f), tMax(-1e9f);
  for (const auto &mesh : model.bmd->Meshes) {
    UploadStaticMesh(mesh, texPath, bones, filename, model.meshes);
    // Accumulate AABB from transformed positions
    for (int vi = 0; vi < (int)mesh.Vertices.size(); ++vi) {
      glm::vec3 pos = mesh.Vertices[vi].Position;
      int boneIdx = mesh.Vertices[vi].Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        pos = MuMath::TransformPoint((const float(*)[4])bones[boneIdx].data(),
                                     pos);
      }
      tMin = glm::min(tMin, pos);
      tMax = glm::max(tMax, pos);
    }
  }
  model.transformedMin = tMin;
  model.transformedMax = tMax;

  s_cache[filename] = std::move(model);
  return &s_cache[filename];
}

void ItemModelManager::RenderItemUI(const std::string &modelFile,
                                    int16_t defIndex, int x, int y, int w,
                                    int h, bool hovered) {
  LoadedItemModel *model = Get(modelFile);
  if (!model || !model->bmd)
    return;

  // Preserve GL state
  GLint lastViewport[4];
  glGetIntegerv(GL_VIEWPORT, lastViewport);
  GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);

  // Setup viewport + scissor (glClear respects scissor, not viewport)
  glViewport(x, y, w, h); // Note: y is from bottom in GL
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, w, h);
  glEnable(GL_DEPTH_TEST);
  glClear(GL_DEPTH_BUFFER_BIT); // Clear depth only for this slot

  // Check shader
  Shader *shader = s_shader;
  if (!shader)
    return;
  shader->use();

  // Auto-fit camera/model based on bone-transformed AABB
  glm::vec3 min = model->transformedMin;
  glm::vec3 max = model->transformedMax;
  glm::vec3 size = max - min;
  glm::vec3 center = (min + max) * 0.5f;
  float maxDim = std::max(std::max(size.x, size.y), size.z);
  if (maxDim < 1.0f)
    maxDim = 1.0f;

  // Use Orthographic projection for UI items to fill grid space perfectly
  float aspect = (float)w / (float)h;
  glm::mat4 proj = glm::ortho(-aspect, aspect, -1.0f, 1.0f, -100.0f, 100.0f);

  // Camera looking at origin
  glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 50.0f), glm::vec3(0, 0, 0),
                                glm::vec3(0, 1, 0));

  // Model Transformation
  glm::mat4 mod = glm::mat4(1.0f);

  // 1. Orientation to make the item "stand up" vertically in the grid
  auto &itemDefs = ItemDatabase::GetItemDefs();
  if (defIndex != -1) {
    int category = 0;
    auto it = itemDefs.find(defIndex);
    if (it != itemDefs.end()) {
      category = it->second.category;
    } else {
      category = defIndex / 32;
    }

    // 1. Orientation to make the item "stand up" vertically in the grid
    if (category <= 5) {
      // Weapons and Staffs (0-5): Use smart axis-detection to ensure they are
      // strictly vertical pointing UP.
      if (size.z >= size.x && size.z >= size.y) {
        mod = glm::rotate(mod, glm::radians(-90.0f), glm::vec3(1, 0, 0));
        if (size.x < size.y)
          mod = glm::rotate(mod, glm::radians(90.0f), glm::vec3(0, 1, 0));
      } else if (size.x >= size.y && size.x >= size.z) {
        mod = glm::rotate(mod, glm::radians(90.0f), glm::vec3(0, 0, 1));
        if (size.z < size.y)
          mod = glm::rotate(mod, glm::radians(90.0f), glm::vec3(0, 1, 0));
      } else {
        if (size.x < size.z)
          mod = glm::rotate(mod, glm::radians(90.0f), glm::vec3(0, 1, 0));
      }
    } else {
      // Other items (Shields 6, Armor 7-11, Wings 12, etc):
      // These are typically modeled lying flat. Use standard MU pose (-90 X).
      mod = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                        glm::vec3(1, 0, 0));
    }
  } else {
    // Zen/Default: Use -90 X to make the Zen coins/box stand up
    mod = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                      glm::vec3(1, 0, 0));
  }

  // 2. Consistent 360 spin around the GRID'S vertical axis (Y) on hover
  if (hovered) {
    float spin = (float)glfwGetTime() * 180.0f;
    // Apply spin AFTER orientation so it's always around the screen's Y axis
    mod =
        glm::rotate(glm::mat4(1.0f), glm::radians(spin), glm::vec3(0, 1, 0)) *
        mod;
  }

  // 3. Transformation order: Scale * (Spin * Orientation) * Translation
  // Scale to fit: map maxDim to ~1.8 (leaving small margin in 2.0 range)
  float scale = 1.8f / maxDim;
  mod = glm::scale(glm::mat4(1.0f), glm::vec3(scale)) * mod;

  // Center the model locally before any rotation
  mod = mod * glm::translate(glm::mat4(1.0f), -center);

  shader->setMat4("projection", proj);
  shader->setMat4("view", view);
  shader->setMat4("model", mod);
  // Set ALL lighting uniforms explicitly for UI — don't rely on stale
  // world-pass values
  shader->setVec3("lightPos", glm::vec3(0, 50, 50.0f));
  shader->setVec3("viewPos", glm::vec3(0, 0, 50.0f));
  shader->setVec3("lightColor",
                  glm::vec3(1.0f, 1.0f, 1.0f)); // Pure white light
  shader->setFloat("blendMeshLight", 1.0f);     // No mesh darkening
  shader->setVec3("terrainLight",
                  glm::vec3(1.0f, 1.0f, 1.0f)); // No terrain darkening
  shader->setFloat("luminosity", 1.0f);         // Full brightness
  shader->setInt("numPointLights", 0);          // No point lights in UI
  shader->setBool("useFog", false);             // No fog in UI
  shader->setFloat("objectAlpha", 1.0f);        // Fully opaque

  // Render — disable face culling for double-sided meshes (pet wings etc.)
  glDisable(GL_CULL_FACE);
  for (const auto &mb : model->meshes) {
    if (mb.hidden)
      continue;
    glBindVertexArray(mb.vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mb.texture);
    shader->setInt("diffuseMap", 0);
    shader->setBool("useTexture", true);
    shader->setVec3("colorTint", glm::vec3(1));

    // Alpha blend if needed
    if (mb.hasAlpha || mb.bright) {
      glEnable(GL_BLEND);
      glDepthMask(GL_FALSE); // Disable depth writes for transparent layers
      if (mb.bright)
        glBlendFunc(GL_ONE, GL_ONE); // Pure additive
      else
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
      glDisable(GL_BLEND);  // Opaque
      glDepthMask(GL_TRUE); // Enable depth writes
    }

    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    glDepthMask(GL_TRUE); // Restore state after draw
  }
  glEnable(GL_CULL_FACE);
  glBindVertexArray(0);

  // Restore
  glDisable(GL_SCISSOR_TEST);
  glViewport(lastViewport[0], lastViewport[1], lastViewport[2],
              lastViewport[3]);
  if (!depthTest)
    glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

void ItemModelManager::RenderItemWorld(const std::string &filename,
                                       const glm::vec3 &pos,
                                       const glm::mat4 &view,
                                       const glm::mat4 &proj, float scale,
                                       glm::vec3 rotation) {
  LoadedItemModel *model = Get(filename);
  if (!model || !model->bmd)
    return;

  Shader *shader = s_shader;
  if (!shader)
    return;
  shader->use();

  // Center the model using transformed AABB before rotating
  glm::vec3 tCenter = (model->transformedMin + model->transformedMax) * 0.5f;
  glm::mat4 mod = glm::translate(glm::mat4(1.0f), pos);

  // Apply resting rotation
  if (rotation.x != 0)
    mod = glm::rotate(mod, glm::radians(rotation.x), glm::vec3(1, 0, 0));
  if (rotation.y != 0)
    mod = glm::rotate(mod, glm::radians(rotation.y), glm::vec3(0, 1, 0));
  if (rotation.z != 0)
    mod = glm::rotate(mod, glm::radians(rotation.z), glm::vec3(0, 0, 1));

  mod = glm::scale(mod, glm::vec3(scale));
  mod = glm::translate(mod, -tCenter); // Center before rotate

  shader->setMat4("projection", proj);
  shader->setMat4("view", view);
  shader->setMat4("model", mod);
  shader->setVec3("colorTint", glm::vec3(1)); // Reset tint

  for (const auto &mb : model->meshes) {
    if (mb.hidden)
      continue;
    glBindVertexArray(mb.vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mb.texture);
    shader->setInt("diffuseMap", 0);
    shader->setBool("useTexture", true);

    if (mb.hasAlpha || mb.bright) {
      glEnable(GL_BLEND);
      if (mb.bright)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
      else
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
      glDisable(GL_BLEND);
    }

    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
  }
  glBindVertexArray(0);
  glDisable(GL_BLEND);
}
