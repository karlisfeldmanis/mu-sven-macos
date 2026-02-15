#ifndef MU_PACKET_HANDLER_HPP
#define MU_PACKET_HANDLER_HPP

#include "Session.hpp"
#include "Database.hpp"
#include "GameWorld.hpp"
#include <cstdint>
#include <vector>

namespace PacketHandler {

// Dispatch a received packet to the appropriate handler
void Handle(Session &session, const std::vector<uint8_t> &packet,
            Database &db, GameWorld &world);

// Send welcome + NPC viewport + equipment immediately (no login needed)
void SendWelcome(Session &session);
void SendNpcViewport(Session &session, const GameWorld &world);
void SendEquipment(Session &session, Database &db, int characterId);

// Handle movement
void HandleMove(Session &session, const std::vector<uint8_t> &packet, Database &db);

// Handle login (for future use)
void HandleLogin(Session &session, const std::vector<uint8_t> &packet, Database &db);
void HandleCharListRequest(Session &session, Database &db);
void HandleCharSelect(Session &session, const std::vector<uint8_t> &packet,
                      Database &db, GameWorld &world);

} // namespace PacketHandler

#endif // MU_PACKET_HANDLER_HPP
