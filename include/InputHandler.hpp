#ifndef MU_INPUT_HANDLER_HPP
#define MU_INPUT_HANDLER_HPP

#include <cstdint>

struct GLFWwindow;
class Camera;
class HeroCharacter;
class ServerConnection;
class ClickEffect;
class NpcManager;
class MonsterManager;
struct GroundItem;
struct UICoords;

struct InputContext {
    HeroCharacter *hero;
    Camera *camera;
    ClickEffect *clickEffect;
    ServerConnection *server;
    MonsterManager *monsterMgr;
    NpcManager *npcMgr;
    GroundItem *groundItems;
    int maxGroundItems;
    UICoords *hudCoords;
    bool *showCharInfo;
    bool *showInventory;
    int *hoveredNpc;
    int *hoveredMonster;
    int *hoveredGroundItem;
    int *selectedNpc;
    int16_t *quickSlotDefIndex;
    bool *shopOpen;
};

namespace InputHandler {
    void Init(const InputContext &ctx);
    void RegisterCallbacks(GLFWwindow *window);
    void ProcessInput(GLFWwindow *window, float deltaTime);
}

#endif // MU_INPUT_HANDLER_HPP
