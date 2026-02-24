#ifndef MU_CHARACTER_SELECT_HPP
#define MU_CHARACTER_SELECT_HPP

#include "ServerConnection.hpp"
#include "Shader.hpp"
#include "Terrain.hpp"
#include "TerrainParser.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include <cstdint>
#include <functional>
#include <string>

struct GLFWwindow;
struct ImDrawList;

namespace CharacterSelect {

static constexpr int MAX_SLOTS = 5;

struct CharSlotEquip {
  uint8_t category = 0xFF; // 0xFF = empty/default
  uint8_t itemIndex = 0;
};

struct CharSlot {
  bool occupied = false;
  char name[11] = {};
  uint8_t classCode = 0; // 0=DW, 16=DK, 32=ELF, 48=MG
  uint16_t level = 0;
  // Equipment appearance from charSet encoding
  // 0=rightHand, 1=leftHand, 2=helm, 3=armor, 4=pants, 5=gloves, 6=boots
  CharSlotEquip equip[7];
};

struct Context {
  ServerConnection *server;
  std::string dataPath;
  GLFWwindow *window = nullptr;
  // Callback to transition game state when character is selected
  std::function<void()> onCharSelected;
  // Callback for exit/quit
  std::function<void()> onExit;
};

void Init(const Context &ctx);
void Shutdown();

// Called when server sends character list (F3:00)
void SetCharacterList(const CharSlot *slots, int count);

// Called when server sends create result (F3:01)
void OnCreateResult(uint8_t result, const char *name, uint8_t slot,
                    uint8_t classCode);

// Called when server sends delete result (F3:02)
void OnDeleteResult(uint8_t result);

void Update(float dt);
void Render(int windowWidth, int windowHeight);

// Mouse/keyboard input
void OnMouseClick(double screenX, double screenY, int windowWidth,
                  int windowHeight);
void OnKeyPress(int key);
void OnCharInput(unsigned int codepoint);

// Query if creation modal is open (for input routing)
bool IsCreateModalOpen();

} // namespace CharacterSelect

#endif // MU_CHARACTER_SELECT_HPP
