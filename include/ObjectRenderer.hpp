#ifndef OBJECT_RENDERER_HPP
#define OBJECT_RENDERER_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "Shader.hpp"
#include "TerrainParser.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ObjectRenderer {
public:
  void Init();
  void LoadObjects(const std::vector<ObjectData> &objects,
                   const std::string &objectDir);
  void Render(const glm::mat4 &view, const glm::mat4 &projection,
              const glm::vec3 &cameraPos, float currentTime = 0.0f);
  void Cleanup();
  void SetPointLights(const std::vector<glm::vec3> &positions,
                      const std::vector<glm::vec3> &colors,
                      const std::vector<float> &ranges);

  int GetInstanceCount() const { return (int)instances.size(); }
  int GetModelCount() const { return (int)modelCache.size(); }

  struct ObjectInstance {
    int type;
    glm::mat4 modelMatrix;
  };
  const std::vector<ObjectInstance> &GetInstances() const { return instances; }

private:
  struct ModelCache {
    std::vector<MeshBuffers> meshBuffers;
    std::vector<BoneWorldMatrix> boneMatrices;
    int blendMeshTexId = -1; // BlendMesh texture ID for window light marking

    // Animation support
    bool isAnimated = false;
    std::unique_ptr<BMDData> bmdData; // retained for re-skinning animated types
    int numAnimationKeys = 0;
  };
  std::unordered_map<int, ModelCache> modelCache;

  // Animation state per animated model type (shared across instances)
  struct AnimState {
    float frame = 0.0f;
  };
  std::unordered_map<int, AnimState> animStates;
  float lastAnimTime = 0.0f;

  std::vector<ObjectInstance> instances;

  std::vector<glm::vec3> plPositions, plColors;
  std::vector<float> plRanges;
  int plCount = 0;

  std::unique_ptr<Shader> shader;

  void UploadMesh(const Mesh_t &mesh, const std::string &baseDir,
                  const std::vector<BoneWorldMatrix> &bones,
                  std::vector<MeshBuffers> &out, bool dynamic = false);

  void RetransformMesh(const Mesh_t &mesh,
                       const std::vector<BoneWorldMatrix> &bones,
                       MeshBuffers &mb);

  static std::string GetObjectBMDFilename(int type);
};

#endif // OBJECT_RENDERER_HPP
