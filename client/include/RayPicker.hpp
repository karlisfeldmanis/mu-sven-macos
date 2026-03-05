#ifndef MU_RAY_PICKER_HPP
#define MU_RAY_PICKER_HPP

#include "ClientTypes.hpp"
#include "TerrainParser.hpp"
#include <glm/glm.hpp>

struct GLFWwindow;
class Camera;
class NpcManager;
class MonsterManager;
class ObjectRenderer;

namespace RayPicker {

void Init(const TerrainData *td, Camera *cam, NpcManager *npcs,
          MonsterManager *monsters, GroundItem *groundItems, int maxGroundItems,
          ObjectRenderer *objRenderer = nullptr);

float GetTerrainHeight(float worldX, float worldZ);
bool IsWalkable(float worldX, float worldZ);
bool ScreenToTerrain(GLFWwindow *window, double mouseX, double mouseY,
                     glm::vec3 &outWorld);
int PickNpc(GLFWwindow *window, double mouseX, double mouseY);
int PickMonster(GLFWwindow *window, double mouseX, double mouseY);
int PickGroundItem(GLFWwindow *window, double mouseX, double mouseY);
// Pick interactive world objects (chairs, pose boxes) — returns index into
// ObjectRenderer::GetInteractiveObjects() or -1
int PickInteractiveObject(GLFWwindow *window, double mouseX, double mouseY);

} // namespace RayPicker

#endif // MU_RAY_PICKER_HPP
