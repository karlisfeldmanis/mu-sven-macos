#include "GameWorld.hpp"
#include "PacketDefs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

// ─── Terrain attribute loading (same decrypt as client TerrainParser) ───

static const uint8_t MAP_XOR_KEY[16] = {
    0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
    0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2};

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
        printf("[World] Cannot open terrain attribute file: %s\n", attFilePath.c_str());
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
        printf("[World] Loaded terrain attributes (WORD format, %zu bytes)\n", data.size());
    } else if (data.size() >= byteSize) {
        for (size_t i = 0; i < cells; i++)
            m_terrainAttributes[i] = data[4 + i];
        printf("[World] Loaded terrain attributes (BYTE format, %zu bytes)\n", data.size());
    } else {
        printf("[World] Terrain attribute file too small: %zu bytes\n", data.size());
        return false;
    }

    // Count walkable vs blocked vs safe zone
    int blocked = 0, safeZone = 0;
    for (size_t i = 0; i < cells; i++) {
        if (m_terrainAttributes[i] & TW_NOMOVE) blocked++;
        if (m_terrainAttributes[i] & TW_SAFEZONE) safeZone++;
    }
    printf("[World] Terrain: %d blocked cells, %d safe zone cells, %zu total\n",
           blocked, safeZone, cells);
    return true;
}

bool GameWorld::IsWalkable(float worldX, float worldZ) const {
    if (m_terrainAttributes.empty()) return true; // No data loaded = allow all
    int gz = (int)(worldX / 100.0f);
    int gx = (int)(worldZ / 100.0f);
    if (gx < 0 || gz < 0 || gx >= TERRAIN_SIZE || gz >= TERRAIN_SIZE)
        return false;
    return (m_terrainAttributes[gz * TERRAIN_SIZE + gx] & TW_NOMOVE) == 0;
}

bool GameWorld::IsSafeZone(float worldX, float worldZ) const {
    if (m_terrainAttributes.empty()) return false;
    int gz = (int)(worldX / 100.0f);
    int gx = (int)(worldZ / 100.0f);
    if (gx < 0 || gz < 0 || gx >= TERRAIN_SIZE || gz >= TERRAIN_SIZE)
        return false;
    return (m_terrainAttributes[gz * TERRAIN_SIZE + gx] & TW_SAFEZONE) != 0;
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
        m_npcs.push_back(npc);

        printf("[World] NPC #%d: type=%d pos=(%d,%d) dir=%d %s\n",
               npc.index, npc.type, npc.x, npc.y, npc.dir, npc.name.c_str());
    }

    printf("[World] Loaded %zu NPCs for map %d from database\n", m_npcs.size(), mapId);
}

std::vector<uint8_t> GameWorld::BuildNpcViewportPacket() const {
    size_t npcSize = sizeof(PMSG_VIEWPORT_NPC);
    size_t totalSize = sizeof(PMSG_VIEWPORT_HEAD) + m_npcs.size() * npcSize;

    std::vector<uint8_t> packet(totalSize, 0);

    auto *head = reinterpret_cast<PMSG_VIEWPORT_HEAD *>(packet.data());
    head->h = MakeC2Header(static_cast<uint16_t>(totalSize), 0x13);
    head->count = static_cast<uint8_t>(m_npcs.size());

    auto *entries = reinterpret_cast<PMSG_VIEWPORT_NPC *>(packet.data() + sizeof(PMSG_VIEWPORT_HEAD));
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

        // Set stats based on type (from Monster.txt Main 5.2)
        if (mon.type == 0) { // Bull Fighter — AtkSpeed=1600, MvRange=3, View=5
            mon.hp = BULL_HP;      mon.maxHp = BULL_HP;
            mon.defense = BULL_DEFENSE;
            mon.defenseRate = BULL_DEFENSE_RATE;
            mon.attackMin = BULL_ATTACK_MIN;
            mon.attackMax = BULL_ATTACK_MAX;
            mon.attackRate = BULL_ATTACK_RATE;
            mon.level = BULL_LEVEL;
            mon.atkCooldownTime = 1.6f;   // AtkSpeed=1600ms
            mon.wanderRange = 300.0f;     // MvRange=3
            mon.aggroRange = 500.0f;      // ViewRange=5
        } else if (mon.type == 1) { // Hound — AtkSpeed=1600, MvRange=3, View=5
            mon.hp = HOUND_HP;     mon.maxHp = HOUND_HP;
            mon.defense = HOUND_DEFENSE;
            mon.defenseRate = HOUND_DEFENSE_RATE;
            mon.attackMin = HOUND_ATTACK_MIN;
            mon.attackMax = HOUND_ATTACK_MAX;
            mon.attackRate = HOUND_ATTACK_RATE;
            mon.level = HOUND_LEVEL;
            mon.atkCooldownTime = 1.6f;   // AtkSpeed=1600ms
            mon.wanderRange = 300.0f;     // MvRange=3
            mon.aggroRange = 500.0f;      // ViewRange=5
        } else if (mon.type == 2) { // Budge Dragon — AtkSpeed=2000, MvRange=3, View=4
            mon.hp = BUDGE_HP;     mon.maxHp = BUDGE_HP;
            mon.defense = BUDGE_DEFENSE;
            mon.defenseRate = BUDGE_DEFENSE_RATE;
            mon.attackMin = BUDGE_ATTACK_MIN;
            mon.attackMax = BUDGE_ATTACK_MAX;
            mon.attackRate = BUDGE_ATTACK_RATE;
            mon.level = BUDGE_LEVEL;
            mon.atkCooldownTime = 2.0f;   // AtkSpeed=2000ms
            mon.wanderRange = 300.0f;     // MvRange=3
            mon.aggroRange = 400.0f;      // ViewRange=4
        } else if (mon.type == 3) { // Spider — AtkSpeed=1800, MvRange=2, View=5
            mon.hp = SPIDER_HP;    mon.maxHp = SPIDER_HP;
            mon.defense = SPIDER_DEFENSE;
            mon.defenseRate = SPIDER_DEFENSE_RATE;
            mon.attackMin = SPIDER_ATTACK_MIN;
            mon.attackMax = SPIDER_ATTACK_MAX;
            mon.attackRate = SPIDER_ATTACK_RATE;
            mon.level = SPIDER_LEVEL;
            mon.atkCooldownTime = 1.8f;   // AtkSpeed=1800ms
            mon.wanderRange = 200.0f;     // MvRange=2
            mon.aggroRange = 500.0f;      // ViewRange=5
        } else {
            mon.hp = 30;  mon.maxHp = 30;
            mon.defense = 3;  mon.defenseRate = 3;
            mon.attackMin = 4; mon.attackMax = 7;
            mon.attackRate = 8; mon.level = 4;
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

// Helper: broadcast a move update only when target grid cell or chasing state changes
static void emitMoveIfChanged(MonsterInstance &mon, uint8_t targetX, uint8_t targetY,
                               bool chasing, std::vector<GameWorld::MonsterMoveUpdate> &outMoves) {
    if (targetX != mon.lastBroadcastTargetX || targetY != mon.lastBroadcastTargetY ||
        chasing != mon.lastBroadcastChasing) {
        mon.lastBroadcastTargetX = targetX;
        mon.lastBroadcastTargetY = targetY;
        mon.lastBroadcastChasing = chasing;
        outMoves.push_back({mon.index, targetX, targetY, static_cast<uint8_t>(chasing ? 1 : 0)});
    }
}

void GameWorld::Update(float dt, std::function<void(uint16_t)> dropExpiredCallback,
                       std::vector<MonsterMoveUpdate> *outWanderMoves) {
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
                        if (step > dist) step = dist;
                        float nextX = mon.worldX + (dx / dist) * step;
                        float nextZ = mon.worldZ + (dz / dist) * step;
                        // Check walkability before moving
                        if (!IsWalkable(nextX, nextZ)) {
                            mon.isWandering = false;
                            mon.wanderTimer = 1.0f + (float)(rand() % 2000) / 1000.0f;
                        } else {
                            mon.worldX = nextX;
                            mon.worldZ = nextZ;
                            mon.gridY = static_cast<uint8_t>(mon.worldX / 100.0f);
                            mon.gridX = static_cast<uint8_t>(mon.worldZ / 100.0f);
                        }
                    }
                } else if (mon.wanderTimer <= 0.0f) {
                    // Pick a new random wander target within per-type wanderRange of spawn
                    // Retry up to 5 times to find a walkable target
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
                        printf("[AI] Mon %d (type %d): wander to (%.0f,%.0f) range=%.0f\n",
                               mon.index, mon.type, mon.wanderTargetX, mon.wanderTargetZ, mon.wanderRange);
                        if (outWanderMoves) {
                            uint8_t tgtGX = static_cast<uint8_t>(mon.wanderTargetZ / 100.0f);
                            uint8_t tgtGY = static_cast<uint8_t>(mon.wanderTargetX / 100.0f);
                            emitMoveIfChanged(mon, tgtGX, tgtGY, false, *outWanderMoves);
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
                mon.justRespawned = true;
                printf("[World] Monster %d (type %d) respawned at grid (%d,%d)\n",
                       mon.index, mon.type, mon.gridX, mon.gridY);
            }
            break;
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

std::vector<GameWorld::MonsterAttackResult> GameWorld::ProcessMonsterAI(
    float dt, const std::vector<PlayerTarget> &players,
    std::vector<MonsterMoveUpdate> &outMoves) {
    std::vector<MonsterAttackResult> attacks;

    // Helper: move a monster back to its spawn position
    auto moveTowardSpawn = [&](MonsterInstance &mon) {
        float dx = mon.spawnX - mon.worldX;
        float dz = mon.spawnZ - mon.worldZ;
        float distToSpawn = std::sqrt(dx * dx + dz * dz);
        if (distToSpawn < 10.0f) {
            // Arrived at spawn — fully reset
            if (mon.isReturning) {
                printf("[AI] Mon %d (type %d): arrived at spawn, resuming idle/wander\n",
                       mon.index, mon.type);
            }
            mon.isChasing = false;
            mon.isReturning = false;
            mon.isWandering = false;
            mon.wanderTimer = 2.0f + (float)(rand() % 3000) / 1000.0f;
            mon.worldX = mon.spawnX;
            mon.worldZ = mon.spawnZ;
            mon.gridY = static_cast<uint8_t>(mon.worldX / 100.0f);
            mon.gridX = static_cast<uint8_t>(mon.worldZ / 100.0f);
            emitMoveIfChanged(mon, mon.gridX, mon.gridY, false, outMoves);
        } else {
            float step = CHASE_SPEED * dt;
            if (step > distToSpawn) step = distToSpawn;
            float nextX = mon.worldX + (dx / distToSpawn) * step;
            float nextZ = mon.worldZ + (dz / distToSpawn) * step;
            if (IsWalkable(nextX, nextZ)) {
                mon.worldX = nextX;
                mon.worldZ = nextZ;
            }
            mon.gridY = static_cast<uint8_t>(mon.worldX / 100.0f);
            mon.gridX = static_cast<uint8_t>(mon.worldZ / 100.0f);
            uint8_t spawnGX = static_cast<uint8_t>(mon.spawnZ / 100.0f);
            uint8_t spawnGY = static_cast<uint8_t>(mon.spawnX / 100.0f);
            emitMoveIfChanged(mon, spawnGX, spawnGY, false, outMoves);
        }
    };

    if (players.empty()) {
        for (auto &mon : m_monsterInstances) {
            if (mon.state != MonsterInstance::ALIVE) continue;
            if (mon.isChasing || mon.isReturning) moveTowardSpawn(mon);
        }
        return attacks;
    }

    for (auto &mon : m_monsterInstances) {
        if (mon.state != MonsterInstance::ALIVE) continue;

        // If returning to spawn after leash, keep returning — don't re-aggro
        if (mon.isReturning) {
            moveTowardSpawn(mon);
            continue;
        }

        // Find closest alive player (skip dead)
        float bestDist = 1e9f;
        const PlayerTarget *bestTarget = nullptr;
        for (auto &p : players) {
            if (p.dead) continue;
            float dx = mon.worldX - p.worldX;
            float dz = mon.worldZ - p.worldZ;
            float dist = std::sqrt(dx * dx + dz * dz);
            if (dist < bestDist) {
                bestDist = dist;
                bestTarget = &p;
            }
        }
        if (!bestTarget) {
            if (mon.isChasing) {
                printf("[AI] Mon %d (type %d): no alive target, returning to spawn\n",
                       mon.index, mon.type);
                mon.isReturning = true;
                moveTowardSpawn(mon);
            }
            continue;
        }

        // Safe zone check: don't aggro if player is in safe zone (TW_SAFEZONE from _define.h)
        if (IsSafeZone(bestTarget->worldX, bestTarget->worldZ)) {
            if (mon.isChasing) {
                printf("[AI] Mon %d (type %d): target in SAFE ZONE at (%.0f,%.0f), returning to spawn\n",
                       mon.index, mon.type, bestTarget->worldX, bestTarget->worldZ);
                mon.isChasing = false;
                mon.isReturning = true;
                moveTowardSpawn(mon);
            }
            continue;
        }

        // Check distance from spawn (leash)
        float dxSpawn = mon.worldX - mon.spawnX;
        float dzSpawn = mon.worldZ - mon.spawnZ;
        float distFromSpawn = std::sqrt(dxSpawn * dxSpawn + dzSpawn * dzSpawn);

        if (mon.isChasing) {
            // Already chasing — leash check (distance from spawn)
            if (distFromSpawn > LEASH_RANGE) {
                printf("[AI] Mon %d (type %d): LEASH exceeded (%.0f > %.0f), returning to spawn\n",
                       mon.index, mon.type, distFromSpawn, LEASH_RANGE);
                mon.isReturning = true;
                moveTowardSpawn(mon);
                continue;
            }
            // De-aggro check: player ran beyond aggro range * 2 — give up chase
            if (bestDist > mon.aggroRange * 2.0f) {
                printf("[AI] Mon %d (type %d): player out of range (%.0f > %.0f), returning to spawn\n",
                       mon.index, mon.type, bestDist, mon.aggroRange * 2.0f);
                mon.isChasing = false;
                mon.isReturning = true;
                moveTowardSpawn(mon);
                continue;
            }
        } else {
            // Not chasing — initial aggro check
            if (bestDist > mon.aggroRange || distFromSpawn > LEASH_RANGE) {
                continue;
            }
        }

        // Chase or attack
        if (!mon.isChasing) {
            printf("[AI] Mon %d (type %d): AGGRO on player at dist=%.0f (range=%.0f)\n",
                   mon.index, mon.type, bestDist, mon.aggroRange);
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
            if (step > bestDist) step = bestDist;
            float nextX = mon.worldX + (dx / bestDist) * step;
            float nextZ = mon.worldZ + (dz / bestDist) * step;
            if (IsWalkable(nextX, nextZ)) {
                mon.worldX = nextX;
                mon.worldZ = nextZ;
            }
            mon.gridY = static_cast<uint8_t>(mon.worldX / 100.0f);
            mon.gridX = static_cast<uint8_t>(mon.worldZ / 100.0f);

            // Broadcast player's grid cell as target (event-driven: only when cell changes)
            emitMoveIfChanged(mon, playerGridX, playerGridY, true, outMoves);
        } else {
            // In attack range — update server grid pos, broadcast standing position
            mon.gridY = static_cast<uint8_t>(mon.worldX / 100.0f);
            mon.gridX = static_cast<uint8_t>(mon.worldZ / 100.0f);
            emitMoveIfChanged(mon, mon.gridX, mon.gridY, true, outMoves);

            if (mon.attackCooldown <= 0.0f) {
                int dmg = mon.attackMin + (mon.attackMax > mon.attackMin
                    ? rand() % (mon.attackMax - mon.attackMin + 1) : 0);

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

                printf("[AI] Mon %d (type %d): ATTACK dmg=%d%s dist=%.0f (range=%.0f) cd=%.1fs monPos=(%.0f,%.0f) playerPos=(%.0f,%.0f)\n",
                       mon.index, mon.type, dmg, missed ? " (MISS)" : "",
                       bestDist, ATTACK_RANGE, mon.atkCooldownTime,
                       mon.worldX, mon.worldZ, bestTarget->worldX, bestTarget->worldZ);

                MonsterAttackResult result;
                result.targetFd = bestTarget->fd;
                result.monsterIndex = mon.index;
                result.damage = static_cast<uint16_t>(dmg);
                result.remainingHp = 0;
                attacks.push_back(result);

                mon.attackCooldown = mon.atkCooldownTime;
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
    if (m_monsterInstances.empty()) return {};

    size_t entrySize = sizeof(PMSG_MONSTER_VIEWPORT_ENTRY);
    size_t totalSize = sizeof(PMSG_MONSTER_VIEWPORT_HEAD) + m_monsterInstances.size() * entrySize;

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
    if (m_monsterInstances.empty()) return {};

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

    auto *entries = reinterpret_cast<PMSG_MONSTER_VIEWPORT_ENTRY_V2 *>(packet.data() + 5);
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

// Drop system
std::vector<GroundDrop> GameWorld::SpawnDrops(float worldX, float worldZ) {
    std::vector<GroundDrop> spawned;

    auto makeDrop = [&](int8_t defIdx, uint8_t qty, uint8_t lvl) {
        GroundDrop drop{};
        drop.index = m_nextDropIndex++;
        drop.defIndex = defIdx;
        drop.quantity = qty;
        drop.itemLevel = lvl;
        drop.worldX = worldX + (float)(rand() % 60 - 30);
        drop.worldZ = worldZ + (float)(rand() % 60 - 30);
        drop.age = 0.0f;
        m_drops.push_back(drop);
        spawned.push_back(drop);
    };

    // Zen: 83% chance
    if (rand() % 12 < 10) {
        uint8_t zen = 1 + rand() % 20;
        makeDrop(-1, zen, 0);
    }
    // Item: 16.7% chance — weapons, shield, armor
    if (rand() % 120 < 20) {
        int8_t idx = (int8_t)(rand() % 5); // indices 0-4
        uint8_t enhLevel = (uint8_t)(rand() % 3); // +0, +1, +2
        makeDrop(idx, 1, enhLevel);
    }
    // Potion: 25% chance
    if (rand() % 4 == 0) {
        makeDrop(5, 1, 0); // Small Healing Potion
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
