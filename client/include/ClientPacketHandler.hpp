#ifndef MU_CLIENT_PACKET_HANDLER_HPP
#define MU_CLIENT_PACKET_HANDLER_HPP

#include "CharacterSelect.hpp"
#include "ClientTypes.hpp"
#include "HeroCharacter.hpp"
#include "ItemDatabase.hpp"
#include "MonsterManager.hpp"
#include "NpcManager.hpp"
#include "Terrain.hpp"
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <vector>

// Context struct that ClientPacketHandler uses to access game state.
// All pointers are owned by main.cpp — this is a non-owning view.
struct ClientGameState {
  HeroCharacter *hero = nullptr;
  MonsterManager *monsterManager = nullptr;
  NpcManager *npcManager = nullptr;
  VFXManager *vfxManager = nullptr;
  Terrain *terrain = nullptr;

  // Inventory (arrays owned by caller, size INVENTORY_SLOTS / 12)
  ClientInventoryItem *inventory = nullptr;
  ClientEquipSlot *equipSlots = nullptr;
  GroundItem *groundItems = nullptr;
  uint32_t *zen = nullptr;
  bool *syncDone = nullptr;

  bool *shopOpen = nullptr;
  std::vector<ShopItem> *shopItems = nullptr;
  std::map<int16_t, ClientItemDefinition> *itemDefs = nullptr;

  // Server-tracked character stats
  int *serverLevel = nullptr;
  int *serverHP = nullptr;
  int *serverMaxHP = nullptr;
  int *serverMP = nullptr;
  int *serverMaxMP = nullptr;
  int *serverAG = nullptr;
  int *serverMaxAG = nullptr;
  int *serverStr = nullptr;
  int *serverDex = nullptr;
  int *serverVit = nullptr;
  int *serverEne = nullptr;
  int *serverLevelUpPoints = nullptr;
  int64_t *serverXP = nullptr;
  int *serverDefense = nullptr;
  int *serverAttackSpeed = nullptr;
  int *serverMagicSpeed = nullptr;
  int16_t *potionBar = nullptr; // [3]
  int8_t *skillBar = nullptr;   // [10]
  int8_t *rmcSkillId = nullptr;
  int *heroCharacterId = nullptr;
  char *characterName = nullptr;
  std::vector<uint8_t> *learnedSkills = nullptr;

  // Callbacks for main.cpp-specific functionality
  std::function<void(const glm::vec3 &, int, uint8_t)> spawnDamageNumber;
  std::function<int(uint8_t)> getBodyPartIndex;
  std::function<std::string(uint8_t, uint8_t)> getBodyPartModelFile;
  std::function<void(int16_t, glm::vec3 &, float &)> getItemRestingAngle;
};

namespace ClientPacketHandler {

// Initialize with game state context (must be called before handling packets)
void Init(ClientGameState *state);

// Parse a single packet from the initial server data burst
void HandleInitialPacket(const uint8_t *pkt, int pktSize, ServerData &out);

// Handle ongoing game packets (monster AI, combat, drops, stats)
void HandleGamePacket(const uint8_t *pkt, int pktSize);

// Handle character select packets (F3:00 charlist, F3:01 create, F3:02 delete)
void HandleCharSelectPacket(const uint8_t *pkt, int pktSize);

} // namespace ClientPacketHandler

#endif // MU_CLIENT_PACKET_HANDLER_HPP
