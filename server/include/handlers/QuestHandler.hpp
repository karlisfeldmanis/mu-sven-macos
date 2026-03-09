#ifndef MU_QUEST_HANDLER_HPP
#define MU_QUEST_HANDLER_HPP

#include "Database.hpp"
#include "Session.hpp"
#include <cstdint>
#include <vector>

class Server;

namespace QuestHandler {

// Send current quest state to client (on login + after changes)
void SendQuestState(Session &session);

// C->S: Player accepts quest from guard (also handles travel quest completion)
void HandleQuestAccept(Session &session, const std::vector<uint8_t> &packet,
                       Database &db, Server &server);

// C->S: Player completes (turns in) quest at guard
void HandleQuestComplete(Session &session, const std::vector<uint8_t> &packet,
                         Database &db, Server &server);

// C->S: Player abandons active quest (resets kill counts)
void HandleQuestAbandon(Session &session, Database &db);

// Called by CombatHandler when a monster is killed
void OnMonsterKill(Session &session, uint16_t monsterType, Database &db);

} // namespace QuestHandler

#endif // MU_QUEST_HANDLER_HPP
