#ifndef MU_PACKET_DEFS_HPP
#define MU_PACKET_DEFS_HPP

#include <cstdint>
#include <cstring>

#pragma pack(push, 1)

// --- Wire Headers ---

struct PBMSG_HEAD {
  uint8_t type;     // 0xC1
  uint8_t size;     // Total packet size
  uint8_t headcode; // Main opcode
};

struct PSBMSG_HEAD {
  uint8_t type;     // 0xC1
  uint8_t size;     // Total packet size
  uint8_t headcode; // Main opcode
  uint8_t subcode;  // Sub-opcode
};

struct PWMSG_HEAD {
  uint8_t type;     // 0xC2
  uint8_t sizeH;    // Size high byte (big-endian)
  uint8_t sizeL;    // Size low byte
  uint8_t headcode; // Main opcode
};

// --- Server → Client: Welcome (F1:00) ---
struct PMSG_WELCOME_SEND {
  PSBMSG_HEAD h;  // C1:05:F1:00
  uint8_t result; // 0x01 = success
};

// --- Client → Server: Login (F1:01) ---
struct PMSG_LOGIN_RECV {
  PSBMSG_HEAD h; // C1/C3:F1:01
  char account[10];
  char password[20];
  uint32_t tickCount;
  uint8_t version[5];
  uint8_t serial[16];
};

// --- Server → Client: Login Result (F1:01) ---
struct PMSG_LOGIN_RESULT_SEND {
  PSBMSG_HEAD h;  // C1:05:F1:01
  uint8_t result; // 0x01=success, 0x00=fail, 0x02=already connected
};

// --- Client → Server: Character List Request (F3:00) ---
// Just PSBMSG_HEAD with headcode=0xF3, subcode=0x00

// --- Server → Client: Character List (F3:00) ---
struct PMSG_CHARLIST_HEAD {
  PSBMSG_HEAD h; // C1:F3:00
  uint8_t classCode;
  uint8_t moveCnt;
  uint8_t count;
};

struct PMSG_CHARLIST_ENTRY {
  uint8_t slot;
  char name[10];
  uint16_t level; // big-endian on wire
  uint8_t ctlCode;
  uint8_t charSet[18];
  uint8_t guildStatus;
};

// --- Client → Server: Character Select (F3:03) ---
struct PMSG_CHARSELECT_RECV {
  PSBMSG_HEAD h; // C1:F3:03
  char name[10];
};

// --- Server → Client: Character Info (F3:03) ---
struct PMSG_CHARINFO_SEND {
  PSBMSG_HEAD h; // C1:F3:03
  uint8_t x;
  uint8_t y;
  uint8_t map;
  uint8_t dir;
  uint8_t experience[8];
  uint8_t nextExperience[8];
  uint16_t levelUpPoint;
  uint16_t strength;
  uint16_t dexterity;
  uint16_t vitality;
  uint16_t energy;
  uint16_t life;
  uint16_t maxLife;
  uint16_t mana;
  uint16_t maxMana;
  uint16_t shield;
  uint16_t maxShield;
  uint16_t bp;
  uint16_t maxBP;
  uint32_t money;
  uint8_t pkLevel;
  uint8_t ctlCode;
  uint16_t fruitAddPoint;
  uint16_t maxFruitAddPoint;
  uint16_t leadership;
  uint16_t fruitSubPoint;
  uint16_t maxFruitSubPoint;
};

// --- Server → Client: NPC Viewport (0x13) ---
struct PMSG_VIEWPORT_HEAD {
  PWMSG_HEAD h; // C2:0x13
  uint8_t count;
};

struct PMSG_VIEWPORT_NPC {
  uint8_t indexH;   // Object index high (bit 7 = create flag)
  uint8_t indexL;   // Object index low
  uint8_t typeH;    // NPC type high
  uint8_t typeL;    // NPC type low
  uint8_t x;        // Grid X
  uint8_t y;        // Grid Y
  uint8_t tx;       // Target X (same as x for static)
  uint8_t ty;       // Target Y (same as y for static)
  uint8_t dirAndPk; // (dir << 4) | pkLevel
};

// --- Server → Client: Monster Viewport (0x1F) ---
struct PMSG_MONSTER_VIEWPORT_HEAD {
  PBMSG_HEAD h; // C1:0x1F
  uint8_t count;
};

struct PMSG_MONSTER_VIEWPORT_ENTRY {
  uint8_t typeH; // Monster type high
  uint8_t typeL; // Monster type low
  uint8_t x;     // Grid X
  uint8_t y;     // Grid Y
  uint8_t dir;   // Direction (0-7)
};

// --- Client → Server: Movement (0xD4) ---
struct PMSG_MOVE_RECV {
  PBMSG_HEAD h; // C1:0xD4
  uint8_t x;
  uint8_t y;
  uint8_t path[8];
};

// --- Client → Server: Precise Position (0xD7) ---
// Float-precision position update for accurate monster AI distance checks
struct PMSG_PRECISE_POS_RECV {
  PBMSG_HEAD h; // C1:0xD7
  float worldX;
  float worldZ;
};

// --- Server → Client: Position Update (0x15) ---
struct PMSG_POSITION_SEND {
  PBMSG_HEAD h; // C1:0x15
  uint8_t indexH;
  uint8_t indexL;
  uint8_t x;
  uint8_t y;
};

// --- Server → Client: Character Equipment (0x24) ---
// Custom remaster packet: tells client what items are equipped
#pragma pack(push, 1)
struct PMSG_EQUIPMENT_HEAD {
  PWMSG_HEAD h;  // C2:0x24 (4 bytes)
  uint8_t count; // Number of equipped slots (1 byte)
};
#pragma pack(pop)
static_assert(sizeof(PMSG_EQUIPMENT_HEAD) == 5,
              "PMSG_EQUIPMENT_HEAD size mismatch");

#pragma pack(push, 1)
struct PMSG_EQUIPMENT_SLOT {
  uint8_t slot;      // EquipSlot (0=right_hand, 1=left_hand, etc.)
  uint8_t category;  // ItemCategory (0=Sword, 1=Axe, etc.)
  uint8_t itemIndex; // Index within category (0=Sword01, 1=Sword02)
  uint8_t itemLevel; // Enhancement level (+0 to +15)
  // Model file name (null-terminated, 32 chars max)
  char modelFile[32];
};
#pragma pack(pop)
static_assert(sizeof(PMSG_EQUIPMENT_SLOT) == 36,
              "PMSG_EQUIPMENT_SLOT size mismatch");

// --- Server → Client: Character Stats (0x25) ---
// Remaster packet: sends DK stats, level, XP on connect
struct PMSG_CHARSTATS_SEND {
  PBMSG_HEAD h; // C1:0x25
  uint16_t characterId;
  uint16_t level;
  uint16_t strength;
  uint16_t dexterity;
  uint16_t vitality;
  uint16_t energy;
  uint16_t life;
  uint16_t maxLife;
  uint16_t mana;
  uint16_t maxMana;
  uint16_t levelUpPoints;
  uint32_t experienceLo;
  uint32_t experienceHi;
  uint8_t charClass;
  int16_t quickSlotDefIndex;
};

// --- Client → Server: Equipment Change (0x27) ---
// Remaster packet: client tells server which item was equipped
struct PMSG_EQUIP_RECV {
  PBMSG_HEAD h; // C1:0x27
  uint16_t characterId;
  uint8_t slot;      // EquipSlot (0=right_hand, 1=left_hand, 3=armor, 6=boots)
  uint8_t category;  // ItemCategory
  uint8_t itemIndex; // Index within category
  uint8_t itemLevel; // Enhancement +0..+15
};

// --- Client → Server: Character Save (0x26) ---
// Remaster packet: client saves stats/XP/level on shutdown
struct PMSG_CHARSAVE_RECV {
  PBMSG_HEAD h; // C1:0x26
  uint16_t characterId;
  uint16_t level;
  uint16_t strength;
  uint16_t dexterity;
  uint16_t vitality;
  uint16_t energy;
  uint16_t life;
  uint16_t maxLife;
  uint16_t levelUpPoints;
  uint32_t experienceLo;
  uint32_t experienceHi;
  int16_t quickSlotDefIndex;
};

// --- Server → Client: Monster Viewport V2 (0x34) ---
// Enhanced viewport with index, HP, and state per monster
struct PMSG_MONSTER_VIEWPORT_ENTRY_V2 {
  uint8_t indexH; // Monster unique index high
  uint8_t indexL; // Monster unique index low
  uint8_t typeH;  // Monster type high
  uint8_t typeL;  // Monster type low
  uint8_t x;      // Grid X
  uint8_t y;      // Grid Y
  uint8_t dir;    // Direction (0-7)
  uint16_t hp;    // Current HP
  uint16_t maxHp; // Max HP
  uint8_t state;  // 0=alive, 1=dying, 2=dead
};

// --- Client → Server: Attack Request (0x28) ---
struct PMSG_ATTACK_RECV {
  PBMSG_HEAD h;          // C1:0x28
  uint16_t monsterIndex; // Target monster unique index
};

// --- Server → Client: Damage Result (0x29) ---
struct PMSG_DAMAGE_SEND {
  PBMSG_HEAD h; // C1:0x29
  uint16_t monsterIndex;
  uint16_t damage;
  uint8_t damageType; // 0=miss, 1=normal, 2=critical, 3=excellent
  uint16_t remainingHp;
  uint16_t attackerCharId; // Which player attacked
};

// --- Server → Client: Monster Death + XP (0x2A) ---
struct PMSG_MONSTER_DEATH_SEND {
  PBMSG_HEAD h; // C1:0x2A
  uint16_t monsterIndex;
  uint16_t killerCharId;
  uint32_t xpReward;
};

// --- Server → Client: Ground Drop Spawned (0x2B) ---
struct PMSG_DROP_SPAWN_SEND {
  PBMSG_HEAD h;       // C1:0x2B
  uint16_t dropIndex; // Unique drop ID
  int16_t defIndex;   // -1=Zen, 0-511+=item def index
  uint8_t quantity;
  uint8_t itemLevel; // Enhancement +0..+2
  float worldX;
  float worldZ;
};

// --- Client → Server: Pickup Request (0x2C) ---
struct PMSG_PICKUP_RECV {
  PBMSG_HEAD h; // C1:0x2C
  uint16_t dropIndex;
};

// --- Server → Client: Pickup Result (0x2D) ---
struct PMSG_PICKUP_RESULT_SEND {
  PBMSG_HEAD h; // C1:0x2D
  uint16_t dropIndex;
  int16_t defIndex; // -1=Zen, 0-511+=item def index
  uint8_t quantity;
  uint8_t itemLevel;
  uint8_t success; // 1=ok, 0=already taken
};

// --- Server → Client: Drop Removed (0x2E) ---
struct PMSG_DROP_REMOVE_SEND {
  PBMSG_HEAD h; // C1:0x2E
  uint16_t dropIndex;
};

// --- Server → Client: Monster Attack Player (0x2F) ---
struct PMSG_MONSTER_ATTACK_SEND {
  PBMSG_HEAD h; // C1:0x2F
  uint16_t monsterIndex;
  float damage;      // Changed from uint16_t to float
  float remainingHp; // Changed from uint16_t to float
};

// --- Server → Client: Monster Respawn (0x30) ---
struct PMSG_MONSTER_RESPAWN_SEND {
  PBMSG_HEAD h; // C1:0x30
  uint16_t monsterIndex;
  uint8_t x, y; // New grid position
  uint16_t hp;
};

// --- Server → Client: Monster Move/Chase (0x35) ---
// Server sends target grid cell; client smoothly moves monster there
struct PMSG_MONSTER_MOVE_SEND {
  PBMSG_HEAD h; // C1:0x35
  uint16_t monsterIndex;
  uint8_t targetX; // Target grid X (where monster is heading)
  uint8_t targetY; // Target grid Y
  uint8_t chasing; // 1=chasing player, 0=returning to spawn/idle
};

// --- Client → Server: Stat Allocation Request (0x37) ---
struct PMSG_STAT_ALLOC_RECV {
  PBMSG_HEAD h;     // C1:0x37
  uint8_t statType; // 0=STR, 1=DEX, 2=VIT, 3=ENE
};

// --- Server → Client: Stat Allocation Response (0x38) ---
struct PMSG_STAT_ALLOC_SEND {
  PBMSG_HEAD h;           // C1:0x38
  uint8_t result;         // 1=success, 0=fail (no points)
  uint8_t statType;       // Which stat was incremented
  uint16_t newValue;      // New stat value
  uint16_t levelUpPoints; // Remaining points
  uint16_t maxLife;       // Updated maxLife (VIT affects HP)
};

// --- Server → Client: Inventory Sync Item (0x36, C2 variable-length) ---
struct PMSG_INVENTORY_ITEM {
  uint8_t slot;      // 0-63
  uint8_t category;  // Category (0-15)
  uint8_t itemIndex; // Index (0-31)
  uint8_t quantity;
  uint8_t itemLevel;
};

// --- Client → Server: Inventory Move Request (0x39) ---
struct PMSG_INVENTORY_MOVE_RECV {
  PBMSG_HEAD h; // C1:0x39
  uint8_t fromSlot;
  uint8_t toSlot;
};

// --- Client → Server: Item Use Request (0x3A) ---
struct PMSG_ITEM_USE_RECV {
  PBMSG_HEAD h; // C1:0x3A
  uint8_t slot; // 0-63
};

#pragma pack(pop)

// --- Helper functions ---

inline void SetWordBE(uint8_t *dst, uint16_t val) {
  dst[0] = static_cast<uint8_t>(val >> 8);
  dst[1] = static_cast<uint8_t>(val & 0xFF);
}

inline uint16_t GetWordBE(const uint8_t *src) {
  return (static_cast<uint16_t>(src[0]) << 8) | src[1];
}

inline void SetDwordBE(uint8_t *dst, uint32_t val) {
  dst[0] = static_cast<uint8_t>(val >> 24);
  dst[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
  dst[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
  dst[3] = static_cast<uint8_t>(val & 0xFF);
}

inline PBMSG_HEAD MakeC1Header(uint8_t size, uint8_t headcode) {
  return {0xC1, size, headcode};
}

inline PSBMSG_HEAD MakeC1SubHeader(uint8_t size, uint8_t headcode,
                                   uint8_t subcode) {
  return {0xC1, size, headcode, subcode};
}

inline PWMSG_HEAD MakeC2Header(uint16_t size, uint8_t headcode) {
  return {0xC2, static_cast<uint8_t>(size >> 8),
          static_cast<uint8_t>(size & 0xFF), headcode};
}

// BUX decode for account/password
inline void BuxDecode(char *data, int len) {
  static const uint8_t buxCode[3] = {0xFC, 0xCF, 0xAB};
  for (int i = 0; i < len; i++) {
    data[i] ^= buxCode[i % 3];
  }
}

#endif // MU_PACKET_DEFS_HPP
