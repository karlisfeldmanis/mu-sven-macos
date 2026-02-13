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
              const glm::vec3 &cameraPos);
  void Cleanup();

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
  };
  std::unordered_map<int, ModelCache> modelCache;

  std::vector<ObjectInstance> instances;

  std::unique_ptr<Shader> shader;

  void UploadMesh(const Mesh_t &mesh, const std::string &baseDir,
                  const std::vector<BoneWorldMatrix> &bones,
                  std::vector<MeshBuffers> &out);

  static std::string GetObjectBMDFilename(int type);
};

#endif // OBJECT_RENDERER_HPP
