#include "handlers/CombatHandler.hpp"
#include "GameWorld.hpp"
#include "PacketDefs.hpp"
#include "PathFinder.hpp"
#include "Server.hpp"
#include "StatCalculator.hpp"
#include "handlers/CharacterHandler.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace CombatHandler {

void HandleAttack(Session &session, const std::vector<uint8_t> &packet,
                  GameWorld &world, Server &server) {
  if (packet.size() < sizeof(PMSG_ATTACK_RECV))
    return;
  const auto *atk = reinterpret_cast<const PMSG_ATTACK_RECV *>(packet.data());

  auto *mon = world.FindMonster(atk->monsterIndex);
  if (!mon || mon->aiState == MonsterInstance::AIState::DYING ||
      mon->aiState == MonsterInstance::AIState::DEAD)
    return;

  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  bool hasBow = false;

  int baseMin = StatCalculator::CalculateMinDamage(charCls, session.strength,
                                                   session.dexterity,
                                                   session.energy, hasBow) +
                session.weaponDamageMin;
  int baseMax = StatCalculator::CalculateMaxDamage(charCls, session.strength,
                                                   session.dexterity,
                                                   session.energy, hasBow) +
                session.weaponDamageMax;

  if (baseMax < baseMin)
    baseMax = baseMin;

  uint8_t damageType = 1; // normal
  int damage = 0;
  bool missed = false;

  int attackRate = StatCalculator::CalculateAttackRate(
      session.level, session.dexterity, session.strength);
  int defRate = mon->defenseRate;

  // OpenMU hit chance: hitChance = 1 - defRate/atkRate (min 3%)
  // Reference: AttackableExtensions.cs GetHitChanceTo()
  int hitChance;
  if (attackRate > 0 && defRate < attackRate) {
    hitChance = 100 - (defRate * 100) / attackRate;
  } else {
    hitChance = 3; // 3% minimum (OpenMU)
  }
  if (hitChance < 3)
    hitChance = 3;
  if (hitChance > 100)
    hitChance = 100;

  if (rand() % 100 >= hitChance) {
    missed = true;
    damageType = 0; // miss
  }

  if (!missed) {
    damage =
        baseMin + (baseMax > baseMin ? rand() % (baseMax - baseMin + 1) : 0);

    int critRoll = rand() % 100;
    if (critRoll < 1) {
      damage = (baseMax * 120) / 100; // Excellent: 1.2x max (OpenMU)
      damageType = 3;
    } else if (critRoll < 6) {
      damage = baseMax; // Critical: max damage (OpenMU)
      damageType = 2;
    }

    damage = std::max(1, damage - mon->defense);
    mon->hp -= damage;

    // Aggro logic
    if (mon->aggroTargetFd != session.GetFd()) {
      mon->aggroTargetFd = session.GetFd();
      printf("[AI] Mon %d (type %d): NEW AGGRO on attacker fd=%d (dmg=%d)\n",
             mon->index, mon->type, session.GetFd(), damage);
    }
    mon->aggroTimer = 15.0f;
    mon->aiState = MonsterInstance::AIState::CHASING;
    mon->currentPath.clear();
    mon->pathStep = 0;
    mon->repathTimer = 0.0f;
    mon->moveTimer = mon->moveDelay; // First chase step happens immediately
    mon->attackCooldown = 0.0f;      // Attack back immediately

    // Pack assist (same-type monsters within viewRange join aggro)
    if (mon->aggressive) {
      for (auto &ally : world.GetMonsterInstancesMut()) {
        if (ally.index == mon->index)
          continue;
        if (ally.type != mon->type)
          continue;
        if (ally.aiState == MonsterInstance::AIState::DYING ||
            ally.aiState == MonsterInstance::AIState::DEAD)
          continue;
        if (ally.aiState == MonsterInstance::AIState::CHASING ||
            ally.aiState == MonsterInstance::AIState::ATTACKING ||
            ally.aiState == MonsterInstance::AIState::RETURNING)
          continue;
        if (ally.aggroTimer < 0.0f)
          continue;
        // Skip monsters that recently failed to pathfind (avoid aggro loop)
        if (ally.chaseFailCount >= 5)
          continue;
        int dist = PathFinder::ChebyshevDist(ally.gridX, ally.gridY,
                                             mon->gridX, mon->gridY);
        if (dist <= ally.viewRange) {
          ally.aggroTargetFd = session.GetFd();
          ally.aggroTimer = 15.0f;
          ally.aiState = MonsterInstance::AIState::CHASING;
          ally.currentPath.clear();
          ally.pathStep = 0;
          ally.repathTimer = 0.0f;
          ally.moveTimer = ally.moveDelay; // First step immediately
          ally.attackCooldown = 0.0f;
          printf("[AI] Mon %d (type %d): PACK ASSIST on attacker fd=%d "
                 "(dist=%d cells from attacked)\n",
                 ally.index, ally.type, session.GetFd(), dist);
        }
      }
    }

    bool killed = mon->hp <= 0;
    if (killed)
      mon->hp = 0;

    if (killed) {
      mon->aiState = MonsterInstance::AIState::DYING;
      mon->stateTimer = 0.0f;

      // OpenMU XP formula: (targetLevel + 25) * targetLevel / 3.0 * 1.25
      double baseXP = (mon->level + 25.0) * mon->level / 3.0;
      if (session.level > mon->level + 10) {
        baseXP *= (double)(mon->level + 10) / session.level;
      }
      if (mon->level >= 65) {
        baseXP += (mon->level - 64) * (mon->level / 4);
      }
      int xp = std::max(1, (int)(baseXP * 1.25 * ServerConfig::XP_MULTIPLIER));

      // Broadcast death + XP
      PMSG_MONSTER_DEATH_SEND deathPkt{};
      deathPkt.h = MakeC1Header(sizeof(deathPkt), Opcode::MON_DEATH);
      deathPkt.monsterIndex = mon->index;
      deathPkt.killerCharId = static_cast<uint16_t>(session.characterId);
      deathPkt.xpReward = static_cast<uint32_t>(xp);
      server.Broadcast(&deathPkt, sizeof(deathPkt));

      // Authoritative leveling
      session.experience += xp;
      bool leveledUp = false;
      while (true) {
        uint64_t nextXP = Database::GetXPForLevel(session.level);
        if (session.experience >= nextXP && session.level < 400) {
          session.level++;

          CharacterClass charCls =
              static_cast<CharacterClass>(session.classCode);
          session.levelUpPoints += StatCalculator::GetLevelUpPoints(charCls);

          session.maxHp = StatCalculator::CalculateMaxHP(charCls, session.level,
                                                         session.vitality);
          session.maxMana = StatCalculator::CalculateMaxManaOrAG(
              charCls, session.level, session.strength, session.dexterity,
              session.vitality, session.energy);

          session.hp = session.maxHp;
          session.mana = session.maxMana;
          leveledUp = true;
          printf("[Combat] Char %d leveled up to %d! Total XP: %llu\n",
                 session.characterId, (int)session.level,
                 (unsigned long long)session.experience);
        } else {
          break;
        }
      }

      if (leveledUp || xp > 0) {
        CharacterHandler::SendCharStats(session);
      }

      // Spawn drops
      auto drops = world.SpawnDrops(mon->worldX, mon->worldZ, mon->level,
                                    mon->type, server.GetDB());
      for (auto &drop : drops) {
        PMSG_DROP_SPAWN_SEND dropPkt{};
        dropPkt.h = MakeC1Header(sizeof(dropPkt), Opcode::DROP_SPAWN);
        dropPkt.dropIndex = drop.index;
        dropPkt.defIndex = drop.defIndex;
        dropPkt.quantity = drop.quantity;
        dropPkt.itemLevel = drop.itemLevel;
        dropPkt.worldX = drop.worldX;
        dropPkt.worldZ = drop.worldZ;
        server.Broadcast(&dropPkt, sizeof(dropPkt));
      }

      printf("[Combat] Monster %d killed by char %d (dmg=%d, xp=%d, "
             "drops=%zu)\n",
             mon->index, session.characterId, damage, xp, drops.size());
    }
  }

  // Broadcast damage result
  PMSG_DAMAGE_SEND dmgPkt{};
  dmgPkt.h = MakeC1Header(sizeof(dmgPkt), Opcode::DAMAGE);
  dmgPkt.monsterIndex = mon->index;
  dmgPkt.damage = static_cast<uint16_t>(damage);
  dmgPkt.damageType = damageType;
  dmgPkt.remainingHp = static_cast<uint16_t>(std::max(0, mon->hp));
  dmgPkt.attackerCharId = static_cast<uint16_t>(session.characterId);
  server.Broadcast(&dmgPkt, sizeof(dmgPkt));
}

} // namespace CombatHandler
