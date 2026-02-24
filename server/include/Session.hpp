#ifndef MU_SESSION_HPP
#define MU_SESSION_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class Session {
public:
  explicit Session(int fd);
  ~Session();

  int GetFd() const { return m_fd; }
  bool IsAlive() const { return m_alive; }

  // Returns complete packets extracted from recv buffer
  // Each vector<uint8_t> is one complete MU packet
  std::vector<std::vector<uint8_t>> ReadPackets();

  // Queue data to send
  void Send(const void *data, size_t len);

  // Flush send buffer to socket. Returns false if connection lost.
  bool FlushSend();

  // Mark session for removal
  void Kill() { m_alive = false; }

  // Session state
  int accountId = 0;
  int characterId = 0;
  std::string characterName;
  uint8_t charClass = 0;
  bool inWorld = false;
  bool inCharSelect = true;

  // Cached combat stats (populated on char select / equip change)
  uint16_t strength = 0;
  uint16_t energy = 0;

  // Combat stats
  uint8_t classCode = 0; // 0=DW, 1=DK, 2=ELF, 3=MG
  int weaponDamageMin = 0;
  int weaponDamageMax = 0;
  int minMagicDamage = 0;
  int maxMagicDamage = 0;

  int attackSpeed = 0;
  int attackRate = 0;
  int defenseRate = 0;

  int totalDefense = 0;
  bool hasBow = false;
  bool hasTwoHandedWeapon = false;

  // Server-authoritative HP tracking (monsters stop attacking dead players)
  int hp = 0;
  int maxHp = 0;
  int mana = 0;
  int maxMana = 0;
  int ag = 0;
  int maxAg = 0;
  bool dead = false;

  // Full character stats (for stat allocation validation)
  uint16_t dexterity = 0;
  uint16_t vitality = 0;
  uint16_t level = 1;
  uint16_t levelUpPoints = 0;
  uint64_t experience = 0;

  // Equipment slots (12 slots: right hand, left hand, helm, armor, pants,
  // gloves, boots, wings, pet, pendant, ring1, ring2)
  static constexpr int NUM_EQUIP_SLOTS = 12;
  struct EquippedItem {
    uint8_t category = 0xFF; // 0xFF = empty
    uint8_t itemIndex = 0;
    uint8_t itemLevel = 0;
  };
  std::array<EquippedItem, NUM_EQUIP_SLOTS> equipment{};

  // Inventory bag (8x8 = 64 slots)
  struct InventoryItem {
    int16_t defIndex = -2; // -2=empty, matches primary slot
    uint8_t category = 0;
    uint8_t itemIndex = 0;
    uint8_t quantity = 0;
    uint8_t itemLevel = 0;
    bool occupied = false; // true if any part of an item is here
    bool primary = false;  // true if this is the top-left root slot
  };
  std::array<InventoryItem, 64> bag{};
  uint32_t zen = 0;

  // World position (updated from move packets, used for server AI aggro)
  float worldX = 0.0f;
  float worldZ = 0.0f;

  // Potion cooldown timer (seconds)
  float potionCooldown = 0.0f;
  float hpRemainder = 0.0f; // Fractional HP for safe zone regeneration
  int8_t skillBar[10];
  int16_t potionBar[4];
  int8_t rmcSkillId = -1;
  int shopNpcType = -1; // -1 means no shop is open

  // AG logic timers
  float agRegenTimer = 0.0f;
  uint32_t lastAgUseTime = 0;

  // Learned skills (skill IDs)
  std::vector<uint8_t> learnedSkills;

private:
  int m_fd;
  bool m_alive = true;

  // Recv buffer — accumulates partial packets
  std::vector<uint8_t> m_recvBuf;

  // Send buffer — queued outgoing data
  std::vector<uint8_t> m_sendBuf;
};

#endif // MU_SESSION_HPP
