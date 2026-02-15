#include "GameWorld.hpp"
#include "PacketDefs.hpp"
#include <cstdio>

void GameWorld::LoadNpcsFromDB(Database &db, uint8_t mapId) {
    auto spawns = db.GetNpcSpawns(mapId);

    uint16_t nextIndex = 1001;
    for (auto &s : spawns) {
        NpcSpawn npc;
        npc.index = nextIndex++;
        npc.type = s.type;
        npc.x = s.posX;
        npc.y = s.posY;
        npc.dir = s.direction;
        npc.name = s.name;
        m_npcs.push_back(npc);

        printf("[World] NPC #%d: type=%d pos=(%d,%d) dir=%d %s\n",
               npc.index, npc.type, npc.x, npc.y, npc.dir, npc.name.c_str());
    }

    printf("[World] Loaded %zu NPCs for map %d from database\n", m_npcs.size(), mapId);
}

std::vector<uint8_t> GameWorld::BuildNpcViewportPacket() const {
    // C2 header (4 bytes) + count (1 byte) + N * PMSG_VIEWPORT_NPC (9 bytes each)
    size_t npcSize = sizeof(PMSG_VIEWPORT_NPC);
    size_t totalSize = sizeof(PMSG_VIEWPORT_HEAD) + m_npcs.size() * npcSize;

    std::vector<uint8_t> packet(totalSize, 0);

    // Header
    auto *head = reinterpret_cast<PMSG_VIEWPORT_HEAD *>(packet.data());
    head->h = MakeC2Header(static_cast<uint16_t>(totalSize), 0x13);
    head->count = static_cast<uint8_t>(m_npcs.size());

    // NPC entries
    auto *entries = reinterpret_cast<PMSG_VIEWPORT_NPC *>(packet.data() + sizeof(PMSG_VIEWPORT_HEAD));
    for (size_t i = 0; i < m_npcs.size(); i++) {
        const auto &npc = m_npcs[i];
        auto &e = entries[i];

        // Index with create flag (bit 15 = 0x80 in high byte)
        e.indexH = 0x80 | static_cast<uint8_t>(npc.index >> 8);
        e.indexL = static_cast<uint8_t>(npc.index & 0xFF);

        // Type
        e.typeH = static_cast<uint8_t>(npc.type >> 8);
        e.typeL = static_cast<uint8_t>(npc.type & 0xFF);

        // Position (static NPC: target = position)
        e.x = npc.x;
        e.y = npc.y;
        e.tx = npc.x;
        e.ty = npc.y;

        // Direction in high nibble
        e.dirAndPk = static_cast<uint8_t>(npc.dir << 4);
    }

    return packet;
}
