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
    PSBMSG_HEAD h; // C1:05:F1:00
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
    PSBMSG_HEAD h; // C1:05:F1:01
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
    uint8_t indexH;     // Object index high (bit 7 = create flag)
    uint8_t indexL;     // Object index low
    uint8_t typeH;      // NPC type high
    uint8_t typeL;      // NPC type low
    uint8_t x;          // Grid X
    uint8_t y;          // Grid Y
    uint8_t tx;         // Target X (same as x for static)
    uint8_t ty;         // Target Y (same as y for static)
    uint8_t dirAndPk;   // (dir << 4) | pkLevel
};

// --- Client → Server: Movement (0xD4) ---
struct PMSG_MOVE_RECV {
    PBMSG_HEAD h; // C1:0xD4
    uint8_t x;
    uint8_t y;
    uint8_t path[8];
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
struct PMSG_EQUIPMENT_HEAD {
    PBMSG_HEAD h; // C1:0x24
    uint8_t count; // Number of equipped slots
};

struct PMSG_EQUIPMENT_SLOT {
    uint8_t slot;        // EquipSlot (0=right_hand, 1=left_hand, etc.)
    uint8_t category;    // ItemCategory (0=Sword, 1=Axe, etc.)
    uint8_t itemIndex;   // Index within category (0=Sword01, 1=Sword02)
    uint8_t itemLevel;   // Enhancement level (+0 to +15)
    // Model file name (null-terminated, 32 chars max)
    char modelFile[32];
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

inline PSBMSG_HEAD MakeC1SubHeader(uint8_t size, uint8_t headcode, uint8_t subcode) {
    return {0xC1, size, headcode, subcode};
}

inline PWMSG_HEAD MakeC2Header(uint16_t size, uint8_t headcode) {
    return {0xC2, static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size & 0xFF), headcode};
}

// BUX decode for account/password
inline void BuxDecode(char *data, int len) {
    static const uint8_t buxCode[3] = {0xFC, 0xCF, 0xAB};
    for (int i = 0; i < len; i++) {
        data[i] ^= buxCode[i % 3];
    }
}

#endif // MU_PACKET_DEFS_HPP
