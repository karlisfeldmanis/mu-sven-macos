#ifndef TERRAIN_PARSER_HPP
#define TERRAIN_PARSER_HPP

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct MapData {
  int map_number;
  std::vector<uint8_t> layer1;
  std::vector<uint8_t> layer2;
  std::vector<float> alpha;
  std::vector<uint8_t> attributes;
  std::vector<uint8_t> symmetry;
};

struct ObjectData {
  int type;
  glm::vec3 position;
  glm::vec3 rotation; // Euler angles in radians
  float scale;
  glm::vec3 mu_pos_raw;
  glm::vec3 mu_angle_raw;
};

struct TerrainData {
  std::vector<float> heightmap;
  MapData mapping;
  std::vector<glm::vec3> lightmap;
  std::vector<ObjectData> objects;
};

class TerrainParser {
public:
  static const int TERRAIN_SIZE = 256;

  static TerrainData LoadWorld(int world_id, const std::string &data_path);

private:
  static std::vector<uint8_t> DecryptMapFile(const std::vector<uint8_t> &data);
  static std::vector<float> ParseHeightFile(const std::string &path);
  static MapData ParseMappingFile(const std::string &path);
  static std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
  ParseAttributesFile(const std::string &path);
  static std::vector<glm::vec3> ParseLightFile(const std::string &path);
  static std::vector<ObjectData> ParseObjectsFile(const std::string &path);
};

#endif
