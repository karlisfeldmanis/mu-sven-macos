#include "handlers/QuestHandler.hpp"
#include "PacketDefs.hpp"
#include "Server.hpp"
#include "StatCalculator.hpp"
#include "handlers/CharacterHandler.hpp"
#include "handlers/InventoryHandler.hpp"
#include <cstdio>

// ═══════════════════════════════════════════════════════
// Quest Chain — 9 quests: 5 kill + 4 travel, linear chain across 5 guards
// ═══════════════════════════════════════════════════════

struct QuestKillTarget {
  uint8_t monsterType;
  uint8_t killsRequired;
};

struct QuestItemReward {
  int16_t defIndex;  // -1 = no item
  uint8_t itemLevel; // Enhancement level (+0, +1, etc.)
};

struct QuestDef {
  uint16_t guardNpcType;  // Kill: quest giver. Travel: destination guard
  uint8_t questType;      // 0=kill, 1=travel
  uint8_t targetCount;    // Number of kill targets (0 for travel)
  QuestKillTarget targets[3];
  uint32_t zenReward;
  uint32_t xpReward;
  QuestItemReward dkReward; // {-1,0} = no item
  QuestItemReward dwReward; // {-1,0} = no item
  QuestItemReward orbReward;    // DK skill orb
  QuestItemReward scrollReward; // DW spell scroll
};

static constexpr int QUEST_COUNT = 9;

static const QuestDef g_quests[QUEST_COUNT] = {
    // Quest 0 (Kill): Kael — Spider + Budge Dragon
    {248, 0, 2, {{3, 10}, {2, 5}, {0, 0}}, 5000, 60000, {0, 0}, {160, 0}, {404, 0}, {483, 0}},
    // Quest 1 (Travel): → Corporal Brynn
    {246, 1, 0, {{0, 0}, {0, 0}, {0, 0}}, 3000, 30000, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}},
    // Quest 2 (Kill): Brynn — Bull Fighter + Hound
    {246, 0, 2, {{0, 8}, {1, 6}, {0, 0}}, 10000, 100000, {1, 2}, {160, 2}, {405, 0}, {482, 0}},
    // Quest 3 (Travel): → Sergeant Dorian
    {247, 1, 0, {{0, 0}, {0, 0}, {0, 0}}, 5000, 50000, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}},
    // Quest 4 (Kill): Dorian — Elite Bull Fighter + Lich
    {247, 0, 2, {{4, 6}, {6, 5}, {0, 0}}, 15000, 130000, {2, 0}, {160, 4}, {406, 0}, {485, 0}},
    // Quest 5 (Travel): → Warden Aldric
    {245, 1, 0, {{0, 0}, {0, 0}, {0, 0}}, 8000, 80000, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}},
    // Quest 6 (Kill): Aldric — Giant
    {245, 0, 1, {{7, 5}, {0, 0}, {0, 0}}, 25000, 200000, {3, 0}, {161, 0}, {407, 0}, {481, 0}},
    // Quest 7 (Travel): → Captain Marcus
    {249, 1, 0, {{0, 0}, {0, 0}, {0, 0}}, 10000, 100000, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}},
    // Quest 8 (Kill): Marcus — Skeleton Warrior + Lich
    {249, 0, 2, {{14, 5}, {6, 4}, {0, 0}}, 50000, 350000, {6, 0}, {161, 3}, {408, 0}, {486, 0}},
};

// ═══════════════════════════════════════════════════════
// Helper: insert item reward into player inventory
// ═══════════════════════════════════════════════════════

static void GiveItemReward(Session &session, Database &db,
                           const QuestItemReward &reward) {
  if (reward.defIndex < 0)
    return;

  uint8_t cat = reward.defIndex / 32;
  uint8_t idx = reward.defIndex % 32;

  auto def = db.GetItemDefinition(cat, idx);
  if (def.name.empty()) {
    printf("[Quest] Item reward defIndex=%d not found in DB\n", reward.defIndex);
    return;
  }

  uint8_t outSlot = 0;
  if (!InventoryHandler::FindEmptySpace(session, def.width, def.height,
                                        outSlot)) {
    printf("[Quest] fd=%d inventory full, skipping item reward %s\n",
           session.GetFd(), def.name.c_str());
    return;
  }

  // Populate bag grid cells
  for (int y = 0; y < def.height; ++y) {
    for (int x = 0; x < def.width; ++x) {
      int slot = (outSlot / 8 + y) * 8 + (outSlot % 8 + x);
      if (slot < 64) {
        session.bag[slot].occupied = true;
        session.bag[slot].defIndex = reward.defIndex;
        session.bag[slot].category = cat;
        session.bag[slot].itemIndex = idx;
        session.bag[slot].quantity = 1;
        session.bag[slot].itemLevel = reward.itemLevel;
        session.bag[slot].primary = (slot == outSlot);
      }
    }
  }

  db.SaveCharacterInventory(session.characterId, reward.defIndex, 1,
                            reward.itemLevel, outSlot);

  printf("[Quest] fd=%d received item: %s +%d (slot %d)\n", session.GetFd(),
         def.name.c_str(), reward.itemLevel, outSlot);
}

namespace QuestHandler {

void SendQuestState(Session &session) {
  PMSG_QUEST_STATE_SEND pkt{};
  pkt.h = MakeC1SubHeader(sizeof(pkt), Opcode::QUEST, Opcode::SUB_QUEST_STATE);
  pkt.questIndex = (uint8_t)session.questIndex;

  if (session.questIndex < QUEST_COUNT && session.questAccepted) {
    const auto &q = g_quests[session.questIndex];
    if (q.questType == 0) { // Kill quest
      pkt.targetCount = q.targetCount;
      int kc[3] = {session.questKillCount0, session.questKillCount1,
                   session.questKillCount2};
      for (int i = 0; i < q.targetCount; i++) {
        pkt.targets[i].killCount = (uint8_t)kc[i];
        pkt.targets[i].killsRequired = q.targets[i].killsRequired;
      }
    } else {
      pkt.targetCount = 0; // Travel quest — no kill targets
    }
  } else {
    pkt.targetCount = 0;
  }

  session.Send(&pkt, sizeof(pkt));
}

void HandleQuestAccept(Session &session, const std::vector<uint8_t> &packet,
                       Database &db, Server &server) {
  if (packet.size() < sizeof(PMSG_QUEST_ACCEPT_RECV))
    return;
  auto *recv = reinterpret_cast<const PMSG_QUEST_ACCEPT_RECV *>(packet.data());

  if (session.questIndex >= QUEST_COUNT) {
    printf("[Quest] fd=%d tried accept but all quests done\n", session.GetFd());
    return;
  }

  // Already accepted
  if (session.questAccepted) {
    printf("[Quest] fd=%d quest %d already accepted\n", session.GetFd(),
           session.questIndex);
    return;
  }

  // If current quest is a TRAVEL quest and the guard matches the destination,
  // auto-complete the travel quest first, then accept the kill quest
  const auto &curQ = g_quests[session.questIndex];
  if (curQ.questType == 1 && recv->guardNpcType == curQ.guardNpcType) {
    // Complete the travel quest — award travel rewards
    session.zen += curQ.zenReward;
    session.experience += curQ.xpReward;

    // Check for level ups from travel reward
    bool leveledUp = false;
    while (true) {
      uint64_t nextXP = Database::GetXPForLevel(session.level);
      if (session.experience >= nextXP && session.level < 400) {
        session.level++;
        CharacterClass charCls =
            static_cast<CharacterClass>(session.classCode);
        session.levelUpPoints += StatCalculator::GetLevelUpPoints(charCls);
        session.maxHp = StatCalculator::CalculateMaxHP(
                            charCls, session.level, session.vitality) +
                        session.petBonusMaxHp;
        session.maxMana = StatCalculator::CalculateMaxMP(charCls, session.level,
                                                        session.energy);
        session.maxAg = StatCalculator::CalculateMaxAG(
            session.strength, session.dexterity, session.vitality,
            session.energy);
        session.hp = session.maxHp;
        session.mana = session.maxMana;
        session.ag = session.maxAg;
        leveledUp = true;
      } else {
        break;
      }
    }

    // Send travel reward notification
    PMSG_QUEST_REWARD_SEND reward{};
    reward.h = MakeC1SubHeader(sizeof(reward), Opcode::QUEST,
                               Opcode::SUB_QUEST_REWARD);
    reward.zenReward = curQ.zenReward;
    reward.xpReward = curQ.xpReward;
    reward.nextQuestIndex = (uint8_t)(session.questIndex + 1);
    session.Send(&reward, sizeof(reward));

    db.UpdateCharacterMoney(session.characterId, session.zen);

    char buf[128];
    snprintf(buf, sizeof(buf), "Quest complete! +%u Zen, +%u Experience",
             curQ.zenReward, curQ.xpReward);
    server.GetDB().SaveChatMessage(session.characterId, 2, 0xFF64FFFF, buf);
    if (leveledUp) {
      snprintf(buf, sizeof(buf), "Congratulations! Level %d reached!",
               (int)session.level);
      server.GetDB().SaveChatMessage(session.characterId, 2, 0xFF64FFFF, buf);
    }

    printf("[Quest] fd=%d completed travel quest %d (zen+%u xp+%u)\n",
           session.GetFd(), session.questIndex, curQ.zenReward, curQ.xpReward);

    // Advance to the kill quest
    session.questIndex++;

    // Fall through to accept the next quest (which should be the kill quest)
    if (session.questIndex >= QUEST_COUNT) {
      db.SaveQuestState(session.characterId, session.questIndex, 0, 0, 0,
                        false);
      SendQuestState(session);
      CharacterHandler::SendCharStats(session);
      return;
    }
  }

  // Now accept the current quest (should be a kill quest)
  const auto &q = g_quests[session.questIndex];
  if (recv->guardNpcType != q.guardNpcType) {
    printf("[Quest] fd=%d wrong guard %d for quest %d (expected %d)\n",
           session.GetFd(), recv->guardNpcType, session.questIndex,
           q.guardNpcType);
    return;
  }

  // Mark as accepted
  session.questAccepted = true;
  session.questKillCount0 = 0;
  session.questKillCount1 = 0;
  session.questKillCount2 = 0;
  db.SaveQuestState(session.characterId, session.questIndex, 0, 0, 0, true);

  // Send state back to client
  SendQuestState(session);
  CharacterHandler::SendCharStats(session);

  printf("[Quest] fd=%d accepted quest %d from guard %d\n", session.GetFd(),
         session.questIndex, recv->guardNpcType);
}

void HandleQuestComplete(Session &session, const std::vector<uint8_t> &packet,
                         Database &db, Server &server) {
  if (packet.size() < sizeof(PMSG_QUEST_COMPLETE_RECV))
    return;
  auto *recv =
      reinterpret_cast<const PMSG_QUEST_COMPLETE_RECV *>(packet.data());

  if (session.questIndex >= QUEST_COUNT) {
    printf("[Quest] fd=%d tried complete but all quests done\n",
           session.GetFd());
    return;
  }

  const auto &q = g_quests[session.questIndex];

  // Only kill quests can be completed via this handler
  if (q.questType != 0) {
    printf("[Quest] fd=%d tried complete non-kill quest %d\n", session.GetFd(),
           session.questIndex);
    return;
  }

  if (recv->guardNpcType != q.guardNpcType) {
    printf("[Quest] fd=%d wrong guard %d for completing quest %d\n",
           session.GetFd(), recv->guardNpcType, session.questIndex);
    return;
  }

  // Verify all targets are complete
  int kc[3] = {session.questKillCount0, session.questKillCount1,
               session.questKillCount2};
  for (int i = 0; i < q.targetCount; i++) {
    if (kc[i] < q.targets[i].killsRequired) {
      printf("[Quest] fd=%d quest %d target %d not complete (%d/%d)\n",
             session.GetFd(), session.questIndex, i, kc[i],
             q.targets[i].killsRequired);
      return;
    }
  }

  // Award zen + XP rewards
  session.zen += q.zenReward;
  session.experience += q.xpReward;

  // Give item rewards (weapons + skill orbs/scrolls)
  GiveItemReward(session, db, q.dkReward);
  GiveItemReward(session, db, q.dwReward);
  GiveItemReward(session, db, q.orbReward);
  GiveItemReward(session, db, q.scrollReward);

  // Check for level ups
  bool leveledUp = false;
  while (true) {
    uint64_t nextXP = Database::GetXPForLevel(session.level);
    if (session.experience >= nextXP && session.level < 400) {
      session.level++;
      CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
      session.levelUpPoints += StatCalculator::GetLevelUpPoints(charCls);
      session.maxHp = StatCalculator::CalculateMaxHP(charCls, session.level,
                                                      session.vitality) +
                      session.petBonusMaxHp;
      session.maxMana = StatCalculator::CalculateMaxMP(charCls, session.level,
                                                        session.energy);
      session.maxAg = StatCalculator::CalculateMaxAG(
          session.strength, session.dexterity, session.vitality,
          session.energy);
      session.hp = session.maxHp;
      session.mana = session.maxMana;
      session.ag = session.maxAg;
      leveledUp = true;
    } else {
      break;
    }
  }

  // Advance quest (next quest NOT accepted yet)
  int nextIndex = session.questIndex + 1;
  session.questIndex = nextIndex;
  session.questKillCount0 = 0;
  session.questKillCount1 = 0;
  session.questKillCount2 = 0;
  session.questAccepted = false;

  db.SaveQuestState(session.characterId, nextIndex, 0, 0, 0, false);
  db.UpdateCharacterMoney(session.characterId, session.zen);

  // Send reward notification
  PMSG_QUEST_REWARD_SEND reward{};
  reward.h =
      MakeC1SubHeader(sizeof(reward), Opcode::QUEST, Opcode::SUB_QUEST_REWARD);
  reward.zenReward = q.zenReward;
  reward.xpReward = q.xpReward;
  reward.nextQuestIndex = (uint8_t)nextIndex;
  session.Send(&reward, sizeof(reward));

  // Send updated quest state
  SendQuestState(session);

  // Send updated inventory (for item rewards)
  InventoryHandler::SendInventorySync(session);

  // Send updated stats (zen, XP, level)
  CharacterHandler::SendCharStats(session);

  // Log reward to chat
  char buf[128];
  snprintf(buf, sizeof(buf), "Quest complete! +%u Zen, +%u Experience",
           q.zenReward, q.xpReward);
  server.GetDB().SaveChatMessage(session.characterId, 2, 0xFF64FFFF, buf);
  if (leveledUp) {
    snprintf(buf, sizeof(buf), "Congratulations! Level %d reached!",
             (int)session.level);
    server.GetDB().SaveChatMessage(session.characterId, 2, 0xFF64FFFF, buf);
  }

  printf("[Quest] fd=%d completed quest %d -> next=%d (zen+%u xp+%u)\n",
         session.GetFd(), session.questIndex - 1, nextIndex, q.zenReward,
         q.xpReward);
}

void HandleQuestAbandon(Session &session, Database &db) {
  if (session.questIndex >= QUEST_COUNT) {
    printf("[Quest] fd=%d tried abandon but all quests done\n", session.GetFd());
    return;
  }

  // Only abandon if quest is accepted
  if (!session.questAccepted) {
    printf("[Quest] fd=%d tried abandon but quest %d not accepted\n",
           session.GetFd(), session.questIndex);
    return;
  }

  // Can only abandon kill quests (travel quests have no progress to reset)
  const auto &q = g_quests[session.questIndex];
  if (q.questType != 0) {
    printf("[Quest] fd=%d tried abandon travel quest %d\n", session.GetFd(),
           session.questIndex);
    return;
  }

  printf("[Quest] fd=%d abandoned quest %d (was %d/%d/%d)\n", session.GetFd(),
         session.questIndex, session.questKillCount0, session.questKillCount1,
         session.questKillCount2);

  // Reset kill counts and acceptance
  session.questKillCount0 = 0;
  session.questKillCount1 = 0;
  session.questKillCount2 = 0;
  session.questAccepted = false;

  db.SaveQuestState(session.characterId, session.questIndex, 0, 0, 0, false);
  SendQuestState(session);
}

void OnMonsterKill(Session &session, uint16_t monsterType, Database &db) {
  if (session.questIndex >= QUEST_COUNT || !session.questAccepted)
    return;

  const auto &q = g_quests[session.questIndex];

  // Only kill quests track monster kills
  if (q.questType != 0)
    return;

  int *kc[3] = {&session.questKillCount0, &session.questKillCount1,
                &session.questKillCount2};

  bool matched = false;
  for (int i = 0; i < q.targetCount; i++) {
    if (q.targets[i].monsterType == monsterType &&
        *kc[i] < q.targets[i].killsRequired) {
      (*kc[i])++;
      matched = true;
      printf("[Quest] fd=%d quest %d target %d: kill %d/%d (monType=%d)\n",
             session.GetFd(), session.questIndex, i, *kc[i],
             q.targets[i].killsRequired, monsterType);
      break;
    }
  }

  if (matched) {
    db.SaveQuestState(session.characterId, session.questIndex,
                      session.questKillCount0, session.questKillCount1,
                      session.questKillCount2, true);
    SendQuestState(session);
  }
}

} // namespace QuestHandler
