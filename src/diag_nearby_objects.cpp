#include "TerrainParser.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
  std::string data_path = "Data";
  TerrainData td = TerrainParser::LoadWorld(1, data_path);

  // Target object 1898 pos
  glm::vec3 targetPos = td.objects[1898].position;
  float radius = 1000.0f;

  std::cout << "Target Object 1898 Type: " << td.objects[1898].type
            << " Pos: " << targetPos.x << ", " << targetPos.y << ", "
            << targetPos.z << std::endl;
  std::cout << "Nearby Objects:" << std::endl;

  struct ObjDist {
    int idx;
    int type;
    float dist;
    glm::vec3 pos;
  };
  std::vector<ObjDist> nearby;

  for (int i = 0; i < (int)td.objects.size(); ++i) {
    float d = glm::distance(td.objects[i].position, targetPos);
    if (d < radius) {
      nearby.push_back({i, td.objects[i].type, d, td.objects[i].position});
    }
  }

  std::sort(nearby.begin(), nearby.end(),
            [](const ObjDist &a, const ObjDist &b) { return a.dist < b.dist; });

  for (const auto &o : nearby) {
    std::cout << "  idx=" << o.idx << " type=" << o.type << " dist=" << o.dist
              << " pos=(" << o.pos.x << "," << o.pos.y << "," << o.pos.z << ")"
              << std::endl;
  }

  return 0;
}
