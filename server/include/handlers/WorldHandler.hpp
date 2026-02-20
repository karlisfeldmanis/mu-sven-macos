#ifndef MU_WORLD_HANDLER_HPP
#define MU_WORLD_HANDLER_HPP

#include "../Database.hpp"
#include "../GameWorld.hpp"
#include "../Session.hpp"
#include <cstdint>
#include <vector>

namespace WorldHandler {

// Send packets on world enter
void SendWelcome(Session &session);
void SendNpcViewport(Session &session, const GameWorld &world);
void SendMonsterViewport(Session &session, const GameWorld &world);

// Packet handlers
void HandleMove(Session &session, const std::vector<uint8_t> &packet,
                Database &db);
void HandlePrecisePosition(Session &session,
                           const std::vector<uint8_t> &packet);

// Auth handlers (simple auto-login flow)
void HandleLogin(Session &session, const std::vector<uint8_t> &packet,
                 Database &db);
void HandleCharListRequest(Session &session, Database &db);
void HandleCharSelect(Session &session, const std::vector<uint8_t> &packet,
                      Database &db, GameWorld &world);

} // namespace WorldHandler

#endif // MU_WORLD_HANDLER_HPP
