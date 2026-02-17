#ifndef MU_GAME_WORLD_HPP
#define MU_GAME_WORLD_HPP

#include "Database.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct NpcSpawn {
  uint16_t index;   // Unique object index (1001+)
  uint16_t type;    // NPC type ID (253=Amy, 250=Merchant, etc.)
  uint8_t x;        // Grid X
  uint8_t y;        // Grid Y
  uint8_t dir;      // Facing direction (0-7)
  std::string name; // Display name (for logging)
};

// Live monster state (server-authoritative)
struct MonsterInstance {
  uint16_t index;       // Unique ID (2001+)
  uint16_t type;        // Monster type (3=Spider)
  float worldX, worldZ; // World position (TERRAIN_SCALE units)
  float spawnX, spawnZ; // Original spawn point for respawn
  uint8_t gridX, gridY; // Grid position (for viewport packets)
  uint8_t dir;
  int hp, maxHp;
  int defense, defenseRate;
  int attackMin, attackMax;
  int attackRate;
  int level;

  enum State : uint8_t { ALIVE = 0, DYING = 1, DEAD = 2 };
  State state = ALIVE;
  float stateTimer = 0.0f;     // Time in current state
  float attackCooldown = 0.0f; // Cooldown between attacks
  bool justRespawned = false;  // Set true on respawn, cleared after broadcast

  // Server-side chase movement
  bool isChasing = false;
  bool isReturning = false; // Returning to spawn after leash — don't re-aggro
  uint8_t lastBroadcastTargetX = 0;  // Last broadcasted target grid X
  uint8_t lastBroadcastTargetY = 0;  // Last broadcasted target grid Y
  bool lastBroadcastChasing = false; // Last broadcasted chasing state

  // Per-type AI parameters (from Monster.txt: AtkSpeed, MvRange, ViewRange)
  float atkCooldownTime = 1.8f; // AtkSpeed/1000 (seconds between attacks)
  float wanderRange = 200.0f;   // MvRange * 100 (world units)
  float aggroRange = 500.0f;    // ViewRange * 100 (world units)

  // Server-side wander
  float wanderTimer = 0.0f;   // Countdown to next wander move
  float wanderTargetX = 0.0f; // Current wander destination (world coords)
  float wanderTargetZ = 0.0f;
  bool isWandering = false; // Currently walking to a wander target
};

// Server-side ground drop
struct GroundDrop {
  uint16_t index;  // Unique drop ID
  int8_t defIndex; // -1=Zen, 0-5=item def index
  uint8_t quantity;
  uint8_t itemLevel; // Enhancement +0..+2
  float worldX, worldZ;
  float age = 0.0f; // Seconds since spawn (despawn after 30s)
};

class GameWorld {
public:
  // Load NPCs and monsters from database
  void LoadNpcsFromDB(Database &db, uint8_t mapId = 0);
  void LoadMonstersFromDB(Database &db, uint8_t mapId = 0);

  // Player info for server-side monster AI
  struct PlayerTarget {
    int fd;
    float worldX, worldZ;
    int defense;
    int defenseRate;
    bool dead;
  };

  // Monster attack result to broadcast
  struct MonsterAttackResult {
    int targetFd; // Which player to send to
    uint16_t monsterIndex;
    uint16_t damage;
    uint16_t remainingHp; // Player's remaining HP after damage
  };

  // Monster target update to broadcast (event-driven, not periodic)
  struct MonsterMoveUpdate {
    uint16_t monsterIndex;
    uint8_t targetX, targetY; // Grid cell the monster is heading toward
    uint8_t chasing;          // 1=chasing player, 0=returning to spawn/idle
  };

  // Game tick — updates monster respawn timers, drop aging, wander AI
  // dropExpiredCallback called for each expired drop index
  // outWanderMoves populated with wander position updates to broadcast
  void Update(float dt,
              std::function<void(uint16_t)> dropExpiredCallback = nullptr,
              std::vector<MonsterMoveUpdate> *outWanderMoves = nullptr);

  // Check monster aggro/attacks against player positions. Returns attacks to
  // broadcast. Also populates outMoves with position updates for moving
  // monsters.
  std::vector<MonsterAttackResult>
  ProcessMonsterAI(float dt, const std::vector<PlayerTarget> &players,
                   std::vector<MonsterMoveUpdate> &outMoves);

  const std::vector<NpcSpawn> &GetNpcs() const { return m_npcs; }
  const std::vector<MonsterInstance> &GetMonsterInstances() const {
    return m_monsterInstances;
  }

  // Find monster by unique index (returns nullptr if not found)
  MonsterInstance *FindMonster(uint16_t index);

  // Build viewport packets
  std::vector<uint8_t> BuildNpcViewportPacket() const;
  std::vector<uint8_t> BuildMonsterViewportPacket() const; // Legacy 0x1F
  std::vector<uint8_t>
  BuildMonsterViewportV2Packet() const; // New 0x34 with HP/state

  // Drop management
  std::vector<GroundDrop> SpawnDrops(float worldX, float worldZ);
  GroundDrop *FindDrop(uint16_t dropIndex);
  bool RemoveDrop(uint16_t dropIndex);
  const std::vector<GroundDrop> &GetDrops() const { return m_drops; }

  // Monster stats (from Monster.txt Main 5.2 — identical to 0.97k for Lorencia
  // mobs)

  // Bull Fighter (type 0) — Level 6, MvRange 3, View 5, AtkSpeed 1600
  static constexpr int BULL_HP = 100;
  static constexpr int BULL_DEFENSE = 6;
  static constexpr int BULL_DEFENSE_RATE = 6;
  static constexpr int BULL_ATTACK_MIN = 16;
  static constexpr int BULL_ATTACK_MAX = 20;
  static constexpr int BULL_ATTACK_RATE = 28;
  static constexpr int BULL_LEVEL = 6;

  // Hound (type 1) — Level 9, MvRange 3, View 5, AtkSpeed 1600
  static constexpr int HOUND_HP = 140;
  static constexpr int HOUND_DEFENSE = 9;
  static constexpr int HOUND_DEFENSE_RATE = 9;
  static constexpr int HOUND_ATTACK_MIN = 22;
  static constexpr int HOUND_ATTACK_MAX = 27;
  static constexpr int HOUND_ATTACK_RATE = 39;
  static constexpr int HOUND_LEVEL = 9;

  // Budge Dragon (type 2) — Level 4, MvRange 3, View 4, AtkSpeed 2000
  static constexpr int BUDGE_HP = 60;
  static constexpr int BUDGE_DEFENSE = 3;
  static constexpr int BUDGE_DEFENSE_RATE = 3;
  static constexpr int BUDGE_ATTACK_MIN = 10;
  static constexpr int BUDGE_ATTACK_MAX = 13;
  static constexpr int BUDGE_ATTACK_RATE = 18;
  static constexpr int BUDGE_LEVEL = 4;

  // Spider (type 3) — Level 2, MvRange 2, View 5 (Monster.txt Main 5.2)
  static constexpr int SPIDER_HP = 30;
  static constexpr int SPIDER_DEFENSE = 1;
  static constexpr int SPIDER_DEFENSE_RATE = 1;
  static constexpr int SPIDER_ATTACK_MIN = 4;
  static constexpr int SPIDER_ATTACK_MAX = 7;
  static constexpr int SPIDER_ATTACK_RATE = 8;
  static constexpr int SPIDER_LEVEL = 2;

  // Elite Bull Fighter (type 4) — Level 12, MvRange 3, View 4, AtkSpeed 1600
  static constexpr int ELITE_BULL_HP = 190;
  static constexpr int ELITE_BULL_DEFENSE = 15;
  static constexpr int ELITE_BULL_DEFENSE_RATE = 15;
  static constexpr int ELITE_BULL_ATTACK_MIN = 33;
  static constexpr int ELITE_BULL_ATTACK_MAX = 38;
  static constexpr int ELITE_BULL_ATTACK_RATE = 56;
  static constexpr int ELITE_BULL_LEVEL = 12;

  // Lich (type 6) — Level 14, MvRange 3, View 7, AtkSpeed 1800 (ranged caster)
  static constexpr int LICH_HP = 255;
  static constexpr int LICH_DEFENSE = 18;
  static constexpr int LICH_DEFENSE_RATE = 18;
  static constexpr int LICH_ATTACK_MIN = 40;
  static constexpr int LICH_ATTACK_MAX = 50;
  static constexpr int LICH_ATTACK_RATE = 72;
  static constexpr int LICH_LEVEL = 14;

  // Giant (type 7) — Level 17, MvRange 3, View 5, AtkSpeed 2000 (slow, strong)
  static constexpr int GIANT_HP = 400;
  static constexpr int GIANT_DEFENSE = 25;
  static constexpr int GIANT_DEFENSE_RATE = 25;
  static constexpr int GIANT_ATTACK_MIN = 57;
  static constexpr int GIANT_ATTACK_MAX = 62;
  static constexpr int GIANT_ATTACK_RATE = 89;
  static constexpr int GIANT_LEVEL = 17;

  // Skeleton Warrior (type 14) — Level 19, MvRange 3, View 5, AtkSpeed 1600
  static constexpr int SKEL_HP = 525;
  static constexpr int SKEL_DEFENSE = 30;
  static constexpr int SKEL_DEFENSE_RATE = 30;
  static constexpr int SKEL_ATTACK_MIN = 63;
  static constexpr int SKEL_ATTACK_MAX = 72;
  static constexpr int SKEL_ATTACK_RATE = 105;
  static constexpr int SKEL_LEVEL = 19;

  static constexpr float DYING_DURATION = 3.0f;
  static constexpr float RESPAWN_DELAY = 10.0f;
  static constexpr float DROP_DESPAWN_TIME = 30.0f;

  // Monster AI constants (shared across all types)
  static constexpr float ATTACK_RANGE = 200.0f; // Melee range (2 grid cells)
  static constexpr float CHASE_SPEED =
      200.0f; // Chase/return speed (player=334, so player can outrun)
  static constexpr float WANDER_SPEED = 80.0f; // Slow patrol wander
  static constexpr float LEASH_RANGE =
      900.0f; // Max distance from spawn before returning
  // Per-type constants (atkCooldownTime, wanderRange, aggroRange) are on
  // MonsterInstance

  // Load terrain attributes (.att file) for walkability checks
  bool LoadTerrainAttributes(const std::string &attFilePath);
  bool IsWalkable(float worldX, float worldZ) const;
  bool IsSafeZone(float worldX, float worldZ) const;

  static constexpr int TERRAIN_SIZE = 256;
  static constexpr uint8_t TW_NOMOVE = 0x04;
  static constexpr uint8_t TW_SAFEZONE =
      0x01; // Safe zone — no monster aggro (from _define.h)

private:
  std::vector<NpcSpawn> m_npcs;
  std::vector<MonsterInstance> m_monsterInstances;
  std::vector<GroundDrop> m_drops;
  std::vector<uint8_t> m_terrainAttributes; // 256x256 attribute grid
  uint16_t m_nextMonsterIndex = 2001;
  uint16_t m_nextDropIndex = 1;
};

#endif // MU_GAME_WORLD_HPP
