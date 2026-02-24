#ifndef MU_CHARACTER_SELECT_HANDLER_HPP
#define MU_CHARACTER_SELECT_HANDLER_HPP

#include "../Database.hpp"
#include "../GameWorld.hpp"
#include "../Session.hpp"
#include <cstdint>
#include <vector>

class Server;

namespace CharacterSelectHandler {

// Send character list to client (F3:00)
void SendCharList(Session &session, Database &db);

// Handle character create request (F3:01)
void HandleCharCreate(Session &session, const std::vector<uint8_t> &packet,
                      Database &db);

// Handle character delete request (F3:02)
void HandleCharDelete(Session &session, const std::vector<uint8_t> &packet,
                      Database &db);

// Handle character select and enter world (F3:03)
// Loads character, sends world data, transitions to inWorld
void HandleCharSelect(Session &session, const std::vector<uint8_t> &packet,
                      Database &db, GameWorld &world, Server &server);

} // namespace CharacterSelectHandler

#endif // MU_CHARACTER_SELECT_HANDLER_HPP
