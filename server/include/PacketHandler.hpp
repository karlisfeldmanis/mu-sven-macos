#ifndef MU_PACKET_HANDLER_HPP
#define MU_PACKET_HANDLER_HPP

#include "Database.hpp"
#include "GameWorld.hpp"
#include "Session.hpp"
#include <cstdint>
#include <vector>

class Server;

// Re-export handler namespaces for convenience
#include "handlers/CharacterHandler.hpp"
#include "handlers/CombatHandler.hpp"
#include "handlers/InventoryHandler.hpp"
#include "handlers/WorldHandler.hpp"

namespace PacketHandler {

// Dispatch a received packet to the appropriate handler module
void Handle(Session &session, const std::vector<uint8_t> &packet, Database &db,
            GameWorld &world, Server &server);

} // namespace PacketHandler

#endif // MU_PACKET_HANDLER_HPP
