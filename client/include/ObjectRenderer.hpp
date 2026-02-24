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
  // Generic naming: ObjectXX.bmd (for non-Lorencia maps like World74)
  // fallbackDir: if set, try Lorencia naming from this dir when generic fails
  void LoadObjectsGeneric(const std::vector<ObjectData> &objects,
                          const std::string &objectDir,
                          const std::string &fallbackDir = "");
  void Render(const glm::mat4 &view, const glm::mat4 &projection,
              const glm::vec3 &cameraPos, float currentTime = 0.0f);
  void Cleanup();
  void SetPointLights(const std::vector<glm::vec3> &positions,
                      const std::vector<glm::vec3> &colors,
                      const std::vector<float> &ranges);
  void SetTerrainLightmap(const std::vector<glm::vec3> &lightmap);
  void SetTerrainMapping(const MapData *mapping) { terrainMapping = mapping; }
  void SetTerrainHeightmap(const std::vector<float> &hm) { terrainHeightmap = hm; }
  void SetTypeAlpha(const std::unordered_map<int, float> &alphaMap);
  void SetLuminosity(float l) { m_luminosity = l; }
  void SetFogEnabled(bool e) { m_fogEnabled = e; }
  void SetFogColor(const glm::vec3 &c) { m_fogColor = c; }
  void SetFogRange(float near_, float far_) { m_fogNear = near_; m_fogFar = far_; }
  void SetTypeFilter(const std::vector<int> &types) { m_typeFilter.assign(types.begin(), types.end()); }

  int GetInstanceCount() const { return (int)instances.size(); }
  int GetModelCount() const { return (int)modelCache.size(); }

  struct ObjectInstance {
    int type;
    glm::mat4 modelMatrix;
    glm::vec3 terrainLight = glm::vec3(1.0f); // Lightmap color at object position
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

  std::vector<glm::vec3> terrainLightmap; // Copy of terrain lightmap for sampling
  const MapData *terrainMapping = nullptr; // For grass-on-tile filtering
  std::vector<float> terrainHeightmap;     // For grass height snapping
  std::unordered_map<int, float> typeAlphaMap; // Per-type alpha for roof hiding
  float m_luminosity = 1.0f;
  bool m_fogEnabled = true;
  glm::vec3 m_fogColor = glm::vec3(0.117f, 0.078f, 0.039f);
  float m_fogNear = 1500.0f;
  float m_fogFar = 3500.0f;
  std::vector<int> m_typeFilter; // If non-empty, only render these types

  // Bilinear sample terrain lightmap at world position
  glm::vec3 SampleTerrainLight(const glm::vec3 &worldPos) const;

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
