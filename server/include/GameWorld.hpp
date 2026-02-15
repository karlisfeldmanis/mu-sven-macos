#ifndef MU_GAME_WORLD_HPP
#define MU_GAME_WORLD_HPP

#include "Database.hpp"
#include <cstdint>
#include <string>
#include <vector>

struct NpcSpawn {
    uint16_t index;  // Unique object index (1001+)
    uint16_t type;   // NPC type ID (253=Amy, 250=Merchant, etc.)
    uint8_t x;       // Grid X
    uint8_t y;       // Grid Y
    uint8_t dir;     // Facing direction (0-7)
    std::string name; // Display name (for logging)
};

class GameWorld {
public:
    // Load NPCs from database
    void LoadNpcsFromDB(Database &db, uint8_t mapId = 0);

    const std::vector<NpcSpawn> &GetNpcs() const { return m_npcs; }

    // Build the 0x13 viewport packet bytes for all NPCs
    std::vector<uint8_t> BuildNpcViewportPacket() const;

private:
    std::vector<NpcSpawn> m_npcs;
};

#endif // MU_GAME_WORLD_HPP
