#include "GameWorld.hpp"
#include "PacketDefs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

// ─── Terrain attribute loading (same decrypt as client TerrainParser) ───

static const uint8_t MAP_XOR_KEY[16] = {0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A,
                                        0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31,
                                        0x37, 0xB3, 0xE7, 0xA2};

static std::vector<uint8_t> DecryptMapFile(const std::vector<uint8_t> &data) {
  std::vector<uint8_t> out(data.size());
  uint8_t wKey = 0x5E;
  for (size_t i = 0; i < data.size(); i++) {
    uint8_t src = data[i];
    out[i] = (src ^ MAP_XOR_KEY[i % 16]) - wKey;
    wKey = src + 0x3D;
  }
  return out;
}

bool GameWorld::LoadTerrainAttributes(const std::string &attFilePath) {
  std::ifstream file(attFilePath, std::ios::binary);
  if (!file) {
    printf("[World] Cannot open terrain attribute file: %s\n",
           attFilePath.c_str());
    return false;
  }

  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

  // Decrypt
  std::vector<uint8_t> data = DecryptMapFile(raw);

  // BuxConvert
  static const uint8_t bux[3] = {0xFC, 0xCF, 0xAB};
  for (size_t i = 0; i < data.size(); i++)
    data[i] ^= bux[i % 3];

  const size_t cells = TERRAIN_SIZE * TERRAIN_SIZE;
  m_terrainAttributes.resize(cells, 0);

  // 4-byte header + data (WORD or BYTE format)
  const size_t wordSize = 4 + cells * 2;
  const size_t byteSize = 4 + cells;

  if (data.size() >= wordSize) {
    for (size_t i = 0; i < cells; i++)
      m_terrainAttributes[i] = data[4 + i * 2];
    printf("[World] Loaded terrain attributes (WORD format, %zu bytes)\n",
           data.size());
  } else if (data.size() >= byteSize) {
    for (size_t i = 0; i < cells; i++)
      m_terrainAttributes[i] = data[4 + i];
    printf("[World] Loaded terrain attributes (BYTE format, %zu bytes)\n",
           data.size());
  } else {
    printf("[World] Terrain attribute file too small: %zu bytes\n",
           data.size());
    return false;
  }

  // Count walkable vs blocked vs safe zone
  int blocked = 0, safeZone = 0;
  for (size_t i = 0; i < cells; i++) {
    if (m_terrainAttributes[i] & TW_NOMOVE)
      blocked++;
    if (m_terrainAttributes[i] & TW_SAFEZONE)
      safeZone++;
  }
  printf("[World] Terrain: %d blocked cells, %d safe zone cells, %zu total\n",
         blocked, safeZone, cells);
  return true;
}

bool GameWorld::IsWalkable(float worldX, float worldZ) const {
  if (m_terrainAttributes.empty())
    return true; // No data loaded = allow all
  int gz = (int)(worldX / 100.0f);
  int gx = (int)(worldZ / 100.0f);
  if (gx < 0 || gz < 0 || gx >= TERRAIN_SIZE || gz >= TERRAIN_SIZE)
    return false;
  return (m_terrainAttributes[gz * TERRAIN_SIZE + gx] & TW_NOMOVE) == 0;
}

bool GameWorld::IsSafeZone(float worldX, float worldZ) const {
  if (m_terrainAttributes.empty())
    return false;
  int gz = (int)(worldX / 100.0f);
  int gx = (int)(worldZ / 100.0f);
  if (gx < 0 || gz < 0 || gx >= TERRAIN_SIZE || gz >= TERRAIN_SIZE)
    return false;
  uint8_t attr = m_terrainAttributes[gz * TERRAIN_SIZE + gx];
  return (attr & TW_SAFEZONE) != 0 || (attr & TW_NOGROUND) != 0;
}

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

    // Guard patrol init (OpenMU: GuardIntelligence.cs)
    // Type 247 (crossbow) and 249 (berdysh) are guards
    if (npc.type == 247 || npc.type == 249) {
      npc.isGuard = true;
      npc.worldX = (float)npc.y * 100.0f; // Grid Y -> WorldX
      npc.worldZ = (float)npc.x * 100.0f; // Grid X -> WorldZ
      npc.spawnX = npc.worldX;
      npc.spawnZ = npc.worldZ;
      npc.wanderTimer = 3.0f + (float)(rand() % 5000) / 1000.0f; // Stagger
      npc.lastBroadcastX = npc.x;
      npc.lastBroadcastY = npc.y;
    }

    m_npcs.push_back(npc);

    printf("[World] NPC #%d: type=%d pos=(%d,%d) dir=%d %s%s\n", npc.index,
           npc.type, npc.x, npc.y, npc.dir, npc.name.c_str(),
           npc.isGuard ? " [GUARD]" : "");
  }

  printf("[World] Loaded %zu NPCs for map %d from database\n", m_npcs.size(),
         mapId);
}

std::vector<uint8_t> GameWorld::BuildNpcViewportPacket() const {
  size_t npcSize = sizeof(PMSG_VIEWPORT_NPC);
  size_t totalSize = sizeof(PMSG_VIEWPORT_HEAD) + m_npcs.size() * npcSize;

  std::vector<uint8_t> packet(totalSize, 0);

  auto *head = reinterpret_cast<PMSG_VIEWPORT_HEAD *>(packet.data());
  head->h = MakeC2Header(static_cast<uint16_t>(totalSize), 0x13);
  head->count = static_cast<uint8_t>(m_npcs.size());

  auto *entries = reinterpret_cast<PMSG_VIEWPORT_NPC *>(
      packet.data() + sizeof(PMSG_VIEWPORT_HEAD));
  for (size_t i = 0; i < m_npcs.size(); i++) {
    const auto &npc = m_npcs[i];
    auto &e = entries[i];
    e.indexH = 0x80 | static_cast<uint8_t>(npc.index >> 8);
    e.indexL = static_cast<uint8_t>(npc.index & 0xFF);
    e.typeH = static_cast<uint8_t>(npc.type >> 8);
    e.typeL = static_cast<uint8_t>(npc.type & 0xFF);
    e.x = npc.x;
    e.y = npc.y;
    e.tx = npc.x;
    e.ty = npc.y;
    e.dirAndPk = static_cast<uint8_t>(npc.dir << 4);
  }

  return packet;
}

void GameWorld::LoadMonstersFromDB(Database &db, uint8_t mapId) {
  auto spawns = db.GetMonsterSpawns(mapId);

  for (auto &s : spawns) {
    MonsterInstance mon{};
    mon.index = m_nextMonsterIndex++;
    mon.type = s.type;
    mon.gridX = s.posX;
    mon.gridY = s.posY;
    mon.dir = s.direction;
    // Convert grid to world coords (TERRAIN_SCALE = 100)
    mon.worldX = s.posY * 100.0f; // MU grid Y → world X
    mon.worldZ = s.posX * 100.0f; // MU grid X → world Z
    mon.spawnX = mon.worldX;
    mon.spawnZ = mon.worldZ;
    mon.state = MonsterInstance::ALIVE;

    // Set stats based on type (from OpenMU Version075 MonsterDefinitions)
    if (mon.type == 0) { // Bull Fighter — AtkSpeed=1600, MvRange=3, View=5
      mon.hp = BULL_HP;
      mon.maxHp = BULL_HP;
      mon.defense = BULL_DEFENSE;
      mon.defenseRate = BULL_DEFENSE_RATE;
      mon.attackMin = BULL_ATTACK_MIN;
      mon.attackMax = BULL_ATTACK_MAX;
      mon.attackRate = BULL_ATTACK_RATE;
      mon.level = BULL_LEVEL;
      mon.atkCooldownTime = 1.6f; // AtkSpeed=1600ms
      mon.wanderRange = 300.0f;   // MvRange=3
      mon.aggroRange = 500.0f;    // ViewRange=5
      mon.aggressive = true;      // Red: attacks on sight
    } else if (mon.type == 1) {   // Hound — AtkSpeed=1600, MvRange=3, View=5
      mon.hp = HOUND_HP;
      mon.maxHp = HOUND_HP;
      mon.defense = HOUND_DEFENSE;
      mon.defenseRate = HOUND_DEFENSE_RATE;
      mon.attackMin = HOUND_ATTACK_MIN;
      mon.attackMax = HOUND_ATTACK_MAX;
      mon.attackRate = HOUND_ATTACK_RATE;
      mon.level = HOUND_LEVEL;
      mon.atkCooldownTime = 1.6f; // AtkSpeed=1600ms
      mon.wanderRange = 300.0f;   // MvRange=3
      mon.aggroRange = 500.0f;    // ViewRange=5
      mon.aggressive = true;      // Red: attacks on sight
    } else if (mon.type ==
               2) { // Budge Dragon — AtkSpeed=2000, MvRange=3, View=4
      mon.hp = BUDGE_HP;
      mon.maxHp = BUDGE_HP;
      mon.defense = BUDGE_DEFENSE;
      mon.defenseRate = BUDGE_DEFENSE_RATE;
      mon.attackMin = BUDGE_ATTACK_MIN;
      mon.attackMax = BUDGE_ATTACK_MAX;
      mon.attackRate = BUDGE_ATTACK_RATE;
      mon.level = BUDGE_LEVEL;
      mon.atkCooldownTime = 2.0f; // AtkSpeed=2000ms
      mon.wanderRange = 300.0f;   // MvRange=3
      mon.aggroRange = 400.0f;    // ViewRange=4
      mon.aggressive = true;      // OpenMU: all Lorencia monsters aggressive
    } else if (mon.type == 3) {   // Spider — AtkSpeed=1800, MvRange=2, View=5
      mon.hp = SPIDER_HP;
      mon.maxHp = SPIDER_HP;
      mon.defense = SPIDER_DEFENSE;
      mon.defenseRate = SPIDER_DEFENSE_RATE;
      mon.attackMin = SPIDER_ATTACK_MIN;
      mon.attackMax = SPIDER_ATTACK_MAX;
      mon.attackRate = SPIDER_ATTACK_RATE;
      mon.level = SPIDER_LEVEL;
      mon.atkCooldownTime = 1.8f; // AtkSpeed=1800ms
      mon.wanderRange = 200.0f;   // MvRange=2
      mon.aggroRange = 500.0f;    // ViewRange=5
      mon.aggressive = true;      // OpenMU: all Lorencia monsters aggressive
    } else if (mon.type ==
               4) { // Elite Bull Fighter — AtkSpeed=1400, MvRange=3, View=4
      mon.hp = ELITE_BULL_HP;
      mon.maxHp = ELITE_BULL_HP;
      mon.defense = ELITE_BULL_DEFENSE;
      mon.defenseRate = ELITE_BULL_DEFENSE_RATE;
      mon.attackMin = ELITE_BULL_ATTACK_MIN;
      mon.attackMax = ELITE_BULL_ATTACK_MAX;
      mon.attackRate = ELITE_BULL_ATTACK_RATE;
      mon.level = ELITE_BULL_LEVEL;
      mon.atkCooldownTime = 1.4f; // AtkSpeed=1400ms
      mon.wanderRange = 300.0f;   // MvRange=3
      mon.aggroRange = 400.0f;    // ViewRange=4
      mon.aggressive = true;      // Red: attacks on sight
    } else if (mon.type ==
               6) { // Lich — AtkSpeed=2000, MvRange=3, View=7 (ranged caster)
      mon.hp = LICH_HP;
      mon.maxHp = LICH_HP;
      mon.defense = LICH_DEFENSE;
      mon.defenseRate = LICH_DEFENSE_RATE;
      mon.attackMin = LICH_ATTACK_MIN;
      mon.attackMax = LICH_ATTACK_MAX;
      mon.attackRate = LICH_ATTACK_RATE;
      mon.level = LICH_LEVEL;
      mon.atkCooldownTime = 2.0f; // AtkSpeed=2000ms
      mon.wanderRange = 300.0f;   // MvRange=3
      mon.aggroRange = 700.0f;    // ViewRange=7
      mon.aggressive = true;      // Red: attacks on sight
    } else if (mon.type ==
               7) { // Giant — AtkSpeed=2200, MvRange=2, View=3 (slow, strong)
      mon.hp = GIANT_HP;
      mon.maxHp = GIANT_HP;
      mon.defense = GIANT_DEFENSE;
      mon.defenseRate = GIANT_DEFENSE_RATE;
      mon.attackMin = GIANT_ATTACK_MIN;
      mon.attackMax = GIANT_ATTACK_MAX;
      mon.attackRate = GIANT_ATTACK_RATE;
      mon.level = GIANT_LEVEL;
      mon.atkCooldownTime = 2.2f; // AtkSpeed=2200ms
      mon.wanderRange = 200.0f;   // MvRange=2
      mon.aggroRange = 300.0f;    // ViewRange=3
      mon.aggressive = true;      // Red: attacks on sight
    } else if (mon.type ==
               14) { // Skeleton Warrior — AtkSpeed=1400, MvRange=2, View=4
      mon.hp = SKEL_HP;
      mon.maxHp = SKEL_HP;
      mon.defense = SKEL_DEFENSE;
      mon.defenseRate = SKEL_DEFENSE_RATE;
      mon.attackMin = SKEL_ATTACK_MIN;
      mon.attackMax = SKEL_ATTACK_MAX;
      mon.attackRate = SKEL_ATTACK_RATE;
      mon.level = SKEL_LEVEL;
      mon.atkCooldownTime = 1.4f; // AtkSpeed=1400ms
      mon.wanderRange = 200.0f;   // MvRange=2
      mon.aggroRange = 400.0f;    // ViewRange=4
      mon.aggressive = true;      // Red: attacks on sight
    } else {
      mon.hp = 30;
      mon.maxHp = 30;
      mon.defense = 3;
      mon.defenseRate = 3;
      mon.attackMin = 4;
      mon.attackMax = 7;
      mon.attackRate = 8;
      mon.level = 4;
      mon.atkCooldownTime = 1.8f;
      mon.wanderRange = 200.0f;
      mon.aggroRange = 500.0f;
    }

    // Stagger initial wander timers so all monsters don't move at once
    mon.wanderTimer = 1.0f + (float)(rand() % 5000) / 1000.0f;

    m_monsterInstances.push_back(mon);
  }

  printf("[World] Loaded %zu monsters for map %d (indices %d-%d)\n",
         m_monsterInstances.size(), mapId,
         m_monsterInstances.empty() ? 0 : m_monsterInstances.front().index,
         m_monsterInstances.empty() ? 0 : m_monsterInstances.back().index);
}

// Helper: broadcast a move update only when target grid cell, chasing state,
// or moving state changes
static void
emitMoveIfChanged(MonsterInstance &mon, uint8_t targetX, uint8_t targetY,
                  bool chasing, bool moving,
                  std::vector<GameWorld::MonsterMoveUpdate> &outMoves) {
  if (targetX != mon.lastBroadcastTargetX ||
      targetY != mon.lastBroadcastTargetY ||
      chasing != mon.lastBroadcastChasing ||
      moving != mon.lastBroadcastIsMoving) {
    mon.lastBroadcastTargetX = targetX;
    mon.lastBroadcastTargetY = targetY;
    mon.lastBroadcastChasing = chasing;
    mon.lastBroadcastIsMoving = moving;
    outMoves.push_back(
        {mon.index, targetX, targetY, static_cast<uint8_t>(chasing ? 1 : 0)});
  }
}

void GameWorld::Update(float dt,
                       std::function<void(uint16_t)> dropExpiredCallback,
                       std::vector<MonsterMoveUpdate> *outWanderMoves,
                       std::vector<NpcMoveUpdate> *outNpcMoves) {
  // Update monster states
  for (auto &mon : m_monsterInstances) {
    mon.stateTimer += dt;
    if (mon.attackCooldown > 0)
      mon.attackCooldown -= dt;

    switch (mon.state) {
    case MonsterInstance::ALIVE:
      // Wander AI for non-chasing monsters (MvRange=2 = 200 world units)
      if (!mon.isChasing) {
        mon.wanderTimer -= dt;

        if (mon.isWandering) {
          // Move toward wander target
          float dx = mon.wanderTargetX - mon.worldX;
          float dz = mon.wanderTargetZ - mon.worldZ;
          float dist = std::sqrt(dx * dx + dz * dz);
          if (dist < 10.0f) {
            // Arrived — idle for 2-5 seconds
            mon.isWandering = false;
            mon.wanderTimer = 2.0f + (float)(rand() % 3000) / 1000.0f;
            mon.gridY = static_cast<uint8_t>(mon.worldX / 100.0f);
            mon.gridX = static_cast<uint8_t>(mon.worldZ / 100.0f);
          } else {
            float step = WANDER_SPEED * dt;
            if (step > dist)
              step = dist;
            float stepX = (dx / dist) * step;
            float stepZ = (dz / dist) * step;
            float nextX = mon.worldX + stepX;
            float nextZ = mon.worldZ + stepZ;
            // Wall sliding: try full move, then X-only, then Z-only
            if (IsWalkable(nextX, nextZ)) {
              mon.worldX = nextX;
              mon.worldZ = nextZ;
            } else if (std::abs(stepX) > 0.01f &&
                       IsWalkable(mon.worldX + stepX, mon.worldZ)) {
              mon.worldX += stepX;
            } else if (std::abs(stepZ) > 0.01f &&
                       IsWalkable(mon.worldX, mon.worldZ + stepZ)) {
              mon.worldZ += stepZ;
            } else {
              // Fully blocked — stop wandering
              mon.isWandering = false;
              mon.wanderTimer = 1.0f + (float)(rand() % 2000) / 1000.0f;
            }
            mon.gridY = static_cast<uint8_t>(mon.worldX / 100.0f);
            mon.gridX = static_cast<uint8_t>(mon.worldZ / 100.0f);
          }
        } else if (mon.wanderTimer <= 0.0f) {
          // Pick a new random wander target within per-type wanderRange of
          // spawn Retry up to 5 times to find a walkable target
          bool found = false;
          for (int tries = 0; tries < 5; tries++) {
            float angle = (float)(rand() % 628) / 100.0f;
            float dist = (float)(rand() % std::max(1, (int)mon.wanderRange));
            float tx = mon.spawnX + std::cos(angle) * dist;
            float tz = mon.spawnZ + std::sin(angle) * dist;
            if (IsWalkable(tx, tz)) {
              mon.wanderTargetX = tx;
              mon.wanderTargetZ = tz;
              found = true;
              break;
            }
          }
          if (found) {
            mon.isWandering = true;
            if (outWanderMoves) {
              uint8_t tgtGX = static_cast<uint8_t>(mon.wanderTargetZ / 100.0f);
              uint8_t tgtGY = static_cast<uint8_t>(mon.wanderTargetX / 100.0f);
              emitMoveIfChanged(mon, tgtGX, tgtGY, false, true,
                                *outWanderMoves);
            }
          } else {
            // All targets unwalkable — just idle longer
            mon.wanderTimer = 3.0f + (float)(rand() % 3000) / 1000.0f;
          }
        }
      }
      break;
    case MonsterInstance::DYING:
      mon.isChasing = false;
      mon.isReturning = false;
      mon.isWandering = false;
      mon.aggroTargetFd = -1; // Clear aggro
      if (mon.stateTimer >= DYING_DURATION) {
        mon.state = MonsterInstance::DEAD;
        mon.stateTimer = 0.0f;
      }
      break;
    case MonsterInstance::DEAD:
      if (mon.stateTimer >= RESPAWN_DELAY) {
        // Respawn at original position
        mon.state = MonsterInstance::ALIVE;
        mon.stateTimer = 0.0f;
        mon.hp = mon.maxHp;
        mon.isChasing = false;
        mon.isReturning = false;
        mon.isWandering = false;
        mon.wanderTimer = 1.0f + (float)(rand() % 3000) / 1000.0f;
        mon.worldX = mon.spawnX;
        mon.worldZ = mon.spawnZ;
        // Recalc grid position
        mon.gridY = static_cast<uint8_t>(mon.worldX / 100.0f);
        mon.gridX = static_cast<uint8_t>(mon.worldZ / 100.0f);
        mon.lastBroadcastTargetX = 0;
        mon.lastBroadcastTargetY = 0;
        mon.lastBroadcastChasing = false;
        mon.aggroTargetFd = -1;    // Clear aggro
        mon.aggroTimer = -3.0f;    // 3s respawn immunity (negative = immune)
        mon.attackCooldown = 1.5f; // Prevent instant attack on respawn
        mon.justRespawned = true;
      }
      break;
    }
  }

  // ── Guard patrol AI (OpenMU: GuardIntelligence.cs) ──
  // Guards wander within 7 tiles of spawn, 80% idle chance per decision tick
  for (auto &npc : m_npcs) {
    if (!npc.isGuard)
      continue;

    if (npc.isWandering) {
      // Move toward wander target
      float dx = npc.wanderTargetX - npc.worldX;
      float dz = npc.wanderTargetZ - npc.worldZ;
      float dist = std::sqrt(dx * dx + dz * dz);
      if (dist < 10.0f) {
        // Arrived — idle for 3-8 seconds
        npc.isWandering = false;
        npc.wanderTimer = 3.0f + (float)(rand() % 5000) / 1000.0f;
        npc.y = static_cast<uint8_t>(npc.worldX / 100.0f);
        npc.x = static_cast<uint8_t>(npc.worldZ / 100.0f);
      } else {
        float step = GUARD_WANDER_SPEED * dt;
        if (step > dist)
          step = dist;
        float sX = (dx / dist) * step;
        float sZ = (dz / dist) * step;
        float nextX = npc.worldX + sX;
        float nextZ = npc.worldZ + sZ;
        // Guards can walk on safe zones (OpenMU: CanWalkOnSafezone = true)
        auto guardWalkable = [&](float wx, float wz) {
          return IsWalkable(wx, wz) || IsSafeZone(wx, wz);
        };
        // Wall sliding: try full move, then X-only, then Z-only
        if (guardWalkable(nextX, nextZ)) {
          npc.worldX = nextX;
          npc.worldZ = nextZ;
        } else if (std::abs(sX) > 0.01f &&
                   guardWalkable(npc.worldX + sX, npc.worldZ)) {
          npc.worldX += sX;
        } else if (std::abs(sZ) > 0.01f &&
                   guardWalkable(npc.worldX, npc.worldZ + sZ)) {
          npc.worldZ += sZ;
        } else {
          npc.isWandering = false;
          npc.wanderTimer = 2.0f + (float)(rand() % 3000) / 1000.0f;
        }
        npc.y = static_cast<uint8_t>(npc.worldX / 100.0f);
        npc.x = static_cast<uint8_t>(npc.worldZ / 100.0f);
      }
    } else {
      npc.wanderTimer -= dt;
      if (npc.wanderTimer <= 0.0f) {
        // 80% idle, 20% move (OpenMU: random.Next(0, 4) > 0 = 75% idle)
        if (rand() % 5 == 0) {
          // Pick a random patrol target within GUARD_PATROL_RANGE of spawn
          bool found = false;
          for (int tries = 0; tries < 5; tries++) {
            float angle = (float)(rand() % 628) / 100.0f;
            float r = (float)(rand() % (int)GUARD_PATROL_RANGE);
            float tx = npc.spawnX + std::cos(angle) * r;
            float tz = npc.spawnZ + std::sin(angle) * r;
            // Guards walk on safe zones
            if (IsWalkable(tx, tz) || IsSafeZone(tx, tz)) {
              npc.wanderTargetX = tx;
              npc.wanderTargetZ = tz;
              found = true;
              break;
            }
          }
          if (found) {
            npc.isWandering = true;
            // Broadcast guard movement
            if (outNpcMoves) {
              uint8_t tgtGX = static_cast<uint8_t>(npc.wanderTargetZ / 100.0f);
              uint8_t tgtGY = static_cast<uint8_t>(npc.wanderTargetX / 100.0f);
              // Only broadcast if position changed
              if (tgtGX != npc.lastBroadcastX || tgtGY != npc.lastBroadcastY) {
                NpcMoveUpdate upd;
                upd.npcIndex = npc.index;
                upd.targetX = tgtGX;
                upd.targetY = tgtGY;
                outNpcMoves->push_back(upd);
                npc.lastBroadcastX = tgtGX;
                npc.lastBroadcastY = tgtGY;
              }
            }
          } else {
            npc.wanderTimer = 3.0f + (float)(rand() % 3000) / 1000.0f;
          }
        } else {
          // Idle — wait 2-6 seconds
          npc.wanderTimer = 2.0f + (float)(rand() % 4000) / 1000.0f;
        }
      }
    }
  }

  // Age ground drops and remove expired ones
  for (auto it = m_drops.begin(); it != m_drops.end();) {
    it->age += dt;
    if (it->age >= DROP_DESPAWN_TIME) {
      if (dropExpiredCallback)
        dropExpiredCallback(it->index);
      it = m_drops.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<GameWorld::MonsterAttackResult>
GameWorld::ProcessMonsterAI(float dt, const std::vector<PlayerTarget> &players,
                            std::vector<MonsterMoveUpdate> &outMoves) {
  std::vector<MonsterAttackResult> attacks;

  // Helper: move a monster back to its spawn position
  auto moveTowardSpawn = [&](MonsterInstance &mon) {
    float dx = mon.spawnX - mon.worldX;
    float dz = mon.spawnZ - mon.worldZ;
    float distToSpawn = std::sqrt(dx * dx + dz * dz);
    if (distToSpawn < 10.0f) {
      // Arrived at spawn — fully reset all state so proximity aggro works again
      mon.isChasing = false;
      mon.isReturning = false;
      mon.isWandering = false;
      mon.aggroTargetFd = -1;
      mon.aggroTimer = 0.0f;
      mon.stuckFrames = 0;
      mon.wanderTimer = 2.0f + (float)(rand() % 3000) / 1000.0f;
      mon.worldX = mon.spawnX;
      mon.worldZ = mon.spawnZ;
      mon.gridY = static_cast<uint8_t>(mon.worldX / 100.0f);
      mon.gridX = static_cast<uint8_t>(mon.worldZ / 100.0f);
      mon.hp = mon.maxHp; // Restore HP on successful return
      emitMoveIfChanged(mon, mon.gridX, mon.gridY, false, false, outMoves);
    } else {
      float step = RETURN_SPEED * dt;
      if (step > distToSpawn)
        step = distToSpawn;

      // Update direction to face spawn point
      float angle = std::atan2(dz, dx);
      float deg = angle * 180.0f / 3.14159f;
      if (deg < 0)
        deg += 360.0f;
      // Convert degree to 0-7 orientation (0=E, 1=SE, 2=S, etc.)
      mon.dir = static_cast<uint8_t>(((int)(deg + 22.5f) % 360) / 45);

      float sX = (dx / distToSpawn) * step;
      float sZ = (dz / distToSpawn) * step;
      float nextX = mon.worldX + sX;
      float nextZ = mon.worldZ + sZ;
      // Wall sliding: try full move, then X-only, then Z-only
      if (IsWalkable(nextX, nextZ)) {
        mon.worldX = nextX;
        mon.worldZ = nextZ;
      } else if (std::abs(sX) > 0.01f &&
                 IsWalkable(mon.worldX + sX, mon.worldZ)) {
        mon.worldX += sX;
      } else if (std::abs(sZ) > 0.01f &&
                 IsWalkable(mon.worldX, mon.worldZ + sZ)) {
        mon.worldZ += sZ;
      }
      mon.gridY = static_cast<uint8_t>(mon.worldX / 100.0f);
      mon.gridX = static_cast<uint8_t>(mon.worldZ / 100.0f);
      uint8_t spawnGX = static_cast<uint8_t>(mon.spawnZ / 100.0f);
      uint8_t spawnGY = static_cast<uint8_t>(mon.spawnX / 100.0f);
      emitMoveIfChanged(mon, spawnGX, spawnGY, false, true, outMoves);
    }
  };

  if (players.empty()) {
    for (auto &mon : m_monsterInstances) {
      if (mon.state != MonsterInstance::ALIVE)
        continue;
      if (mon.isChasing || mon.isReturning)
        moveTowardSpawn(mon);
    }
    return attacks;
  }

  for (auto &mon : m_monsterInstances) {
    if (mon.state != MonsterInstance::ALIVE)
      continue;

    // If returning to spawn after leash, keep returning — don't re-aggro
    if (mon.isReturning) {
      mon.aggroTargetFd = -1; // Drop aggro when returning
      moveTowardSpawn(mon);
      continue;
    }

    // Tick respawn immunity timer toward 0 (negative = immune to proximity
    // aggro)
    if (mon.aggroTimer < 0.0f) {
      mon.aggroTimer += dt;
      if (mon.aggroTimer >= 0.0f)
        mon.aggroTimer = 0.0f; // Immunity expired
    }

    // Update aggro timer (positive = time remaining on active aggro)
    // Only decrement when NOT in respawn immunity (Block A may have just
    // clamped timer to 0.0f this frame — don't push it negative again)
    if (mon.aggroTargetFd != -1 && mon.aggroTimer > 0.0f) {
      mon.aggroTimer -= dt;
      if (mon.aggroTimer <= 0.0f) {
        mon.aggroTargetFd = -1; // Aggro expired
        mon.aggroTimer = 0.0f;
      }
    }

    // Passive out-of-combat HP regeneration (~1% Max HP per second roughly)
    // ONLY regenerate if:
    // 1. Not chasing
    // 2. Not returning to spawn
    // 3. Not in recent combat (aggroTimer <= 0)
    if (!mon.isChasing && !mon.isReturning && mon.aggroTimer <= 0.0f) {
      if (mon.hp > 0 && mon.hp < mon.maxHp) {
        // Probabilistic check: ~1% max HP per second on average
        if (rand() % 1000 < (int)(dt * 1000.0f)) {
          int heal = std::max(1, mon.maxHp / 100);
          mon.hp = std::min(mon.maxHp, mon.hp + heal);

          MonsterAttackResult hpUpdate;
          hpUpdate.targetFd = -1; // Silent update for all
          hpUpdate.monsterIndex = mon.index;
          hpUpdate.damage = 0;
          hpUpdate.damageType = 0;
          hpUpdate.remainingHp = mon.hp;
          attacks.push_back(hpUpdate);

          printf("[AI] Mon %d Regened HP: %d/%d\n", mon.index, mon.hp,
                 mon.maxHp);
        }
      }
    }

    // Find best target (prioritize aggro target)
    float bestDist = 1e9f;
    const PlayerTarget *bestTarget = nullptr;

    // First check aggro target
    if (mon.aggroTargetFd != -1) {
      for (auto &p : players) {
        if (p.fd == mon.aggroTargetFd && !p.dead) {
          float dx = mon.worldX - p.worldX;
          float dz = mon.worldZ - p.worldZ;
          float dist = std::sqrt(dx * dx + dz * dz);
          // Only stick to aggro target if within reasonable range (2x aggro)
          if (dist < mon.aggroRange * 3.0f) {
            bestDist = dist;
            bestTarget = &p;
          } else {
            mon.aggroTargetFd = -1; // Lost track
          }
          break;
        }
      }
      // If aggro target not found (disconnected) or dead, clear and return
      if (!bestTarget && mon.aggroTargetFd != -1) {
        mon.aggroTargetFd = -1;
        if (mon.isChasing) {
          mon.isChasing = false;
          mon.isReturning = true;
          mon.hp = mon.maxHp;
          moveTowardSpawn(mon);
        }
      }
    }

    // If no aggro target, look for closest player
    if (!bestTarget) {
      for (auto &p : players) {
        if (p.dead)
          continue;
        float dx = mon.worldX - p.worldX;
        float dz = mon.worldZ - p.worldZ;
        float dist = std::sqrt(dx * dx + dz * dz);
        if (dist < bestDist) {
          bestDist = dist;
          bestTarget = &p;
        }
      }
    }
    if (!bestTarget) {
      if (mon.isChasing) {
        mon.isReturning = true;
        mon.hp = mon.maxHp; // Reset HP on aggro loss
        moveTowardSpawn(mon);
      }
      continue;
    }

    // Safe zone check: don't aggro if player is in safe zone
    if (IsSafeZone(bestTarget->worldX, bestTarget->worldZ)) {
      mon.isChasing = false;
      mon.isReturning = true;
      mon.hp = mon.maxHp;     // Reset HP on aggro loss
      mon.aggroTargetFd = -1; // Drop aggro immediately
      mon.aggroTimer = 0.0f;
      moveTowardSpawn(mon);
      continue;
    }

    // Check distance from spawn (leash)
    float dxSpawn = mon.worldX - mon.spawnX;
    float dzSpawn = mon.worldZ - mon.spawnZ;
    float distFromSpawn = std::sqrt(dxSpawn * dxSpawn + dzSpawn * dzSpawn);

    if (mon.isChasing) {
      // Already chasing — leash check (distance from spawn)
      if (distFromSpawn > LEASH_RANGE) {
        mon.isReturning = true;
        mon.hp = mon.maxHp; // Reset HP on leash
        moveTowardSpawn(mon);
        continue;
      }
      // De-aggro check: player ran beyond aggro range * 2 — give up chase
      if (bestDist > mon.aggroRange * 3.0f) {
        mon.isChasing = false;
        mon.isReturning = true;
        mon.hp = mon.maxHp; // Reset HP on de-aggro
        moveTowardSpawn(mon);
        continue;
      }
    } else {
      // Not chasing — initial aggro check
      // Passive (yellow) monsters never proximity-aggro, only fight back
      // Allow fight-back if monster has active aggro target (was hit by player)
      bool inImmunity = (mon.aggroTimer < 0.0f);
      bool hasExplicitAggro = (mon.aggroTargetFd != -1);
      if ((!mon.aggressive && !hasExplicitAggro) || bestDist > mon.aggroRange ||
          distFromSpawn > LEASH_RANGE || (inImmunity && !hasExplicitAggro)) {
        continue;
      }
    }

    if (!mon.isChasing) {
      mon.stuckFrames = 0;
      mon.prevChaseX = mon.worldX;
      mon.prevChaseZ = mon.worldZ;
    }
    mon.isChasing = true;
    mon.isWandering = false;

    // Compute target: player's grid cell
    uint8_t playerGridX = static_cast<uint8_t>(bestTarget->worldZ / 100.0f);
    uint8_t playerGridY = static_cast<uint8_t>(bestTarget->worldX / 100.0f);

    if (bestDist > ATTACK_RANGE) {
      // Chase: move server position toward player (with walkability check)
      float dx = bestTarget->worldX - mon.worldX;
      float dz = bestTarget->worldZ - mon.worldZ;
      float step = CHASE_SPEED * dt;
      if (step > bestDist)
        step = bestDist;
      float stepX = (dx / bestDist) * step;
      float stepZ = (dz / bestDist) * step;
      float nextX = mon.worldX + stepX;
      float nextZ = mon.worldZ + stepZ;

      if (IsSafeZone(nextX, nextZ)) {
        mon.isChasing = false;
        mon.isReturning = true;
        mon.aggroTargetFd = -1;
        moveTowardSpawn(mon);
        continue;
      }

      // Wall sliding: try full move, then X-only, then Z-only
      if (IsWalkable(nextX, nextZ)) {
        mon.worldX = nextX;
        mon.worldZ = nextZ;
      } else if (std::abs(stepX) > 0.01f &&
                 IsWalkable(mon.worldX + stepX, mon.worldZ)) {
        mon.worldX += stepX;
      } else if (std::abs(stepZ) > 0.01f &&
                 IsWalkable(mon.worldX, mon.worldZ + stepZ)) {
        mon.worldZ += stepZ;
      }

      // Stuck detection: if monster barely moved, increment counter
      float movedDx = mon.worldX - mon.prevChaseX;
      float movedDz = mon.worldZ - mon.prevChaseZ;
      float movedDist = movedDx * movedDx + movedDz * movedDz;
      if (movedDist < 1.0f) {
        mon.stuckFrames++;
        if (mon.stuckFrames > 60) { // ~3 seconds at 20 ticks/sec
          mon.isChasing = false;
          mon.isReturning = true;
          mon.aggroTargetFd = -1;
          mon.stuckFrames = 0;
          mon.hp = mon.maxHp;
          printf("[AI] Mon %d (type %d): STUCK for %d frames, returning to "
                 "spawn\n",
                 mon.index, mon.type, 60);
          moveTowardSpawn(mon);
          continue;
        }
      } else {
        mon.stuckFrames = 0;
      }
      mon.prevChaseX = mon.worldX;
      mon.prevChaseZ = mon.worldZ;

      mon.gridY = static_cast<uint8_t>(mon.worldX / 100.0f);
      mon.gridX = static_cast<uint8_t>(mon.worldZ / 100.0f);

      // Broadcast player's grid cell as target (event-driven: only when cell
      // changes)
      emitMoveIfChanged(mon, playerGridX, playerGridY, true, true, outMoves);
    } else {
      // In attack range — update server grid pos, broadcast standing position
      mon.gridY = static_cast<uint8_t>(mon.worldX / 100.0f);
      mon.gridX = static_cast<uint8_t>(mon.worldZ / 100.0f);
      emitMoveIfChanged(mon, mon.gridX, mon.gridY, true, false, outMoves);

      if (mon.attackCooldown <= 0.0f) {
        // Fresh distance check
        float freshDx = bestTarget->worldX - mon.worldX;
        float freshDz = bestTarget->worldZ - mon.worldZ;
        float freshDist = std::sqrt(freshDx * freshDx + freshDz * freshDz);
        if (freshDist > ATTACK_RANGE) {
          continue;
        }

        // Wall check: verify midpoint between monster and target is walkable
        float midX = (mon.worldX + bestTarget->worldX) * 0.5f;
        float midZ = (mon.worldZ + bestTarget->worldZ) * 0.5f;
        if (!IsWalkable(midX, midZ)) {
          continue; // Can't attack through walls
        }

        int dmg =
            mon.attackMin + (mon.attackMax > mon.attackMin
                                 ? rand() % (mon.attackMax - mon.attackMin + 1)
                                 : 0);

        bool missed = false;
        if (bestTarget->defenseRate > 0 &&
            rand() % std::max(1, mon.attackRate) < bestTarget->defenseRate) {
          missed = true;
        }

        if (missed) {
          dmg = 0;
        } else {
          dmg = std::max(0, dmg - bestTarget->defense);
        }

        printf("[AI] Mon %d (type %d): ATTACK dmg=%d%s dist=%.0f (range=%.0f) "
               "cd=%.1fs monPos=(%.0f,%.0f) playerPos=(%.0f,%.0f)\n",
               mon.index, mon.type, dmg, missed ? " (MISS)" : "", bestDist,
               ATTACK_RANGE, mon.atkCooldownTime, mon.worldX, mon.worldZ,
               bestTarget->worldX, bestTarget->worldZ);

        MonsterAttackResult result;
        result.targetFd = bestTarget->fd;
        result.monsterIndex = mon.index;
        result.damage = static_cast<uint16_t>(dmg);
        result.remainingHp = static_cast<uint16_t>(bestTarget->life);
        attacks.push_back(result);

        mon.attackCooldown = mon.atkCooldownTime;
        // Keep combat timer active when monster attacks
        mon.aggroTimer = 10.0f;
      }
    }
  }
  return attacks;
}

MonsterInstance *GameWorld::FindMonster(uint16_t index) {
  for (auto &mon : m_monsterInstances) {
    if (mon.index == index)
      return &mon;
  }
  return nullptr;
}

// Legacy monster viewport (0x1F) — still uses grid positions, no HP
std::vector<uint8_t> GameWorld::BuildMonsterViewportPacket() const {
  if (m_monsterInstances.empty())
    return {};

  size_t entrySize = sizeof(PMSG_MONSTER_VIEWPORT_ENTRY);
  size_t totalSize = sizeof(PMSG_MONSTER_VIEWPORT_HEAD) +
                     m_monsterInstances.size() * entrySize;

  std::vector<uint8_t> packet(totalSize, 0);

  auto *head = reinterpret_cast<PMSG_MONSTER_VIEWPORT_HEAD *>(packet.data());
  head->h = MakeC1Header(static_cast<uint8_t>(totalSize), 0x1F);
  head->count = static_cast<uint8_t>(m_monsterInstances.size());

  auto *entries = reinterpret_cast<PMSG_MONSTER_VIEWPORT_ENTRY *>(
      packet.data() + sizeof(PMSG_MONSTER_VIEWPORT_HEAD));
  for (size_t i = 0; i < m_monsterInstances.size(); i++) {
    const auto &mon = m_monsterInstances[i];
    auto &e = entries[i];
    e.typeH = static_cast<uint8_t>(mon.type >> 8);
    e.typeL = static_cast<uint8_t>(mon.type & 0xFF);
    e.x = mon.gridX;
    e.y = mon.gridY;
    e.dir = mon.dir;
  }

  return packet;
}

// New v2 monster viewport (0x34) — includes index, HP, state
std::vector<uint8_t> GameWorld::BuildMonsterViewportV2Packet() const {
  if (m_monsterInstances.empty())
    return {};

  // C1 header (3 bytes) + count (1 byte) + N * 12 bytes per entry
  size_t entrySize = sizeof(PMSG_MONSTER_VIEWPORT_ENTRY_V2);
  size_t totalSize = 4 + m_monsterInstances.size() * entrySize;

  // Use C2 header for potentially large packet
  std::vector<uint8_t> packet(totalSize + 1, 0); // +1 for C2 4-byte header
  totalSize = 5 + m_monsterInstances.size() * entrySize;
  packet.resize(totalSize);

  auto *head = reinterpret_cast<PWMSG_HEAD *>(packet.data());
  *head = MakeC2Header(static_cast<uint16_t>(totalSize), 0x34);
  packet[4] = static_cast<uint8_t>(m_monsterInstances.size());

  auto *entries =
      reinterpret_cast<PMSG_MONSTER_VIEWPORT_ENTRY_V2 *>(packet.data() + 5);
  for (size_t i = 0; i < m_monsterInstances.size(); i++) {
    const auto &mon = m_monsterInstances[i];
    auto &e = entries[i];
    e.indexH = static_cast<uint8_t>(mon.index >> 8);
    e.indexL = static_cast<uint8_t>(mon.index & 0xFF);
    e.typeH = static_cast<uint8_t>(mon.type >> 8);
    e.typeL = static_cast<uint8_t>(mon.type & 0xFF);
    e.x = mon.gridX;
    e.y = mon.gridY;
    e.dir = mon.dir;
    e.hp = static_cast<uint16_t>(mon.hp);
    e.maxHp = static_cast<uint16_t>(mon.maxHp);
    e.state = static_cast<uint8_t>(mon.state);
  }

  return packet;
}

// ── 0.97d Lorencia Drop Tables ──
// defIndex = category*32 + itemIndex
// Weight = relative chance within the pool (higher = more common)

struct DropEntry {
  int16_t defIndex; // category*32 + itemIndex (-1 = skip)
  int weight;       // Relative weight for weighted random pick
  uint8_t maxPlus;  // Max enhancement level (+0..+N)
};

// Helper: pick a weighted random entry
static const DropEntry &PickWeighted(const std::vector<DropEntry> &pool) {
  int total = 0;
  for (auto &e : pool)
    total += e.weight;
  int roll = rand() % total;
  int acc = 0;
  for (auto &e : pool) {
    acc += e.weight;
    if (roll < acc)
      return e;
  }
  return pool.back();
}

// Per-monster pools (defIndex = cat*32 + idx)
// Cat  0=Sword  1=Axe   2=Mace  3=Spear  4=Bow   5=Staff
// Cat  6=Shield 7=Helm  8=Armor 9=Pants 10=Glove 11=Boot
// Cat 12=Wing  13=Helper 14=Potion 15=Scroll

// Spider (type 3, Lv2) — weakest, almost no drops
static const std::vector<DropEntry> s_spiderDrops = {
    {14 * 32 + 1, 30, 0}, // Small Healing Potion
    {14 * 32 + 4, 20, 0}, // Small Mana Potion
    {0 * 32 + 0, 5, 0},   // Kris
    {1 * 32 + 0, 5, 0},   // Small Axe
    {15 * 32 + 3, 3, 0},  // Scroll of Fire Ball
};

// Budge Dragon (type 2, Lv4)
static const std::vector<DropEntry> s_budgeDrops = {
    {14 * 32 + 1, 25, 0}, // Small Healing Potion
    {14 * 32 + 4, 20, 0}, // Small Mana Potion
    {0 * 32 + 0, 8, 0},   // Kris
    {0 * 32 + 1, 6, 0},   // Short Sword
    {1 * 32 + 0, 6, 0},   // Small Axe
    {1 * 32 + 1, 4, 0},   // Hand Axe
    {5 * 32 + 0, 5, 0},   // Skull Staff
    {6 * 32 + 0, 4, 0},   // Small Shield
    {10 * 32 + 2, 3, 0},  // Pad Gloves
    {11 * 32 + 2, 3, 0},  // Pad Boots
    {15 * 32 + 3, 3, 0},  // Scroll of Fire Ball
    {15 * 32 + 10, 2, 0}, // Scroll of Power Wave
};

// Bull Fighter (type 0, Lv6)
static const std::vector<DropEntry> s_bullDrops = {
    {14 * 32 + 1, 20, 0}, // Small Healing Potion
    {14 * 32 + 2, 10, 0}, // Medium Healing Potion
    {14 * 32 + 4, 15, 0}, // Small Mana Potion
    {0 * 32 + 0, 6, 1},   // Kris +0/+1
    {0 * 32 + 1, 5, 1},   // Short Sword
    {0 * 32 + 2, 3, 0},   // Rapier
    {1 * 32 + 0, 5, 1},   // Small Axe
    {1 * 32 + 1, 4, 0},   // Hand Axe
    {2 * 32 + 0, 3, 0},   // Mace
    {5 * 32 + 0, 4, 1},   // Skull Staff
    {6 * 32 + 0, 4, 1},   // Small Shield
    {6 * 32 + 4, 3, 0},   // Buckler
    {7 * 32 + 2, 3, 0},   // Pad Helm
    {8 * 32 + 2, 3, 0},   // Pad Armor
    {9 * 32 + 2, 2, 0},   // Pad Pants
    {10 * 32 + 2, 3, 0},  // Pad Gloves
    {11 * 32 + 2, 3, 0},  // Pad Boots
    {7 * 32 + 5, 2, 0},   // Leather Helm
    {8 * 32 + 5, 2, 0},   // Leather Armor
    {15 * 32 + 3, 3, 0},  // Scroll of Fire Ball
    {15 * 32 + 10, 2, 0}, // Scroll of Power Wave
};

// Hound (type 1, Lv9)
static const std::vector<DropEntry> s_houndDrops = {
    {14 * 32 + 2, 15, 0}, // Medium Healing Potion
    {14 * 32 + 5, 12, 0}, // Medium Mana Potion
    {0 * 32 + 1, 5, 1},   // Short Sword
    {0 * 32 + 2, 5, 1},   // Rapier
    {0 * 32 + 4, 3, 0},   // Sword of Assassin
    {1 * 32 + 1, 4, 1},   // Hand Axe
    {1 * 32 + 2, 3, 0},   // Double Axe
    {2 * 32 + 0, 4, 1},   // Mace
    {2 * 32 + 1, 3, 0},   // Morning Star
    {4 * 32 + 0, 4, 0},   // Short Bow
    {4 * 32 + 1, 3, 0},   // Bow
    {4 * 32 + 8, 3, 0},   // Crossbow
    {5 * 32 + 0, 3, 1},   // Skull Staff
    {6 * 32 + 0, 3, 1},   // Small Shield
    {6 * 32 + 1, 3, 0},   // Horn Shield
    {6 * 32 + 4, 3, 1},   // Buckler
    {7 * 32 + 2, 3, 1},   // Pad Helm
    {7 * 32 + 5, 3, 0},   // Leather Helm
    {7 * 32 + 10, 2, 0},  // Vine Helm
    {8 * 32 + 2, 3, 1},   // Pad Armor
    {8 * 32 + 5, 3, 0},   // Leather Armor
    {8 * 32 + 10, 2, 0},  // Vine Armor
    {15 * 32 + 2, 3, 0},  // Scroll of Lighting
    {15 * 32 + 5, 2, 0},  // Scroll of Teleport
    {13 * 32 + 8, 1, 0},  // Ring of Ice
};

// Elite Bull Fighter (type 4, Lv12)
static const std::vector<DropEntry> s_eliteBullDrops = {
    {14 * 32 + 2, 12, 0}, // Medium Healing Potion
    {14 * 32 + 5, 10, 0}, // Medium Mana Potion
    {0 * 32 + 2, 4, 1},   // Rapier
    {0 * 32 + 3, 4, 1},   // Katana
    {0 * 32 + 4, 3, 0},   // Sword of Assassin
    {1 * 32 + 2, 4, 1},   // Double Axe
    {1 * 32 + 3, 3, 0},   // Tomahawk
    {2 * 32 + 1, 4, 1},   // Morning Star
    {2 * 32 + 2, 3, 0},   // Flail
    {3 * 32 + 5, 3, 0},   // Double Poleaxe
    {3 * 32 + 2, 3, 0},   // Dragon Lance
    {4 * 32 + 1, 3, 1},   // Bow
    {4 * 32 + 9, 3, 0},   // Golden Crossbow
    {5 * 32 + 1, 3, 0},   // Angelic Staff
    {6 * 32 + 1, 3, 1},   // Horn Shield
    {6 * 32 + 2, 3, 0},   // Kite Shield
    {6 * 32 + 6, 2, 0},   // Skull Shield
    {7 * 32 + 0, 3, 0},   // Bronze Helm
    {7 * 32 + 4, 3, 0},   // Bone Helm
    {8 * 32 + 0, 3, 0},   // Bronze Armor
    {8 * 32 + 4, 3, 0},   // Bone Armor
    {9 * 32 + 0, 2, 0},   // Bronze Pants
    {10 * 32 + 0, 2, 0},  // Bronze Gloves
    {11 * 32 + 0, 2, 0},  // Bronze Boots
    {15 * 32 + 2, 3, 0},  // Scroll of Lighting
    {15 * 32 + 6, 2, 0},  // Scroll of Ice
    {13 * 32 + 9, 1, 0},  // Ring of Poison
};

// Lich (type 6, Lv14) — caster, drops staves and magic items
static const std::vector<DropEntry> s_lichDrops = {
    {14 * 32 + 2, 10, 0}, // Medium Healing Potion
    {14 * 32 + 3, 5, 0},  // Large Healing Potion
    {14 * 32 + 5, 8, 0},  // Medium Mana Potion
    {14 * 32 + 6, 4, 0},  // Large Mana Potion
    {0 * 32 + 3, 3, 1},   // Katana
    {0 * 32 + 5, 3, 0},   // Blade
    {0 * 32 + 6, 3, 0},   // Gladius
    {1 * 32 + 3, 3, 1},   // Tomahawk
    {1 * 32 + 4, 2, 0},   // Elven Axe
    {2 * 32 + 2, 3, 1},   // Flail
    {3 * 32 + 1, 3, 0},   // Spear
    {3 * 32 + 6, 3, 0},   // Halberd
    {4 * 32 + 2, 3, 0},   // Elven Bow
    {4 * 32 + 10, 2, 0},  // Arquebus
    {5 * 32 + 1, 3, 1},   // Angelic Staff
    {5 * 32 + 2, 3, 0},   // Serpent Staff
    {6 * 32 + 2, 3, 1},   // Kite Shield
    {6 * 32 + 3, 2, 0},   // Elven Shield
    {7 * 32 + 4, 3, 1},   // Bone Helm
    {7 * 32 + 11, 2, 0},  // Silk Helm
    {8 * 32 + 4, 3, 1},   // Bone Armor
    {8 * 32 + 11, 2, 0},  // Silk Armor
    {15 * 32 + 0, 3, 0},  // Scroll of Poison
    {15 * 32 + 6, 3, 0},  // Scroll of Ice
    {15 * 32 + 7, 2, 0},  // Scroll of Twister
    {13 * 32 + 12, 1, 0}, // Pendant of Lighting
    {13 * 32 + 8, 1, 0},  // Ring of Ice
};

// Giant (type 7, Lv17) — strong melee, drops DK gear
static const std::vector<DropEntry> s_giantDrops = {
    {14 * 32 + 3, 8, 0},  // Large Healing Potion
    {14 * 32 + 6, 6, 0},  // Large Mana Potion
    {0 * 32 + 5, 3, 1},   // Blade
    {0 * 32 + 6, 3, 1},   // Gladius
    {0 * 32 + 7, 2, 0},   // Falchion
    {1 * 32 + 4, 3, 1},   // Elven Axe
    {1 * 32 + 5, 2, 0},   // Battle Axe
    {2 * 32 + 2, 3, 1},   // Flail
    {2 * 32 + 3, 2, 0},   // Great Hammer
    {3 * 32 + 6, 3, 1},   // Halberd
    {3 * 32 + 7, 2, 0},   // Berdysh
    {4 * 32 + 2, 3, 1},   // Elven Bow
    {4 * 32 + 3, 2, 0},   // Battle Bow
    {4 * 32 + 11, 2, 0},  // Light Crossbow
    {5 * 32 + 2, 3, 1},   // Serpent Staff
    {5 * 32 + 3, 2, 0},   // Thunder Staff
    {6 * 32 + 6, 3, 1},   // Skull Shield
    {6 * 32 + 9, 2, 0},   // Plate Shield
    {6 * 32 + 10, 2, 0},  // Big Round Shield
    {7 * 32 + 0, 3, 1},   // Bronze Helm
    {7 * 32 + 6, 2, 0},   // Scale Helm
    {7 * 32 + 12, 2, 0},  // Wind Helm
    {8 * 32 + 0, 3, 1},   // Bronze Armor
    {8 * 32 + 6, 2, 0},   // Scale Armor
    {8 * 32 + 12, 2, 0},  // Wind Armor
    {9 * 32 + 5, 2, 0},   // Leather Pants
    {9 * 32 + 6, 2, 0},   // Scale Pants
    {10 * 32 + 5, 2, 0},  // Leather Gloves
    {11 * 32 + 5, 2, 0},  // Leather Boots
    {15 * 32 + 7, 2, 0},  // Scroll of Twister
    {13 * 32 + 12, 1, 0}, // Pendant of Lighting
    {13 * 32 + 13, 1, 0}, // Pendant of Fire
};

// Skeleton Warrior (type 14, Lv19) — best Lorencia drops
static const std::vector<DropEntry> s_skelDrops = {
    {14 * 32 + 3, 8, 0},  // Large Healing Potion
    {14 * 32 + 6, 6, 0},  // Large Mana Potion
    {0 * 32 + 6, 3, 2},   // Gladius
    {0 * 32 + 7, 3, 1},   // Falchion
    {0 * 32 + 8, 2, 0},   // Serpent Sword
    {1 * 32 + 4, 3, 1},   // Elven Axe
    {1 * 32 + 5, 3, 0},   // Battle Axe
    {1 * 32 + 6, 2, 0},   // Nikkea Axe
    {2 * 32 + 2, 3, 1},   // Flail
    {2 * 32 + 3, 2, 0},   // Great Hammer
    {3 * 32 + 7, 3, 1},   // Berdysh
    {3 * 32 + 3, 2, 0},   // Giant Trident
    {4 * 32 + 3, 3, 0},   // Battle Bow
    {4 * 32 + 11, 3, 0},  // Light Crossbow
    {5 * 32 + 2, 3, 1},   // Serpent Staff
    {5 * 32 + 3, 2, 0},   // Thunder Staff
    {6 * 32 + 7, 3, 0},   // Spiked Shield
    {6 * 32 + 9, 2, 0},   // Plate Shield
    {7 * 32 + 6, 3, 1},   // Scale Helm
    {7 * 32 + 8, 2, 0},   // Brass Helm
    {7 * 32 + 12, 2, 0},  // Wind Helm
    {7 * 32 + 13, 1, 0},  // Spirit Helm
    {8 * 32 + 6, 3, 1},   // Scale Armor
    {8 * 32 + 8, 2, 0},   // Brass Armor
    {8 * 32 + 12, 2, 0},  // Wind Armor
    {9 * 32 + 6, 2, 1},   // Scale Pants
    {9 * 32 + 8, 2, 0},   // Brass Pants
    {10 * 32 + 6, 2, 1},  // Scale Gloves
    {11 * 32 + 6, 2, 1},  // Scale Boots
    {15 * 32 + 7, 2, 0},  // Scroll of Twister
    {15 * 32 + 8, 1, 0},  // Scroll of Evil Spirit
    {13 * 32 + 8, 1, 0},  // Ring of Ice
    {13 * 32 + 9, 1, 0},  // Ring of Poison
    {13 * 32 + 12, 1, 0}, // Pendant of Lighting
    {13 * 32 + 13, 1, 0}, // Pendant of Fire
};

// Map monster type → drop pool
static const std::vector<DropEntry> &GetDropPool(uint16_t monsterType) {
  switch (monsterType) {
  case 3:
    return s_spiderDrops;
  case 2:
    return s_budgeDrops;
  case 0:
    return s_bullDrops;
  case 1:
    return s_houndDrops;
  case 4:
    return s_eliteBullDrops;
  case 6:
    return s_lichDrops;
  case 7:
    return s_giantDrops;
  case 14:
    return s_skelDrops;
  default:
    return s_bullDrops; // Fallback
  }
}

std::vector<GroundDrop> GameWorld::SpawnDrops(float worldX, float worldZ,
                                              int monsterLevel,
                                              uint16_t monsterType,
                                              Database &db) {
  std::vector<GroundDrop> spawned;

  auto makeDrop = [&](int16_t defIndex, uint8_t qty, uint8_t lvl) {
    GroundDrop drop{};
    drop.index = m_nextDropIndex++;
    drop.defIndex = defIndex;
    drop.quantity = qty;
    drop.itemLevel = lvl;
    drop.worldX = worldX + (float)(rand() % 60 - 30);
    drop.worldZ = worldZ + (float)(rand() % 60 - 30);
    drop.age = 0.0f;
    m_drops.push_back(drop);
    spawned.push_back(drop);
  };

  // 1. Zen Drop — 40% chance
  if (rand() % 100 < 40) {
    uint32_t zenAmount = monsterLevel * 10 + (rand() % (monsterLevel * 10 + 1));
    if (zenAmount < 1)
      zenAmount = 1;
    uint8_t zen = std::min(255, (int)zenAmount);
    makeDrop(-1, zen, 0);
    return spawned; // Zen alone
  }

  // 2. Jewel Drops — Rare (Chaos 0.1%, Bless 0.05%, Soul 0.05%)
  {
    int jewelRoll = rand() % 10000;
    if (jewelRoll < 10) {
      // Jewel of Chaos (12*32+15 = 399)
      makeDrop(12 * 32 + 15, 1, 0);
      printf("[World] RARE: Jewel of Chaos from MonType %d!\n", monsterType);
      return spawned;
    } else if (monsterLevel >= 10 && jewelRoll < 15) {
      // Jewel of Bless (14*32+13 = 461)
      makeDrop(14 * 32 + 13, 1, 0);
      printf("[World] RARE: Jewel of Bless from MonType %d!\n", monsterType);
      return spawned;
    } else if (monsterLevel >= 10 && jewelRoll < 20) {
      // Jewel of Soul (14*32+14 = 462)
      makeDrop(14 * 32 + 14, 1, 0);
      printf("[World] RARE: Jewel of Soul from MonType %d!\n", monsterType);
      return spawned;
    }
  }

  // 3. Item Drop — 8-15% (scales with monster level)
  int itemChance = 8 + monsterLevel / 3; // 8% at Lv2, ~14% at Lv19
  if (rand() % 100 < itemChance) {
    const auto &pool = GetDropPool(monsterType);
    const auto &picked = PickWeighted(pool);

    // Random enhancement level +0 to max
    uint8_t dropLvl = 0;
    if (picked.maxPlus > 0) {
      dropLvl = rand() % (picked.maxPlus + 1);
    }

    makeDrop(picked.defIndex, 1, dropLvl);
    printf("[World] Drop: defIdx=%d +%d from MonType %d (Lv%d)\n",
           picked.defIndex, dropLvl, monsterType, monsterLevel);
    return spawned;
  }

  // 4. Potion Drop — 20% fallback
  if (rand() % 5 == 0) {
    // Scale potion type with monster level
    int16_t potCode;
    if (monsterLevel <= 5)
      potCode = 14 * 32 + 1; // Small HP
    else if (monsterLevel <= 12)
      potCode = 14 * 32 + 2; // Medium HP
    else
      potCode = 14 * 32 + 3; // Large HP

    // 50% chance for mana potion instead
    if (rand() % 2 == 0) {
      if (monsterLevel <= 5)
        potCode = 14 * 32 + 4; // Small Mana
      else if (monsterLevel <= 12)
        potCode = 14 * 32 + 5; // Medium Mana
      else
        potCode = 14 * 32 + 6; // Large Mana
    }
    makeDrop(potCode, 1, 0);
  }

  return spawned;
}

GroundDrop *GameWorld::FindDrop(uint16_t dropIndex) {
  for (auto &drop : m_drops) {
    if (drop.index == dropIndex)
      return &drop;
  }
  return nullptr;
}

bool GameWorld::RemoveDrop(uint16_t dropIndex) {
  for (auto it = m_drops.begin(); it != m_drops.end(); ++it) {
    if (it->index == dropIndex) {
      m_drops.erase(it);
      return true;
    }
  }
  return false;
}
