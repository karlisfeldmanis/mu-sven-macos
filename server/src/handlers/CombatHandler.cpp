#include "handlers/CombatHandler.hpp"
#include "GameWorld.hpp"
#include "PacketDefs.hpp"
#include "PathFinder.hpp"
#include "Server.hpp"
#include "StatCalculator.hpp"
#include "handlers/CharacterHandler.hpp"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace CombatHandler {

// DK skill definitions (server-side copy matching client g_dkSkills)
struct SkillDef {
  uint8_t skillId;
  int agCost;
  int damageBonus;
};

static const SkillDef g_skillDefs[] = {
    {19, 9, 15},  // Falling Slash
    {20, 9, 15},  // Lunge
    {21, 8, 15},  // Uppercut
    {22, 9, 18},  // Cyclone
    {23, 10, 20}, // Slash
    {41, 10, 25}, // Twisting Slash
    {42, 20, 60}, // Rageful Blow
    {43, 12, 70}, // Death Stab
};

static const SkillDef *FindSkillDef(uint8_t skillId) {
  for (auto &s : g_skillDefs)
    if (s.skillId == skillId)
      return &s;
  return nullptr;
}

// Shared combat logic: calculate damage, apply to monster, handle aggro/kill/XP
static void ApplyDamageToMonster(Session &session, MonsterInstance *mon,
                                 int bonusDamage, GameWorld &world,
                                 Server &server) {
  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  bool hasBow = session.hasBow;

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
  int hitChance;
  if (attackRate > 0 && defRate < attackRate) {
    hitChance = 100 - (defRate * 100) / attackRate;
  } else {
    hitChance = 3;
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
      damage = (baseMax * 120) / 100; // Excellent: 1.2x max
      damageType = 3;
    } else if (critRoll < 6) {
      damage = baseMax; // Critical: max damage
      damageType = 2;
    }

    // Two-handed weapon bonus: 120%
    if (session.hasTwoHandedWeapon) {
      damage = (damage * 120) / 100;
    }

    // Skill damage bonus (flat addition before defense)
    damage += bonusDamage;

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
    mon->moveTimer = mon->moveDelay;
    mon->attackCooldown = 0.0f;

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
        if (ally.chaseFailCount >= 5)
          continue;
        int dist = PathFinder::ChebyshevDist(ally.gridX, ally.gridY, mon->gridX,
                                             mon->gridY);
        if (dist <= ally.viewRange) {
          ally.aggroTargetFd = session.GetFd();
          ally.aggroTimer = 15.0f;
          ally.aiState = MonsterInstance::AIState::CHASING;
          ally.currentPath.clear();
          ally.pathStep = 0;
          ally.repathTimer = 0.0f;
          ally.moveTimer = ally.moveDelay;
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

      double baseXP = (mon->level + 25.0) * mon->level / 3.0;
      if (session.level > mon->level + 10) {
        baseXP *= (double)(mon->level + 10) / session.level;
      }
      if (mon->level >= 65) {
        baseXP += (mon->level - 64) * (mon->level / 4);
      }
      int xp = std::max(1, (int)(baseXP * 1.25 * ServerConfig::XP_MULTIPLIER));

      PMSG_MONSTER_DEATH_SEND deathPkt{};
      deathPkt.h = MakeC1Header(sizeof(deathPkt), Opcode::MON_DEATH);
      deathPkt.monsterIndex = mon->index;
      deathPkt.killerCharId = static_cast<uint16_t>(session.characterId);
      deathPkt.xpReward = static_cast<uint32_t>(xp);
      server.Broadcast(&deathPkt, sizeof(deathPkt));

      session.experience += xp;
      bool leveledUp = false;
      while (true) {
        uint64_t nextXP = Database::GetXPForLevel(session.level);
        if (session.experience >= nextXP && session.level < 400) {
          session.level++;

          CharacterClass charCls2 =
              static_cast<CharacterClass>(session.classCode);
          session.levelUpPoints += StatCalculator::GetLevelUpPoints(charCls2);

          session.maxHp = StatCalculator::CalculateMaxHP(
              charCls2, session.level, session.vitality);
          session.maxMana = StatCalculator::CalculateMaxMP(
              charCls2, session.level, session.energy);
          session.maxAg = StatCalculator::CalculateMaxAG(
              session.strength, session.dexterity, session.vitality,
              session.energy);

          session.hp = session.maxHp;
          session.mana = session.maxMana;
          session.ag = session.maxAg;
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

void HandleAttack(Session &session, const std::vector<uint8_t> &packet,
                  GameWorld &world, Server &server) {
  if (packet.size() < sizeof(PMSG_ATTACK_RECV))
    return;
  const auto *atk = reinterpret_cast<const PMSG_ATTACK_RECV *>(packet.data());

  auto *mon = world.FindMonster(atk->monsterIndex);
  if (!mon || mon->aiState == MonsterInstance::AIState::DYING ||
      mon->aiState == MonsterInstance::AIState::DEAD)
    return;

  ApplyDamageToMonster(session, mon, 0, world, server);
}

// File-based debug log for skill attacks (stdout goes to /dev/null)
static void SkillLog(const char *fmt, ...) {
  FILE *f = fopen("skill_debug.log", "a");
  if (!f)
    return;
  va_list args;
  va_start(args, fmt);
  vfprintf(f, fmt, args);
  va_end(args);
  fclose(f);
}

void HandleSkillAttack(Session &session, const std::vector<uint8_t> &packet,
                       GameWorld &world, Server &server) {
  SkillLog("[SkillAttack] ENTER: pktSize=%zu learnedSkills=%zu mana=%d\n",
           packet.size(), session.learnedSkills.size(), session.mana);

  if (packet.size() < sizeof(PMSG_SKILL_ATTACK_RECV)) {
    SkillLog("[SkillAttack] FAIL: packet too small (%zu < %zu)\n",
             packet.size(), sizeof(PMSG_SKILL_ATTACK_RECV));
    return;
  }
  const auto *atk =
      reinterpret_cast<const PMSG_SKILL_ATTACK_RECV *>(packet.data());

  SkillLog("[SkillAttack] monsterIndex=%d skillId=%d\n", atk->monsterIndex,
           atk->skillId);

  // Validate skill is learned
  bool hasSkill = false;
  for (auto s : session.learnedSkills) {
    if (s == atk->skillId) {
      hasSkill = true;
      break;
    }
  }
  if (!hasSkill) {
    SkillLog("[SkillAttack] FAIL: skill %d not learned (have %zu skills)\n",
             atk->skillId, session.learnedSkills.size());
    printf("[Combat] fd=%d tried skill %d but hasn't learned it\n",
           session.GetFd(), atk->skillId);
    return;
  }

  // Look up skill definition
  const SkillDef *skillDef = FindSkillDef(atk->skillId);
  if (!skillDef) {
    SkillLog("[SkillAttack] FAIL: unknown skill %d\n", atk->skillId);
    printf("[Combat] fd=%d unknown skill %d\n", session.GetFd(), atk->skillId);
    return;
  }

  // Check resource (DK uses AG, others use Mana)
  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  bool isDK = (charCls == CharacterClass::CLASS_DK);
  int currentResource = isDK ? session.ag : session.mana;

  if (currentResource < skillDef->agCost) {
    SkillLog("[SkillAttack] FAIL: not enough resource (%d < %d) isDK=%d\n",
             currentResource, skillDef->agCost, (int)isDK);
    printf("[Combat] fd=%d not enough %s (%d/%d) for skill %d\n",
           session.GetFd(), isDK ? "AG" : "Mana", currentResource,
           skillDef->agCost, atk->skillId);
    return;
  }

  // Deduct resource
  if (isDK) {
    session.ag -= skillDef->agCost;
  } else {
    session.mana -= skillDef->agCost;
  }

  SkillLog("[SkillAttack] Resource deducted: %d -> %d\n", currentResource,
           isDK ? session.ag : session.mana);

  // Find target monster
  auto *mon = world.FindMonster(atk->monsterIndex);
  if (!mon || mon->aiState == MonsterInstance::AIState::DYING ||
      mon->aiState == MonsterInstance::AIState::DEAD) {
    SkillLog("[SkillAttack] FAIL: monster %d not found or dead\n",
             atk->monsterIndex);
    return;
  }

  SkillLog("[SkillAttack] SUCCESS: applying damage bonus=%d to mon %d\n",
           skillDef->damageBonus, atk->monsterIndex);

  // Apply damage with skill bonus
  ApplyDamageToMonster(session, mon, skillDef->damageBonus, world, server);

  // Send updated stats (so client sees AG decrease)
  CharacterHandler::SendCharStats(session);

  printf("[Combat] fd=%d used skill %d (AG cost=%d, bonus=%d) on mon %d\n",
         session.GetFd(), atk->skillId, skillDef->agCost, skillDef->damageBonus,
         atk->monsterIndex);
}

} // namespace CombatHandler
