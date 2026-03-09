#ifndef MU_TERRAIN_UTILS_HPP
#define MU_TERRAIN_UTILS_HPP

#include "TerrainParser.hpp"
#include <algorithm>
#include <glm/glm.hpp>
#include <vector>

namespace TerrainUtils {

// Bilinear interpolation of 256x256 lightmap at world position
glm::vec3 SampleLightAt(const std::vector<glm::vec3> &lightmap,
                         const glm::vec3 &worldPos);

// Bilinear interpolation of heightmap at world position (returns Y)
float GetHeight(const TerrainData *td, float worldX, float worldZ);

// Discrete attribute lookup at world position
uint8_t GetAttribute(const TerrainData *td, float worldX, float worldZ);

// Discrete layer1 tile lookup at world position
uint8_t GetLayer1(const TerrainData *td, float worldX, float worldZ);

} // namespace TerrainUtils

#endif // MU_TERRAIN_UTILS_HPP
