#include "TerrainUtils.hpp"

namespace TerrainUtils {

glm::vec3 SampleLightAt(const std::vector<glm::vec3> &lightmap,
                         const glm::vec3 &worldPos) {
  const int SIZE = 256;
  if (lightmap.size() < (size_t)(SIZE * SIZE))
    return glm::vec3(1.0f);

  float gz = worldPos.x / 100.0f;
  float gx = worldPos.z / 100.0f;
  int xi = (int)gx, zi = (int)gz;
  if (xi < 0 || zi < 0 || xi > SIZE - 2 || zi > SIZE - 2)
    return glm::vec3(0.5f);

  float xd = gx - (float)xi, zd = gz - (float)zi;
  const glm::vec3 &c00 = lightmap[zi * SIZE + xi];
  const glm::vec3 &c10 = lightmap[zi * SIZE + (xi + 1)];
  const glm::vec3 &c01 = lightmap[(zi + 1) * SIZE + xi];
  const glm::vec3 &c11 = lightmap[(zi + 1) * SIZE + (xi + 1)];
  glm::vec3 left = c00 + (c01 - c00) * zd;
  glm::vec3 right = c10 + (c11 - c10) * zd;
  return left + (right - left) * xd;
}

float GetHeight(const TerrainData *td, float worldX, float worldZ) {
  if (!td)
    return 0.0f;
  const int S = TerrainParser::TERRAIN_SIZE;
  float gz = worldX / 100.0f;
  float gx = worldZ / 100.0f;
  gz = std::clamp(gz, 0.0f, (float)(S - 2));
  gx = std::clamp(gx, 0.0f, (float)(S - 2));
  int xi = (int)gx, zi = (int)gz;
  float xd = gx - (float)xi, zd = gz - (float)zi;
  float h00 = td->heightmap[zi * S + xi];
  float h10 = td->heightmap[zi * S + (xi + 1)];
  float h01 = td->heightmap[(zi + 1) * S + xi];
  float h11 = td->heightmap[(zi + 1) * S + (xi + 1)];
  return h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
         h01 * (1 - xd) * zd + h11 * xd * zd;
}

uint8_t GetAttribute(const TerrainData *td, float worldX, float worldZ) {
  if (!td)
    return 0;
  const int S = TerrainParser::TERRAIN_SIZE;
  int gz = (int)(worldX / 100.0f);
  int gx = (int)(worldZ / 100.0f);
  gz = std::clamp(gz, 0, S - 1);
  gx = std::clamp(gx, 0, S - 1);
  int idx = gz * S + gx;
  if (idx < 0 || idx >= (int)td->mapping.attributes.size())
    return 0;
  return td->mapping.attributes[idx];
}

uint8_t GetLayer1(const TerrainData *td, float worldX, float worldZ) {
  if (!td)
    return 0;
  const int S = TerrainParser::TERRAIN_SIZE;
  int gz = (int)(worldX / 100.0f);
  int gx = (int)(worldZ / 100.0f);
  gz = std::clamp(gz, 0, S - 1);
  gx = std::clamp(gx, 0, S - 1);
  int idx = gz * S + gx;
  if (idx < 0 || idx >= (int)td->mapping.layer1.size())
    return 0;
  return td->mapping.layer1[idx];
}

} // namespace TerrainUtils
