#ifndef MU_PACKET_HANDLER_HPP
#define MU_PACKET_HANDLER_HPP

#include "Database.hpp"
#include "GameWorld.hpp"
#include "Session.hpp"
#include <cstdint>
#include <vector>

class Server; // Forward declaration

namespace PacketHandler {

// Dispatch a received packet to the appropriate handler
void Handle(Session &session, const std::vector<uint8_t> &packet, Database &db,
            GameWorld &world, Server &server);

// Send welcome + NPC viewport + equipment + stats immediately (no login needed)
void SendWelcome(Session &session);
void SendNpcViewport(Session &session, const GameWorld &world);
void SendMonsterViewport(Session &session, const GameWorld &world);
void SendEquipment(Session &session, Database &db, int characterId);
void SendCharStats(Session &session, Database &db, int characterId);
void SendCharStats(Session &session);

// Handle movement, character save, and equipment changes
void HandleMove(Session &session, const std::vector<uint8_t> &packet,
                Database &db);
void HandleCharSave(Session &session, const std::vector<uint8_t> &packet,
                    Database &db);
void HandleEquip(Session &session, const std::vector<uint8_t> &packet,
                 Database &db);

// Handle login (for future use)
void HandleLogin(Session &session, const std::vector<uint8_t> &packet,
                 Database &db);
void HandleCharListRequest(Session &session, Database &db);
void HandleCharSelect(Session &session, const std::vector<uint8_t> &packet,
                      Database &db, GameWorld &world);

// Server-authoritative combat and drops
void HandleAttack(Session &session, const std::vector<uint8_t> &packet,
                  GameWorld &world, Server &server);
void HandlePickup(Session &session, const std::vector<uint8_t> &packet,
                  GameWorld &world, Server &server, Database &db);

// Stat allocation and inventory
void HandleStatAlloc(Session &session, const std::vector<uint8_t> &packet,
                     Database &db);
void SendInventorySync(Session &session);
void LoadInventory(Session &session, Database &db, int characterId);

} // namespace PacketHandler

#endif // MU_PACKET_HANDLER_HPP
