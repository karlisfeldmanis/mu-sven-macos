#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "Camera.hpp"
#include "ClickEffect.hpp"
#include "FireEffect.hpp"
#include "GrassRenderer.hpp"
#include "HeroCharacter.hpp"
#include "MonsterManager.hpp"
#include "NetworkClient.hpp"
#include "NpcManager.hpp"
#include "../server/include/PacketDefs.hpp"
#include "ObjectRenderer.hpp"
#include "Screenshot.hpp"
#include "Shader.hpp"
#include "Sky.hpp"
#include "Terrain.hpp"
#include "TerrainParser.hpp"
#include "ViewerCommon.hpp"
#include "UICoords.hpp"
#include "HUD.hpp"
#include "MockData.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <streambuf>
#include <turbojpeg.h>
#include <unistd.h>

// Tee streambuf: writes to both a file and the original stream
class TeeStreambuf : public std::streambuf {
public:
  TeeStreambuf(std::streambuf *orig, std::streambuf *file)
      : original(orig), fileBuf(file) {}

protected:
  int overflow(int c) override {
    if (c == EOF)
      return !EOF;
    int r1 = original->sputc(c);
    int r2 = fileBuf->sputc(c);
    return (r1 == EOF || r2 == EOF) ? EOF : c;
  }
  int sync() override {
    original->pubsync();
    fileBuf->pubsync();
    return 0;
  }

private:
  std::streambuf *original;
  std::streambuf *fileBuf;
};

#ifdef __APPLE__
#include <objc/message.h>
#include <objc/runtime.h>
static void activateMacOSApp() {
  id app =
      ((id(*)(id, SEL))objc_msgSend)((id)objc_getClass("NSApplication"),
                                     sel_registerName("sharedApplication"));
  ((void (*)(id, SEL, long))objc_msgSend)(
      app, sel_registerName("setActivationPolicy:"),
      0); // NSApplicationActivationPolicyRegular
  ((void (*)(id, SEL, BOOL))objc_msgSend)(
      app, sel_registerName("activateIgnoringOtherApps:"), YES);
}
#endif

// GL error checking utility — call after critical GL operations
static void checkGLError(const char *label) {
  GLenum err;
  while ((err = glGetError()) != GL_NO_ERROR) {
    const char *errStr = "UNKNOWN";
    switch (err) {
    case GL_INVALID_ENUM:
      errStr = "INVALID_ENUM";
      break;
    case GL_INVALID_VALUE:
      errStr = "INVALID_VALUE";
      break;
    case GL_INVALID_OPERATION:
      errStr = "INVALID_OP";
      break;
    case GL_OUT_OF_MEMORY:
      errStr = "OUT_OF_MEMORY";
      break;
    case GL_INVALID_FRAMEBUFFER_OPERATION:
      errStr = "INVALID_FBO";
      break;
    }
    std::cerr << "[GL ERROR] " << errStr << " (0x" << std::hex << err
              << std::dec << ") at " << label << std::endl;
  }
}

// OpenGL debug callback (ARB_debug_output) — logs all GL warnings/errors
static void GLAPIENTRY glDebugCallback(GLenum source, GLenum type, GLuint id,
                                       GLenum severity, GLsizei /*length*/,
                                       const GLchar *message,
                                       const void * /*userParam*/) {
  // Skip notifications (very noisy)
  if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
    return;
  const char *sevStr = "???";
  switch (severity) {
  case GL_DEBUG_SEVERITY_HIGH:
    sevStr = "HIGH";
    break;
  case GL_DEBUG_SEVERITY_MEDIUM:
    sevStr = "MED";
    break;
  case GL_DEBUG_SEVERITY_LOW:
    sevStr = "LOW";
    break;
  }
  const char *typeStr = "other";
  switch (type) {
  case GL_DEBUG_TYPE_ERROR:
    typeStr = "ERROR";
    break;
  case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
    typeStr = "DEPRECATED";
    break;
  case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
    typeStr = "UNDEFINED";
    break;
  case GL_DEBUG_TYPE_PERFORMANCE:
    typeStr = "PERF";
    break;
  }
  std::cerr << "[GL " << sevStr << "/" << typeStr << "] " << message
            << std::endl;
}

Camera g_camera(glm::vec3(12800.0f, 0.0f, 12800.0f));
Terrain g_terrain;
ObjectRenderer g_objectRenderer;
FireEffect g_fireEffect;
Sky g_sky;
GrassRenderer g_grass;

// Point lights collected from light-emitting world objects
static const int MAX_POINT_LIGHTS = 64;
static std::vector<PointLight> g_pointLights;

// Hero character and click-to-move effect
static HeroCharacter g_hero;
static ClickEffect g_clickEffect;
static NpcManager g_npcManager;
static MonsterManager g_monsterManager;
static NetworkClient g_net;

// NPC interaction state
static int g_hoveredNpc = -1;  // Index of NPC under mouse cursor
static int g_selectedNpc = -1; // Index of NPC that was clicked (dialog open)

// ── Floating damage numbers ──
struct FloatingDamage {
  glm::vec3 worldPos;    // World position where damage occurred
  int damage;
  uint8_t type;          // 0=normal(orange), 2=excellent(green), 3=critical(blue), 7=miss, 8=incoming(red)
  float timer;           // Counts up from 0
  float maxTime;         // When to remove (1.5s)
  bool active;
};
static constexpr int MAX_FLOATING_DAMAGE = 32;
static FloatingDamage g_floatingDmg[MAX_FLOATING_DAMAGE] = {};

// ── Ground item drops ──
struct GroundItem {
  uint16_t dropIndex;
  int8_t defIndex;    // -1=Zen
  uint8_t quantity;
  uint8_t itemLevel;
  glm::vec3 position;
  float timer;        // Time alive (for bob animation)
  bool active;
};
static constexpr int MAX_GROUND_ITEMS = 64;
static GroundItem g_groundItems[MAX_GROUND_ITEMS] = {};
// Drop item names (matching server's SeedItemDefinitions)
static const char* GetDropName(int8_t defIndex) {
  switch (defIndex) {
    case -1: return "Zen";
    case 0: return "Short Sword";
    case 1: return "Kris";
    case 2: return "Small Shield";
    case 3: return "Leather Armor";
    case 4: return "Healing Potion";
    case 5: return "Mana Potion";
    default: return "Item";
  }
}

static void SpawnDamageNumber(const glm::vec3 &pos, int damage, uint8_t type) {
  for (auto &d : g_floatingDmg) {
    if (!d.active) {
      d.worldPos = pos + glm::vec3(((rand() % 40) - 20), 80.0f + (rand() % 30), ((rand() % 40) - 20));
      d.damage = damage;
      d.type = type;
      d.timer = 0.0f;
      d.maxTime = 1.5f;
      d.active = true;
      return;
    }
  }
}

// Server-received character stats for HUD
static int g_serverLevel = 1;
static int g_serverHP = 100, g_serverMaxHP = 100;
static int g_serverMP = 0, g_serverMaxMP = 0;
static int g_serverStr = 0, g_serverDex = 0, g_serverVit = 0, g_serverEne = 0;
static int g_serverLevelUpPoints = 0;
static int64_t g_serverXP = 0;

// Panel toggle state
static bool g_showCharInfo = false;
static bool g_showInventory = false;

// Client-side inventory (synced from server via 0x36)
struct ClientInventoryItem {
  int8_t defIndex = -2; // -2=empty
  uint8_t quantity = 0;
  uint8_t itemLevel = 0;
  bool occupied = false;
};
static constexpr int INVENTORY_SLOTS = 64;
static ClientInventoryItem g_inventory[INVENTORY_SLOTS] = {};
static uint32_t g_zen = 0;

// Equipment display (populated from 0x24 packet)
struct ClientEquipSlot {
  uint8_t category = 0xFF;
  uint8_t itemIndex = 0;
  uint8_t itemLevel = 0;
  std::string modelFile;
  bool equipped = false;
};
static ClientEquipSlot g_equipSlots[7] = {}; // 7 equipment slots

// Forward declarations for panel rendering
static UICoords g_hudCoords; // File-scope for mouse callback access

// Data received from server initial burst
struct ServerData {
  std::vector<ServerNpcSpawn> npcs;
  std::vector<ServerMonsterSpawn> monsters;
  std::vector<WeaponEquipInfo> equipment;
  bool connected = false;
};

// Parse a single MU packet from the initial server data burst
static void parseInitialPacket(const uint8_t *pkt, int pktSize,
                               ServerData &result) {
  if (pktSize < 3) return;
  uint8_t type = pkt[0];

  // C2 packets (4-byte header)
  if (type == 0xC2 && pktSize >= 5) {
    uint8_t headcode = pkt[3];

    // NPC viewport (0x13)
    if (headcode == 0x13) {
      uint8_t count = pkt[4];
      std::cout << "[Net] NPC viewport: " << (int)count << " NPCs" << std::endl;
      int entryStart = 5;
      for (int i = 0; i < count; i++) {
        int off = entryStart + i * 9;
        if (off + 9 > pktSize) break;
        ServerNpcSpawn npc;
        npc.type = (uint16_t)((pkt[off + 2] << 8) | pkt[off + 3]);
        npc.gridX = pkt[off + 4];
        npc.gridY = pkt[off + 5];
        npc.dir = pkt[off + 8] >> 4;
        result.npcs.push_back(npc);
        std::cout << "[Net]   NPC type=" << npc.type << " grid=("
                  << (int)npc.gridX << "," << (int)npc.gridY
                  << ") dir=" << (int)npc.dir << std::endl;
      }
    }

    // Monster viewport V2 (0x34) — includes index, HP, state
    if (headcode == 0x34) {
      uint8_t count = pkt[4];
      std::cout << "[Net] Monster viewport V2: " << (int)count << " monsters"
                << std::endl;
      int entryStart = 5;
      int entrySize = 12; // sizeof(PMSG_MONSTER_VIEWPORT_ENTRY_V2)
      for (int i = 0; i < count; i++) {
        int off = entryStart + i * entrySize;
        if (off + entrySize > pktSize) break;
        ServerMonsterSpawn mon;
        mon.serverIndex = (uint16_t)((pkt[off + 0] << 8) | pkt[off + 1]);
        mon.monsterType = (uint16_t)((pkt[off + 2] << 8) | pkt[off + 3]);
        mon.gridX = pkt[off + 4];
        mon.gridY = pkt[off + 5];
        mon.dir = pkt[off + 6];
        result.monsters.push_back(mon);
        std::cout << "[Net]   Monster idx=" << mon.serverIndex
                  << " type=" << mon.monsterType << " grid=("
                  << (int)mon.gridX << "," << (int)mon.gridY
                  << ") dir=" << (int)mon.dir << std::endl;
      }
    }

    // Inventory sync (0x36) — C2: header(4) + zen(4) + count(1) + N*item(4)
    if (headcode == 0x36 && pktSize >= 9) {
      uint32_t zen;
      std::memcpy(&zen, pkt + 4, 4);
      g_zen = zen;
      uint8_t count = pkt[8];
      for (auto &item : g_inventory) item = {};
      for (int i = 0; i < count; i++) {
        int off = 9 + i * 4;
        if (off + 4 > pktSize) break;
        uint8_t slot = pkt[off];
        if (slot < INVENTORY_SLOTS) {
          g_inventory[slot].defIndex = static_cast<int8_t>(pkt[off + 1]);
          g_inventory[slot].quantity = pkt[off + 2];
          g_inventory[slot].itemLevel = pkt[off + 3];
          g_inventory[slot].occupied = true;
        }
      }
      std::cout << "[Net] Inventory sync: " << (int)count << " items, zen=" << g_zen << std::endl;
    }
  }

  // C1 packets (2-byte header)
  if (type == 0xC1 && pktSize >= 3) {
    uint8_t headcode = pkt[2];

    // Monster viewport V1 (0x1F) — fallback if V2 not parsed
    if (headcode == 0x1F && pktSize >= 4) {
      uint8_t count = pkt[3];
      std::cout << "[Net] Monster viewport V1: " << (int)count << " monsters"
                << std::endl;
      int entryStart = 4;
      int entrySize = 5; // sizeof(PMSG_MONSTER_VIEWPORT_ENTRY)
      for (int i = 0; i < count; i++) {
        int off = entryStart + i * entrySize;
        if (off + entrySize > pktSize) break;
        ServerMonsterSpawn mon;
        mon.monsterType = (uint16_t)((pkt[off] << 8) | pkt[off + 1]);
        mon.gridX = pkt[off + 2];
        mon.gridY = pkt[off + 3];
        mon.dir = pkt[off + 4];
        // Only add if not already from V2
        if (result.monsters.empty())
          result.monsters.push_back(mon);
      }
    }

    // Equipment (0x24)
    if (headcode == 0x24 && pktSize >= 4) {
      uint8_t count = pkt[3];
      std::cout << "[Net] Equipment: " << (int)count << " slots" << std::endl;
      int entryStart = 4;
      int entrySize = 4 + 32;
      for (int i = 0; i < count; i++) {
        int off = entryStart + i * entrySize;
        if (off + entrySize > pktSize) break;
        WeaponEquipInfo weapon;
        uint8_t slot = pkt[off + 0];
        weapon.category = pkt[off + 1];
        weapon.itemIndex = pkt[off + 2];
        weapon.itemLevel = pkt[off + 3];
        char modelFile[33] = {};
        std::memcpy(modelFile, &pkt[off + 4], 32);
        weapon.modelFile = modelFile;
        std::cout << "[Net]   Slot " << (int)slot << ": " << weapon.modelFile
                  << " cat=" << (int)weapon.category
                  << " idx=" << (int)weapon.itemIndex << " +"
                  << (int)weapon.itemLevel << std::endl;
        if (slot == 0) result.equipment.push_back(weapon);
        // Populate all equipment display slots
        if (slot < 7) {
          g_equipSlots[slot].category = weapon.category;
          g_equipSlots[slot].itemIndex = weapon.itemIndex;
          g_equipSlots[slot].itemLevel = weapon.itemLevel;
          g_equipSlots[slot].modelFile = weapon.modelFile;
          g_equipSlots[slot].equipped = true;
        }
      }
    }

    // Character stats (0x25)
    if (headcode == 0x25 && pktSize >= (int)sizeof(PMSG_CHARSTATS_SEND)) {
      auto *stats = reinterpret_cast<const PMSG_CHARSTATS_SEND *>(pkt);
      g_serverLevel = stats->level;
      g_serverStr = stats->strength;
      g_serverDex = stats->dexterity;
      g_serverVit = stats->vitality;
      g_serverEne = stats->energy;
      g_serverHP = stats->life;
      g_serverMaxHP = stats->maxLife;
      g_serverLevelUpPoints = stats->levelUpPoints;
      g_serverXP = ((int64_t)stats->experienceHi << 32) | stats->experienceLo;
      std::cout << "[Net] Character stats: Lv." << g_serverLevel
                << " HP=" << g_serverHP << "/" << g_serverMaxHP
                << " STR=" << g_serverStr << " XP=" << g_serverXP << std::endl;
    }
  }
}

// Handle ongoing server packets (monster AI, combat, drops)
static void handleServerPacket(const uint8_t *pkt, int pktSize) {
  if (pktSize < 3) return;
  uint8_t type = pkt[0];

  if (type == 0xC1) {
    uint8_t headcode = pkt[2];

    // Monster move/chase (0x35)
    if (headcode == 0x35 && pktSize >= (int)sizeof(PMSG_MONSTER_MOVE_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_MOVE_SEND *>(pkt);
      int idx = g_monsterManager.FindByServerIndex(p->monsterIndex);
      if (idx >= 0) {
        float worldX = (float)p->targetY * 100.0f;
        float worldZ = (float)p->targetX * 100.0f;
        g_monsterManager.SetMonsterServerPosition(idx, worldX, worldZ,
                                                   p->chasing != 0);
      }
    }

    // Damage result (0x29) — player hits monster
    if (headcode == 0x29 && pktSize >= (int)sizeof(PMSG_DAMAGE_SEND)) {
      auto *p = reinterpret_cast<const PMSG_DAMAGE_SEND *>(pkt);
      int idx = g_monsterManager.FindByServerIndex(p->monsterIndex);
      if (idx >= 0) {
        MonsterInfo mi = g_monsterManager.GetMonsterInfo(idx);
        g_monsterManager.SetMonsterHP(idx, p->remainingHp, mi.maxHp);
        if (p->damageType > 0)
          g_monsterManager.TriggerHitAnimation(idx);
        // Floating damage number above monster
        uint8_t dmgType = p->damageType; // 0=miss, 1=normal, 2=critical, 3=excellent
        SpawnDamageNumber(mi.position, p->damage, dmgType == 0 ? 7 : dmgType);
      }
    }

    // Monster death (0x2A)
    if (headcode == 0x2A && pktSize >= (int)sizeof(PMSG_MONSTER_DEATH_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_DEATH_SEND *>(pkt);
      int idx = g_monsterManager.FindByServerIndex(p->monsterIndex);
      if (idx >= 0)
        g_monsterManager.SetMonsterDying(idx);
    }

    // Monster attack player (0x2F) — monster hits hero
    if (headcode == 0x2F && pktSize >= (int)sizeof(PMSG_MONSTER_ATTACK_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_ATTACK_SEND *>(pkt);
      int idx = g_monsterManager.FindByServerIndex(p->monsterIndex);
      if (idx >= 0)
        g_monsterManager.TriggerAttackAnimation(idx);
      g_serverHP = p->remainingHp;
      g_hero.TakeDamage(p->damage);
      // Red damage number above hero
      SpawnDamageNumber(g_hero.GetPosition(), p->damage, 8);
    }

    // Monster respawn (0x30)
    if (headcode == 0x30 && pktSize >= (int)sizeof(PMSG_MONSTER_RESPAWN_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_RESPAWN_SEND *>(pkt);
      int idx = g_monsterManager.FindByServerIndex(p->monsterIndex);
      if (idx >= 0)
        g_monsterManager.RespawnMonster(idx, p->x, p->y, p->hp);
    }

    // Ground drop spawned (0x2B)
    if (headcode == 0x2B && pktSize >= (int)sizeof(PMSG_DROP_SPAWN_SEND)) {
      auto *p = reinterpret_cast<const PMSG_DROP_SPAWN_SEND *>(pkt);
      for (auto &gi : g_groundItems) {
        if (!gi.active) {
          gi.dropIndex = p->dropIndex;
          gi.defIndex = p->defIndex;
          gi.quantity = p->quantity;
          gi.itemLevel = p->itemLevel;
          gi.position = glm::vec3(p->worldX, 0.0f, p->worldZ);
          gi.timer = 0.0f;
          gi.active = true;
          std::cout << "[Drop] Spawned " << GetDropName(p->defIndex)
                    << " (idx=" << p->dropIndex << ") at ("
                    << p->worldX << "," << p->worldZ << ")" << std::endl;
          break;
        }
      }
    }

    // Pickup result (0x2D) — gold or item acquired
    if (headcode == 0x2D && pktSize >= (int)sizeof(PMSG_PICKUP_RESULT_SEND)) {
      auto *p = reinterpret_cast<const PMSG_PICKUP_RESULT_SEND *>(pkt);
      if (p->success) {
        // Remove from ground
        for (auto &gi : g_groundItems) {
          if (gi.active && gi.dropIndex == p->dropIndex) {
            gi.active = false;
            break;
          }
        }
      }
    }

    // Drop removed (0x2E)
    if (headcode == 0x2E && pktSize >= (int)sizeof(PMSG_DROP_REMOVE_SEND)) {
      auto *p = reinterpret_cast<const PMSG_DROP_REMOVE_SEND *>(pkt);
      for (auto &gi : g_groundItems) {
        if (gi.active && gi.dropIndex == p->dropIndex) {
          gi.active = false;
          break;
        }
      }
    }

    // Stat allocation response (0x38)
    if (headcode == 0x38 && pktSize >= (int)sizeof(PMSG_STAT_ALLOC_SEND)) {
      auto *resp = reinterpret_cast<const PMSG_STAT_ALLOC_SEND *>(pkt);
      if (resp->result) {
        switch (resp->statType) {
          case 0: g_serverStr = resp->newValue; break;
          case 1: g_serverDex = resp->newValue; break;
          case 2: g_serverVit = resp->newValue; break;
          case 3: g_serverEne = resp->newValue; break;
        }
        g_serverLevelUpPoints = resp->levelUpPoints;
        g_serverMaxHP = resp->maxLife;
        std::cout << "[Net] Stat alloc: type=" << (int)resp->statType
                  << " val=" << resp->newValue << " pts=" << resp->levelUpPoints << std::endl;
      }
    }
  }

  // C2 packets (ongoing)
  if (type == 0xC2 && pktSize >= 5) {
    uint8_t headcode = pkt[3];

    // Inventory sync (0x36)
    if (headcode == 0x36 && pktSize >= 9) {
      uint32_t zen;
      std::memcpy(&zen, pkt + 4, 4);
      g_zen = zen;
      uint8_t count = pkt[8];
      for (auto &item : g_inventory) item = {};
      for (int i = 0; i < count; i++) {
        int off = 9 + i * 4;
        if (off + 4 > pktSize) break;
        uint8_t slot = pkt[off];
        if (slot < INVENTORY_SLOTS) {
          g_inventory[slot].defIndex = static_cast<int8_t>(pkt[off + 1]);
          g_inventory[slot].quantity = pkt[off + 2];
          g_inventory[slot].itemLevel = pkt[off + 3];
          g_inventory[slot].occupied = true;
        }
      }
    }
  }
}

static const TerrainData *g_terrainDataPtr = nullptr;

// Roof hiding: types 125 (HouseWall05) and 126 (HouseWall06) fade when
// hero stands on layer1 tile == 4 (building interior). Original:
// ZzzObject.cpp:3744
static std::unordered_map<int, float> g_typeAlpha = {{125, 1.0f}, {126, 1.0f}};
static std::unordered_map<int, float> g_typeAlphaTarget = {{125, 1.0f},
                                                           {126, 1.0f}};

struct LightTemplate {
  glm::vec3 color;
  float range;
  float heightOffset; // Y offset above object base for emission point
};

// Returns light properties for a given object type, or nullptr if not a light
static const LightTemplate *GetLightProperties(int type) {
  static const LightTemplate fireLightProps = {glm::vec3(1.5f, 0.9f, 0.5f),
                                               800.0f, 150.0f};
  static const LightTemplate bonfireProps = {glm::vec3(1.5f, 0.75f, 0.3f),
                                             1000.0f, 100.0f};
  static const LightTemplate gateProps = {glm::vec3(1.5f, 0.9f, 0.5f), 800.0f,
                                          200.0f};
  static const LightTemplate bridgeProps = {glm::vec3(1.2f, 0.7f, 0.4f), 700.0f,
                                            50.0f};
  static const LightTemplate streetLightProps = {glm::vec3(1.5f, 1.2f, 0.75f),
                                                 800.0f, 250.0f};
  static const LightTemplate candleProps = {glm::vec3(1.2f, 0.7f, 0.3f), 600.0f,
                                            80.0f};
  static const LightTemplate lightFixtureProps = {glm::vec3(1.2f, 0.85f, 0.5f),
                                                  700.0f, 150.0f};

  switch (type) {
  case 50:
  case 51:
    return &fireLightProps;
  case 52:
    return &bonfireProps;
  case 55:
    return &gateProps;
  case 80:
    return &bridgeProps;
  case 90:
    return &streetLightProps;
  case 130:
  case 131:
  case 132:
    return &lightFixtureProps;
  case 150:
    return &candleProps;
  default:
    return nullptr;
  }
}

// Forward declaration for NPC ray picking
static int rayPickNpc(GLFWwindow *window, double mouseX, double mouseY);

void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
  ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
  // Camera rotation disabled — fixed isometric angle like original MU

  // Update NPC hover state on cursor move
  if (!ImGui::GetIO().WantCaptureMouse) {
    g_hoveredNpc = rayPickNpc(window, xpos, ypos);
  } else {
    g_hoveredNpc = -1;
  }
}

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
  ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
  g_camera.ProcessMouseScroll(yoffset);
}

// --- Terrain helpers ---

static float getTerrainHeight(const TerrainData &td, float worldX,
                              float worldZ) {
  const int S = TerrainParser::TERRAIN_SIZE;
  // World → grid: WorldX maps to gridZ, WorldZ maps to gridX
  float gz = worldX / 100.0f;
  float gx = worldZ / 100.0f;
  gz = std::clamp(gz, 0.0f, (float)(S - 2));
  gx = std::clamp(gx, 0.0f, (float)(S - 2));
  int xi = (int)gx, zi = (int)gz;
  float xd = gx - (float)xi, zd = gz - (float)zi;
  float h00 = td.heightmap[zi * S + xi];
  float h10 = td.heightmap[zi * S + (xi + 1)];
  float h01 = td.heightmap[(zi + 1) * S + xi];
  float h11 = td.heightmap[(zi + 1) * S + (xi + 1)];
  return (h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
          h01 * (1 - xd) * zd + h11 * xd * zd);
}

static bool isWalkable(const TerrainData &td, float worldX, float worldZ) {
  const int S = TerrainParser::TERRAIN_SIZE;
  int gz = (int)(worldX / 100.0f);
  int gx = (int)(worldZ / 100.0f);
  if (gx < 0 || gz < 0 || gx >= S || gz >= S)
    return false;
  uint8_t attr = td.mapping.attributes[gz * S + gx];
  // Only TW_NOMOVE (0x04) blocks character movement.
  // TW_NOGROUND (0x08) is our bridge shader flag, not a movement blocker —
  // characters walk on bridges. Original uses iWall > byMapAttribute where
  // the raw .att data has TW_NOMOVE on actual walls/obstacles.
  return (attr & 0x04) == 0;
}

// --- Ray-terrain intersection for click-to-move ---

static bool screenToTerrain(GLFWwindow *window, double mouseX, double mouseY,
                            glm::vec3 &outWorld) {
  if (!g_terrainDataPtr)
    return false;

  int winW, winH;
  glfwGetWindowSize(window, &winW, &winH);

  // NDC coordinates
  float ndcX = (float)(2.0 * mouseX / winW - 1.0);
  float ndcY = (float)(1.0 - 2.0 * mouseY / winH);

  glm::mat4 proj = g_camera.GetProjectionMatrix((float)winW, (float)winH);
  glm::mat4 view = g_camera.GetViewMatrix();
  glm::mat4 invVP = glm::inverse(proj * view);

  // Unproject near and far points
  glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  nearPt /= nearPt.w;
  farPt /= farPt.w;

  glm::vec3 rayOrigin = glm::vec3(nearPt);
  glm::vec3 rayDir = glm::normalize(glm::vec3(farPt) - rayOrigin);

  // March along ray, find where it crosses the terrain
  float stepSize = 50.0f;
  float maxDist = 10000.0f;
  float prevT = 0.0f;
  float prevAbove = rayOrigin.y - getTerrainHeight(*g_terrainDataPtr,
                                                   rayOrigin.x, rayOrigin.z);

  for (float t = stepSize; t < maxDist; t += stepSize) {
    glm::vec3 p = rayOrigin + rayDir * t;
    // Bounds check
    if (p.x < 0 || p.z < 0 || p.x > 25500.0f || p.z > 25500.0f)
      continue;
    float terrH = getTerrainHeight(*g_terrainDataPtr, p.x, p.z);
    float above = p.y - terrH;

    if (above < 0.0f) {
      // Crossed below terrain — binary search for precise intersection
      float lo = prevT, hi = t;
      for (int i = 0; i < 8; ++i) {
        float mid = (lo + hi) * 0.5f;
        glm::vec3 mp = rayOrigin + rayDir * mid;
        float mh = getTerrainHeight(*g_terrainDataPtr, mp.x, mp.z);
        if (mp.y > mh)
          lo = mid;
        else
          hi = mid;
      }
      glm::vec3 hit = rayOrigin + rayDir * ((lo + hi) * 0.5f);
      outWorld = glm::vec3(
          hit.x, getTerrainHeight(*g_terrainDataPtr, hit.x, hit.z), hit.z);
      return true;
    }
    prevT = t;
    prevAbove = above;
  }
  return false;
}

// --- Ray-NPC picking (cylinder intersection) ---

static int rayPickNpc(GLFWwindow *window, double mouseX, double mouseY) {
  int winW, winH;
  glfwGetWindowSize(window, &winW, &winH);

  float ndcX = (float)(2.0 * mouseX / winW - 1.0);
  float ndcY = (float)(1.0 - 2.0 * mouseY / winH);

  glm::mat4 proj = g_camera.GetProjectionMatrix((float)winW, (float)winH);
  glm::mat4 view = g_camera.GetViewMatrix();
  glm::mat4 invVP = glm::inverse(proj * view);

  glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  nearPt /= nearPt.w;
  farPt /= farPt.w;

  glm::vec3 rayO = glm::vec3(nearPt);
  glm::vec3 rayD = glm::normalize(glm::vec3(farPt) - rayO);

  int bestIdx = -1;
  float bestT = 1e9f;

  for (int i = 0; i < g_npcManager.GetNpcCount(); ++i) {
    NpcInfo info = g_npcManager.GetNpcInfo(i);
    float r = info.radius;
    float yMin = info.position.y;
    float yMax = info.position.y + info.height;

    // Ray-cylinder intersection in XZ plane
    float dx = rayO.x - info.position.x;
    float dz = rayO.z - info.position.z;
    float a = rayD.x * rayD.x + rayD.z * rayD.z;
    float b = 2.0f * (dx * rayD.x + dz * rayD.z);
    float c = dx * dx + dz * dz - r * r;
    float disc = b * b - 4.0f * a * c;
    if (disc < 0)
      continue;

    float sqrtDisc = sqrtf(disc);
    float t0 = (-b - sqrtDisc) / (2.0f * a);
    float t1 = (-b + sqrtDisc) / (2.0f * a);

    // Check both intersection points
    for (float t : {t0, t1}) {
      if (t < 0)
        continue;
      float hitY = rayO.y + rayD.y * t;
      if (hitY >= yMin && hitY <= yMax && t < bestT) {
        bestT = t;
        bestIdx = i;
      }
    }
  }
  return bestIdx;
}

// --- Ray-Monster picking (cylinder intersection, same as NPC) ---

static int rayPickMonster(GLFWwindow *window, double mouseX, double mouseY) {
  int winW, winH;
  glfwGetWindowSize(window, &winW, &winH);

  float ndcX = (float)(2.0 * mouseX / winW - 1.0);
  float ndcY = (float)(1.0 - 2.0 * mouseY / winH);

  glm::mat4 proj = g_camera.GetProjectionMatrix((float)winW, (float)winH);
  glm::mat4 view = g_camera.GetViewMatrix();
  glm::mat4 invVP = glm::inverse(proj * view);

  glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  nearPt /= nearPt.w;
  farPt /= farPt.w;

  glm::vec3 rayO = glm::vec3(nearPt);
  glm::vec3 rayD = glm::normalize(glm::vec3(farPt) - rayO);

  int bestIdx = -1;
  float bestT = 1e9f;

  for (int i = 0; i < g_monsterManager.GetMonsterCount(); ++i) {
    MonsterInfo info = g_monsterManager.GetMonsterInfo(i);
    if (info.state == MonsterState::DEAD || info.state == MonsterState::DYING)
      continue;
    float r = info.radius * 1.8f; // Inflated pick radius for click forgiveness
    float yMin = info.position.y;
    float yMax = info.position.y + info.height;

    float dx = rayO.x - info.position.x;
    float dz = rayO.z - info.position.z;
    float a = rayD.x * rayD.x + rayD.z * rayD.z;
    float b = 2.0f * (dx * rayD.x + dz * rayD.z);
    float c = dx * dx + dz * dz - r * r;
    float disc = b * b - 4.0f * a * c;
    if (disc < 0)
      continue;

    float sqrtDisc = sqrtf(disc);
    float t0 = (-b - sqrtDisc) / (2.0f * a);
    float t1 = (-b + sqrtDisc) / (2.0f * a);

    for (float t : {t0, t1}) {
      if (t < 0)
        continue;
      float hitY = rayO.y + rayD.y * t;
      if (hitY >= yMin && hitY <= yMax && t < bestT) {
        bestT = t;
        bestIdx = i;
      }
    }
  }
  return bestIdx;
}

// Drag state for inventory (declared here for mouse callback access)
static int g_dragFromSlot = -1;
static int8_t g_dragDefIndex = -2;
static uint8_t g_dragQuantity = 0;
static uint8_t g_dragItemLevel = 0;
static bool g_isDragging = false;

// Forward declarations for panel interaction (defined later)
static bool HandlePanelClick(float vx, float vy);
static void HandlePanelMouseUp(float vx, float vy);

// --- Click-to-move mouse handler ---

void mouse_button_callback(GLFWwindow *window, int button, int action,
                           int mods) {
  ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

  // Click-to-move on left click (NPC click takes priority)
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    if (!ImGui::GetIO().WantCaptureMouse && g_terrainDataPtr) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);

      // Check if click is on a UI panel first
      if (g_showCharInfo || g_showInventory) {
        float vx = g_hudCoords.ToVirtualX((float)mx);
        float vy = g_hudCoords.ToVirtualY((float)my);
        if (HandlePanelClick(vx, vy)) return;
      }

      // Check NPC click first, then monster, then ground
      int npcHit = rayPickNpc(window, mx, my);
      if (npcHit >= 0) {
        g_selectedNpc = npcHit;
        g_hero.CancelAttack();
      } else {
        g_selectedNpc = -1;
        // Check monster click
        int monHit = rayPickMonster(window, mx, my);
        if (monHit >= 0) {
          MonsterInfo info = g_monsterManager.GetMonsterInfo(monHit);
          g_hero.AttackMonster(monHit, info.position);
        } else {
          // Ground click — move to terrain
          if (g_hero.IsAttacking())
            g_hero.CancelAttack();
          glm::vec3 target;
          if (screenToTerrain(window, mx, my, target)) {
            if (isWalkable(*g_terrainDataPtr, target.x, target.z)) {
              g_hero.MoveTo(target);
              g_clickEffect.Show(target);
            }
          }
        }
      }
    }
  }

  // Mouse up: handle drag release
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
    if (g_isDragging) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);
      float vx = g_hudCoords.ToVirtualX((float)mx);
      float vy = g_hudCoords.ToVirtualY((float)my);
      HandlePanelMouseUp(vx, vy);
    }
  }
}

void key_callback(GLFWwindow *window, int key, int scancode, int action,
                  int mods) {
  ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
  if (ImGui::GetIO().WantCaptureKeyboard) return;

  if (action == GLFW_PRESS) {
    if (key == GLFW_KEY_C) g_showCharInfo = !g_showCharInfo;
    if (key == GLFW_KEY_I) g_showInventory = !g_showInventory;
    if (key == GLFW_KEY_ESCAPE) {
      if (g_showCharInfo) g_showCharInfo = false;
      else if (g_showInventory) g_showInventory = false;
    }
  }
}

void char_callback(GLFWwindow *window, unsigned int c) {
  ImGui_ImplGlfw_CharCallback(window, c);
}

// --- Process input: hero movement + screenshot ---

void processInput(GLFWwindow *window, float deltaTime) {
  bool wasMoving = g_hero.IsMoving();
  g_hero.ProcessMovement(deltaTime);

  // Hide click effect when hero stops moving
  if (wasMoving && !g_hero.IsMoving())
    g_clickEffect.Hide();

  // Camera follows hero
  if (wasMoving)
    g_camera.SetPosition(g_hero.GetPosition());

  static bool pPressed = false;
  if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) {
    if (!pPressed) {
      Screenshot::Capture(window);
      pPressed = true;
    }
  } else {
    pPressed = false;
  }
}

// ═══════════════════════════════════════════════════════════════════════
// Panel rendering: Character Info (C) and Inventory (I)
// ═══════════════════════════════════════════════════════════════════════

static const char* GetEquipSlotName(int slot) {
  static const char* names[] = {"R.Hand", "L.Hand", "Helm", "Armor", "Pants", "Gloves", "Boots"};
  if (slot >= 0 && slot < 7) return names[slot];
  return "???";
}

static const char* GetItemNameByDef(int8_t defIndex) {
  switch (defIndex) {
    case 0: return "Short Sword";
    case 1: return "Kris";
    case 2: return "Small Shield";
    case 3: return "Leather Armor";
    case 4: return "Healing Pot";
    case 5: return "Mana Pot";
    default: return "Item";
  }
}

// Panel layout constants (virtual coords 1280x720)
static constexpr float PANEL_W = 290.0f;
static constexpr float PANEL_H = 560.0f;
static constexpr float PANEL_Y = 20.0f;
static constexpr float PANEL_X_RIGHT = 970.0f;

static float GetCharInfoPanelX() { return PANEL_X_RIGHT; }
static float GetInventoryPanelX() { return g_showCharInfo ? PANEL_X_RIGHT - PANEL_W - 10.0f : PANEL_X_RIGHT; }

// Check if virtual point is inside a panel
static bool IsPointInPanel(float vx, float vy, float panelX) {
  return vx >= panelX && vx < panelX + PANEL_W && vy >= PANEL_Y && vy < PANEL_Y + PANEL_H;
}

// Helper: draw centered text with shadow
static void DrawPanelText(ImDrawList* dl, const UICoords& c, float vx, float vy,
                          const char* text, ImU32 color, ImFont* font = nullptr) {
  float sx = c.ToScreenX(vx), sy = c.ToScreenY(vy);
  if (font)
    dl->AddText(font, font->LegacySize, ImVec2(sx+1, sy+1), IM_COL32(0,0,0,180), text);
  dl->AddText(ImVec2(sx+1, sy+1), IM_COL32(0,0,0,180), text);
  if (font)
    dl->AddText(font, font->LegacySize, ImVec2(sx, sy), color, text);
  else
    dl->AddText(ImVec2(sx, sy), color, text);
}

// Helper: draw right-aligned text
static void DrawPanelTextRight(ImDrawList* dl, const UICoords& c, float vx, float vy,
                               float width, const char* text, ImU32 color) {
  ImVec2 sz = ImGui::CalcTextSize(text);
  float sx = c.ToScreenX(vx + width) - sz.x;
  float sy = c.ToScreenY(vy);
  dl->AddText(ImVec2(sx+1, sy+1), IM_COL32(0,0,0,180), text);
  dl->AddText(ImVec2(sx, sy), color, text);
}

// Helper: draw centered text horizontally
static void DrawPanelTextCentered(ImDrawList* dl, const UICoords& c, float vx, float vy,
                                   float width, const char* text, ImU32 color) {
  ImVec2 sz = ImGui::CalcTextSize(text);
  float sx = c.ToScreenX(vx + width * 0.5f) - sz.x * 0.5f;
  float sy = c.ToScreenY(vy);
  dl->AddText(ImVec2(sx+1, sy+1), IM_COL32(0,0,0,180), text);
  dl->AddText(ImVec2(sx, sy), color, text);
}

static void RenderCharInfoPanel(ImDrawList* dl, const UICoords& c) {
  float px = GetCharInfoPanelX(), py = PANEL_Y;
  float pw = PANEL_W, ph = PANEL_H;

  // Colors
  const ImU32 colBg     = IM_COL32(15, 15, 25, 235);
  const ImU32 colBorder = IM_COL32(60, 65, 90, 200);
  const ImU32 colTitle  = IM_COL32(255, 210, 80, 255);
  const ImU32 colHeader = IM_COL32(200, 180, 120, 255);
  const ImU32 colLabel  = IM_COL32(170, 170, 190, 255);
  const ImU32 colValue  = IM_COL32(255, 255, 255, 255);
  const ImU32 colGreen  = IM_COL32(100, 255, 100, 255);
  const ImU32 colBtnN   = IM_COL32(40, 80, 40, 200);
  const ImU32 colBtnH   = IM_COL32(60, 120, 60, 230);
  const ImU32 colClose   = IM_COL32(200, 60, 60, 200);
  const ImU32 colCloseH  = IM_COL32(255, 80, 80, 230);
  const ImU32 colBar     = IM_COL32(30, 30, 45, 200);

  // Background + border
  ImVec2 pMin(c.ToScreenX(px), c.ToScreenY(py));
  ImVec2 pMax(c.ToScreenX(px + pw), c.ToScreenY(py + ph));
  dl->AddRectFilled(pMin, pMax, colBg, 6.0f);
  dl->AddRect(pMin, pMax, colBorder, 6.0f);

  // Close button [X]
  float closeX = px + pw - 28, closeY = py + 6;
  ImVec2 cMin(c.ToScreenX(closeX), c.ToScreenY(closeY));
  ImVec2 cMax(c.ToScreenX(closeX + 22), c.ToScreenY(closeY + 18));
  ImVec2 mp = ImGui::GetIO().MousePos;
  bool hoverClose = mp.x >= cMin.x && mp.x < cMax.x && mp.y >= cMin.y && mp.y < cMax.y;
  dl->AddRectFilled(cMin, cMax, hoverClose ? colCloseH : colClose, 3.0f);
  DrawPanelTextCentered(dl, c, closeX, closeY + 1, 22, "X", colValue);

  // Title
  DrawPanelTextCentered(dl, c, px, py + 10, pw, "Character Info", colTitle);

  // Separator
  dl->AddLine(ImVec2(c.ToScreenX(px + 10), c.ToScreenY(py + 32)),
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(py + 32)), colBorder);

  // Name + Class + Level
  DrawPanelTextCentered(dl, c, px, py + 40, pw, "TestDK", colValue);
  DrawPanelTextCentered(dl, c, px, py + 58, pw, "Dark Knight", colLabel);

  char buf[64];
  snprintf(buf, sizeof(buf), "Level %d", g_serverLevel);
  DrawPanelTextCentered(dl, c, px, py + 78, pw, buf, colHeader);

  // XP bar
  float xpFrac = 0.0f;
  uint64_t nextXp = g_hero.GetNextExperience();
  uint64_t curXp = (uint64_t)g_serverXP;
  uint64_t prevXp = g_hero.CalcXPForLevel(g_serverLevel);
  if (nextXp > prevXp) xpFrac = (float)(curXp - prevXp) / (float)(nextXp - prevXp);
  xpFrac = std::clamp(xpFrac, 0.0f, 1.0f);

  float barX = px + 15, barY = py + 100, barW = pw - 30, barH = 14;
  dl->AddRectFilled(ImVec2(c.ToScreenX(barX), c.ToScreenY(barY)),
                    ImVec2(c.ToScreenX(barX + barW), c.ToScreenY(barY + barH)), colBar, 3.0f);
  if (xpFrac > 0.0f)
    dl->AddRectFilled(ImVec2(c.ToScreenX(barX + 1), c.ToScreenY(barY + 1)),
                      ImVec2(c.ToScreenX(barX + 1 + (barW - 2) * xpFrac), c.ToScreenY(barY + barH - 1)),
                      IM_COL32(40, 180, 80, 255), 2.0f);
  snprintf(buf, sizeof(buf), "EXP %.1f%%", xpFrac * 100.0f);
  DrawPanelTextCentered(dl, c, barX, barY, barW, buf, colValue);

  // Separator
  dl->AddLine(ImVec2(c.ToScreenX(px + 10), c.ToScreenY(py + 122)),
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(py + 122)), colBorder);

  // Stats section
  DrawPanelText(dl, c, px + 15, py + 128, "Stats", colHeader);

  const char* statNames[] = {"Strength", "Dexterity", "Vitality", "Energy"};
  int statValues[] = {g_serverStr, g_serverDex, g_serverVit, g_serverEne};

  for (int i = 0; i < 4; i++) {
    float rowY = py + 150 + i * 32;

    // Stat label
    DrawPanelText(dl, c, px + 20, rowY + 2, statNames[i], colLabel);

    // Stat value
    snprintf(buf, sizeof(buf), "%d", statValues[i]);
    DrawPanelTextRight(dl, c, px + 100, rowY + 2, 80, buf, colValue);

    // "+" button (if points available)
    if (g_serverLevelUpPoints > 0) {
      float btnX = px + pw - 42, btnY = rowY;
      ImVec2 bMin(c.ToScreenX(btnX), c.ToScreenY(btnY));
      ImVec2 bMax(c.ToScreenX(btnX + 26), c.ToScreenY(btnY + 20));
      bool hoverBtn = mp.x >= bMin.x && mp.x < bMax.x && mp.y >= bMin.y && mp.y < bMax.y;
      dl->AddRectFilled(bMin, bMax, hoverBtn ? colBtnH : colBtnN, 3.0f);
      DrawPanelTextCentered(dl, c, btnX, btnY + 2, 26, "+", colValue);
    }
  }

  // Available points
  if (g_serverLevelUpPoints > 0) {
    snprintf(buf, sizeof(buf), "Points: %d", g_serverLevelUpPoints);
    DrawPanelText(dl, c, px + 20, py + 282, buf, colGreen);
  }

  // Separator
  dl->AddLine(ImVec2(c.ToScreenX(px + 10), c.ToScreenY(py + 302)),
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(py + 302)), colBorder);

  // Derived stats
  DrawPanelText(dl, c, px + 15, py + 308, "Combat", colHeader);

  // Compute derived stats (DK formulas)
  int dmgMin = g_serverStr / 8 + g_hero.GetWeaponBonusMin();
  int dmgMax = g_serverStr / 4 + g_hero.GetWeaponBonusMax();
  int defense = g_serverDex / 3 + g_hero.GetDefenseBonus();
  int atkRate = g_serverLevel * 5 + g_serverDex * 3 / 2 + g_serverStr / 4;
  int defRate = g_serverDex / 3;

  struct { const char* label; int val; char fmt[32]; } derived[] = {
    {"Damage",   0, ""},
    {"Defense",  defense, ""},
    {"Atk Rate", atkRate, ""},
    {"Def Rate", defRate, ""},
  };
  snprintf(derived[0].fmt, 32, "%d - %d", dmgMin, dmgMax);
  snprintf(derived[1].fmt, 32, "%d", defense);
  snprintf(derived[2].fmt, 32, "%d", atkRate);
  snprintf(derived[3].fmt, 32, "%d", defRate);

  for (int i = 0; i < 4; i++) {
    float rowY = py + 330 + i * 22;
    DrawPanelText(dl, c, px + 20, rowY, derived[i].label, colLabel);
    DrawPanelTextRight(dl, c, px + 130, rowY, 100, derived[i].fmt, colValue);
  }

  // Separator
  dl->AddLine(ImVec2(c.ToScreenX(px + 10), c.ToScreenY(py + 422)),
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(py + 422)), colBorder);

  // HP bar
  float hpFrac = g_serverMaxHP > 0 ? (float)g_serverHP / g_serverMaxHP : 0.0f;
  hpFrac = std::clamp(hpFrac, 0.0f, 1.0f);
  float hpBarX = px + 15, hpBarY = py + 432, hpBarW = pw - 30, hpBarH = 16;
  dl->AddRectFilled(ImVec2(c.ToScreenX(hpBarX), c.ToScreenY(hpBarY)),
                    ImVec2(c.ToScreenX(hpBarX + hpBarW), c.ToScreenY(hpBarY + hpBarH)), colBar, 3.0f);
  if (hpFrac > 0)
    dl->AddRectFilled(ImVec2(c.ToScreenX(hpBarX + 1), c.ToScreenY(hpBarY + 1)),
                      ImVec2(c.ToScreenX(hpBarX + 1 + (hpBarW - 2) * hpFrac), c.ToScreenY(hpBarY + hpBarH - 1)),
                      IM_COL32(180, 30, 30, 255), 2.0f);
  snprintf(buf, sizeof(buf), "HP %d / %d", g_serverHP, g_serverMaxHP);
  DrawPanelTextCentered(dl, c, hpBarX, hpBarY, hpBarW, buf, colValue);

  // MP bar
  float mpBarY = py + 454;
  dl->AddRectFilled(ImVec2(c.ToScreenX(hpBarX), c.ToScreenY(mpBarY)),
                    ImVec2(c.ToScreenX(hpBarX + hpBarW), c.ToScreenY(mpBarY + hpBarH)), colBar, 3.0f);
  dl->AddRectFilled(ImVec2(c.ToScreenX(hpBarX + 1), c.ToScreenY(mpBarY + 1)),
                    ImVec2(c.ToScreenX(hpBarX + 1 + (hpBarW - 2)), c.ToScreenY(mpBarY + hpBarH - 1)),
                    IM_COL32(40, 80, 200, 255), 2.0f);
  snprintf(buf, sizeof(buf), "MP %d / %d", g_serverMP, g_serverMaxMP);
  DrawPanelTextCentered(dl, c, hpBarX, mpBarY, hpBarW, buf, colValue);
}

static void RenderInventoryPanel(ImDrawList* dl, const UICoords& c) {
  float px = GetInventoryPanelX(), py = PANEL_Y;
  float pw = PANEL_W, ph = PANEL_H;

  const ImU32 colBg     = IM_COL32(15, 15, 25, 235);
  const ImU32 colBorder = IM_COL32(60, 65, 90, 200);
  const ImU32 colTitle  = IM_COL32(255, 210, 80, 255);
  const ImU32 colHeader = IM_COL32(200, 180, 120, 255);
  const ImU32 colLabel  = IM_COL32(170, 170, 190, 255);
  const ImU32 colValue  = IM_COL32(255, 255, 255, 255);
  const ImU32 colSlotBg = IM_COL32(25, 25, 40, 220);
  const ImU32 colSlotBr = IM_COL32(50, 50, 70, 180);
  const ImU32 colEquip  = IM_COL32(120, 200, 255, 255);
  const ImU32 colClose  = IM_COL32(200, 60, 60, 200);
  const ImU32 colCloseH = IM_COL32(255, 80, 80, 230);
  const ImU32 colGold   = IM_COL32(255, 215, 0, 255);
  const ImU32 colDragHi = IM_COL32(255, 255, 100, 100);

  // Background + border
  ImVec2 pMin(c.ToScreenX(px), c.ToScreenY(py));
  ImVec2 pMax(c.ToScreenX(px + pw), c.ToScreenY(py + ph));
  dl->AddRectFilled(pMin, pMax, colBg, 6.0f);
  dl->AddRect(pMin, pMax, colBorder, 6.0f);

  // Close button
  float closeX = px + pw - 28, closeY = py + 6;
  ImVec2 cMin(c.ToScreenX(closeX), c.ToScreenY(closeY));
  ImVec2 cMax(c.ToScreenX(closeX + 22), c.ToScreenY(closeY + 18));
  ImVec2 mp = ImGui::GetIO().MousePos;
  bool hoverClose = mp.x >= cMin.x && mp.x < cMax.x && mp.y >= cMin.y && mp.y < cMax.y;
  dl->AddRectFilled(cMin, cMax, hoverClose ? colCloseH : colClose, 3.0f);
  DrawPanelTextCentered(dl, c, closeX, closeY + 1, 22, "X", colValue);

  // Title
  DrawPanelTextCentered(dl, c, px, py + 10, pw, "Inventory", colTitle);
  dl->AddLine(ImVec2(c.ToScreenX(px + 10), c.ToScreenY(py + 32)),
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(py + 32)), colBorder);

  // Equipment section header
  DrawPanelText(dl, c, px + 15, py + 36, "Equipment", colHeader);

  // Equipment layout: body-shaped arrangement
  //           Helm
  //    R.Hand Armor L.Hand
  //    Gloves Pants Boots
  struct EquipPos { int slot; float x; float y; float w; float h; };
  EquipPos equipLayout[] = {
    {2, px+118, py+56,  54, 42},  // Helm - top center
    {0, px+52,  py+102, 54, 54},  // R.Hand - left
    {3, px+118, py+102, 54, 54},  // Armor - center
    {1, px+184, py+102, 54, 54},  // L.Hand - right
    {5, px+52,  py+162, 54, 42},  // Gloves - bottom left
    {4, px+118, py+162, 54, 42},  // Pants - bottom center
    {6, px+184, py+162, 54, 42},  // Boots - bottom right
  };

  char buf[64];
  for (auto &ep : equipLayout) {
    ImVec2 sMin(c.ToScreenX(ep.x), c.ToScreenY(ep.y));
    ImVec2 sMax(c.ToScreenX(ep.x + ep.w), c.ToScreenY(ep.y + ep.h));

    // Highlight if drag hovering over this slot
    bool hoverSlot = mp.x >= sMin.x && mp.x < sMax.x && mp.y >= sMin.y && mp.y < sMax.y;

    dl->AddRectFilled(sMin, sMax, colSlotBg, 3.0f);
    dl->AddRect(sMin, sMax, hoverSlot && g_isDragging ? colDragHi : colSlotBr, 3.0f);

    if (g_equipSlots[ep.slot].equipped) {
      // Show abbreviated item name
      const char* catNames[] = {"Sword", "Axe", "Mace", "Spear", "Bow", "Staff", "Shield"};
      uint8_t cat = g_equipSlots[ep.slot].category;
      const char* name = (cat < 7) ? catNames[cat] : "Item";
      if (g_equipSlots[ep.slot].itemLevel > 0) {
        snprintf(buf, sizeof(buf), "%s+%d", name, g_equipSlots[ep.slot].itemLevel);
      } else {
        snprintf(buf, sizeof(buf), "%s", name);
      }
      // Centered in slot
      ImVec2 sz = ImGui::CalcTextSize(buf);
      float tx = (sMin.x + sMax.x) * 0.5f - sz.x * 0.5f;
      float ty = (sMin.y + sMax.y) * 0.5f - sz.y * 0.5f;
      dl->AddText(ImVec2(tx + 1, ty + 1), IM_COL32(0,0,0,180), buf);
      dl->AddText(ImVec2(tx, ty), colEquip, buf);
    } else {
      // Show slot name dimmed
      const char* slotName = GetEquipSlotName(ep.slot);
      ImVec2 sz = ImGui::CalcTextSize(slotName);
      float tx = (sMin.x + sMax.x) * 0.5f - sz.x * 0.5f;
      float ty = (sMin.y + sMax.y) * 0.5f - sz.y * 0.5f;
      dl->AddText(ImVec2(tx, ty), IM_COL32(80, 80, 100, 150), slotName);
    }
  }

  // Separator
  dl->AddLine(ImVec2(c.ToScreenX(px + 10), c.ToScreenY(py + 210)),
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(py + 210)), colBorder);

  // Bag grid header
  DrawPanelText(dl, c, px + 15, py + 214, "Bag", colHeader);

  // 8x8 grid of items
  float gridX = px + 15, gridY = py + 234;
  float cellW = 32.0f, cellH = 32.0f, gap = 1.0f;

  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int slot = row * 8 + col;
      float cx = gridX + col * (cellW + gap);
      float cy = gridY + row * (cellH + gap);

      ImVec2 sMin(c.ToScreenX(cx), c.ToScreenY(cy));
      ImVec2 sMax(c.ToScreenX(cx + cellW), c.ToScreenY(cy + cellH));

      bool hoverCell = mp.x >= sMin.x && mp.x < sMax.x && mp.y >= sMin.y && mp.y < sMax.y;
      bool isBeingDragged = g_isDragging && g_dragFromSlot == slot;

      dl->AddRectFilled(sMin, sMax, isBeingDragged ? IM_COL32(40,40,20,200) : colSlotBg, 2.0f);
      dl->AddRect(sMin, sMax, hoverCell ? IM_COL32(100,100,140,200) : colSlotBr, 2.0f);

      if (g_inventory[slot].occupied && !isBeingDragged) {
        // Item abbreviation
        const char* name = GetItemNameByDef(g_inventory[slot].defIndex);
        // Truncate to ~4 chars
        char abbr[8];
        snprintf(abbr, sizeof(abbr), "%.4s", name);
        if (g_inventory[slot].itemLevel > 0) {
          snprintf(buf, sizeof(buf), "%s+%d", abbr, g_inventory[slot].itemLevel);
        } else {
          snprintf(buf, sizeof(buf), "%s", abbr);
        }
        ImVec2 sz = ImGui::CalcTextSize(buf);
        float tx = (sMin.x + sMax.x) * 0.5f - sz.x * 0.5f;
        float ty = (sMin.y + sMax.y) * 0.5f - sz.y * 0.5f;
        dl->AddText(ImVec2(tx, ty), colValue, buf);
      }
    }
  }

  // Draw dragged item at cursor
  if (g_isDragging) {
    const char* name = GetItemNameByDef(g_dragDefIndex);
    if (g_dragItemLevel > 0)
      snprintf(buf, sizeof(buf), "%s +%d", name, g_dragItemLevel);
    else
      snprintf(buf, sizeof(buf), "%s", name);
    ImVec2 sz = ImGui::CalcTextSize(buf);
    dl->AddRectFilled(ImVec2(mp.x - 2, mp.y - 2), ImVec2(mp.x + sz.x + 4, mp.y + sz.y + 4),
                      IM_COL32(30, 30, 50, 220), 3.0f);
    dl->AddText(ImVec2(mp.x + 1, mp.y + 1), IM_COL32(0,0,0,180), buf);
    dl->AddText(ImVec2(mp.x, mp.y), colEquip, buf);
  }

  // Separator
  float zenLineY = py + 500;
  dl->AddLine(ImVec2(c.ToScreenX(px + 10), c.ToScreenY(zenLineY)),
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(zenLineY)), colBorder);

  // Zen display
  snprintf(buf, sizeof(buf), "%u Zen", g_zen);
  DrawPanelTextCentered(dl, c, px, zenLineY + 8, pw, buf, colGold);

  // Tooltip on hover (bag items)
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int slot = row * 8 + col;
      if (!g_inventory[slot].occupied) continue;
      float cx = gridX + col * (cellW + gap);
      float cy = gridY + row * (cellH + gap);
      ImVec2 sMin(c.ToScreenX(cx), c.ToScreenY(cy));
      ImVec2 sMax(c.ToScreenX(cx + cellW), c.ToScreenY(cy + cellH));
      if (mp.x >= sMin.x && mp.x < sMax.x && mp.y >= sMin.y && mp.y < sMax.y && !g_isDragging) {
        const char* name = GetItemNameByDef(g_inventory[slot].defIndex);
        if (g_inventory[slot].itemLevel > 0)
          snprintf(buf, sizeof(buf), "%s +%d (x%d)", name, g_inventory[slot].itemLevel, g_inventory[slot].quantity);
        else
          snprintf(buf, sizeof(buf), "%s (x%d)", name, g_inventory[slot].quantity);
        ImVec2 tsz = ImGui::CalcTextSize(buf);
        float ttx = mp.x + 10, tty = mp.y - tsz.y - 6;
        dl->AddRectFilled(ImVec2(ttx - 4, tty - 2), ImVec2(ttx + tsz.x + 4, tty + tsz.y + 4),
                          IM_COL32(20, 20, 35, 240), 3.0f);
        dl->AddRect(ImVec2(ttx - 4, tty - 2), ImVec2(ttx + tsz.x + 4, tty + tsz.y + 4),
                    colBorder, 3.0f);
        dl->AddText(ImVec2(ttx, tty), colValue, buf);
      }
    }
  }
}

// Handle panel click interactions (returns true if click was consumed)
static bool HandlePanelClick(float vx, float vy) {
  // Character Info panel
  if (g_showCharInfo && IsPointInPanel(vx, vy, GetCharInfoPanelX())) {
    float px = GetCharInfoPanelX(), py = PANEL_Y;

    // Close button
    if (vx >= px + PANEL_W - 28 && vx < px + PANEL_W - 6 && vy >= py + 6 && vy < py + 24) {
      g_showCharInfo = false;
      return true;
    }

    // Stat "+" buttons
    if (g_serverLevelUpPoints > 0) {
      for (int i = 0; i < 4; i++) {
        float btnX = px + PANEL_W - 42, btnY = py + 150 + i * 32;
        if (vx >= btnX && vx < btnX + 26 && vy >= btnY && vy < btnY + 20) {
          // Send stat allocation request
          PMSG_STAT_ALLOC_RECV pkt{};
          pkt.h = MakeC1Header(sizeof(pkt), 0x37);
          pkt.statType = static_cast<uint8_t>(i);
          g_net.Send(&pkt, sizeof(pkt));
          return true;
        }
      }
    }
    return true; // Consumed by panel area
  }

  // Inventory panel
  if (g_showInventory && IsPointInPanel(vx, vy, GetInventoryPanelX())) {
    float px = GetInventoryPanelX(), py = PANEL_Y;

    // Close button
    if (vx >= px + PANEL_W - 28 && vx < px + PANEL_W - 6 && vy >= py + 6 && vy < py + 24) {
      g_showInventory = false;
      return true;
    }

    // Bag grid: start drag
    float gridX = px + 15, gridY = py + 234;
    float cellW = 32.0f, cellH = 32.0f, gap = 1.0f;
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        int slot = row * 8 + col;
        float cx = gridX + col * (cellW + gap);
        float cy = gridY + row * (cellH + gap);
        if (vx >= cx && vx < cx + cellW && vy >= cy && vy < cy + cellH) {
          if (g_inventory[slot].occupied) {
            g_dragFromSlot = slot;
            g_dragDefIndex = g_inventory[slot].defIndex;
            g_dragQuantity = g_inventory[slot].quantity;
            g_dragItemLevel = g_inventory[slot].itemLevel;
            g_isDragging = true;
          }
          return true;
        }
      }
    }

    return true; // Consumed by panel area
  }
  return false;
}

// Handle drag release (mouse up)
static void HandlePanelMouseUp(float vx, float vy) {
  if (!g_isDragging) return;
  g_isDragging = false;

  // Check if dropped on an equipment slot
  if (g_showInventory) {
    float px = GetInventoryPanelX(), py = PANEL_Y;
    struct EquipPos { int slot; float x; float y; float w; float h; };
    EquipPos equipLayout[] = {
      {2, px+118, py+56,  54, 42},
      {0, px+52,  py+102, 54, 54},
      {3, px+118, py+102, 54, 54},
      {1, px+184, py+102, 54, 54},
      {5, px+52,  py+162, 54, 42},
      {4, px+118, py+162, 54, 42},
      {6, px+184, py+162, 54, 42},
    };

    for (auto &ep : equipLayout) {
      if (vx >= ep.x && vx < ep.x + ep.w && vy >= ep.y && vy < ep.y + ep.h) {
        // Drop item into equipment slot
        // Check if item category matches the slot
        // For simplicity: slot 0/1 = weapons/shields (cat 0-6), slot 2 = helm, etc.
        // Just equip it and send to server
        g_equipSlots[ep.slot].category = 0; // TODO: resolve from defIndex
        g_equipSlots[ep.slot].itemIndex = 0;
        g_equipSlots[ep.slot].itemLevel = g_dragItemLevel;
        g_equipSlots[ep.slot].equipped = true;

        // Remove from bag
        if (g_dragFromSlot >= 0 && g_dragFromSlot < INVENTORY_SLOTS) {
          g_inventory[g_dragFromSlot] = {};
        }

        // Send equip packet to server
        PMSG_EQUIP_RECV eq{};
        eq.h = MakeC1Header(sizeof(eq), 0x27);
        eq.characterId = 1;
        eq.slot = static_cast<uint8_t>(ep.slot);
        eq.category = g_equipSlots[ep.slot].category;
        eq.itemIndex = g_equipSlots[ep.slot].itemIndex;
        eq.itemLevel = g_dragItemLevel;
        g_net.Send(&eq, sizeof(eq));

        printf("[UI] Equipped item from bag slot %d to equip slot %d\n", g_dragFromSlot, ep.slot);
        break;
      }
    }
  }

  g_dragFromSlot = -1;
}

int main(int argc, char **argv) {
  // Open client.log — tee all cout/cerr to both console and file
  std::ofstream logFile("client.log", std::ios::trunc);
  TeeStreambuf *coutTee = nullptr, *cerrTee = nullptr;
  std::streambuf *origCout = nullptr, *origCerr = nullptr;
  if (logFile.is_open()) {
    // Log header with timestamp
    std::time_t now = std::time(nullptr);
    logFile << "=== MuRemaster client.log === " << std::ctime(&now)
            << std::endl;
    logFile.flush();

    origCout = std::cout.rdbuf();
    origCerr = std::cerr.rdbuf();
    coutTee = new TeeStreambuf(origCout, logFile.rdbuf());
    cerrTee = new TeeStreambuf(origCerr, logFile.rdbuf());
    std::cout.rdbuf(coutTee);
    std::cerr.rdbuf(cerrTee);
  }

  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    return -1;
  }

  // GL 3.3 + GLSL 150
  const char *glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_STENCIL_BITS, 8);

  GLFWwindow *window = glfwCreateWindow(
      1920, 1080, "Mu Online Remaster (Native macOS C++)", nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // Enable vsync

#ifdef __APPLE__
  activateMacOSApp();
#endif

  if (glewInit() != GLEW_OK) {
    std::cerr << "Failed to initialize GLEW" << std::endl;
    return -1;
  }

  // Enable OpenGL debug output if available (ARB_debug_output)
  if (GLEW_ARB_debug_output) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallbackARB(glDebugCallback, nullptr);
    std::cout << "[GL] Debug output enabled" << std::endl;
  } else {
    std::cout << "[GL] Debug output not available — using manual checks"
              << std::endl;
  }
  std::cout << "[GL] Renderer: " << glGetString(GL_RENDERER) << std::endl;
  std::cout << "[GL] Version: " << glGetString(GL_VERSION) << std::endl;

  g_terrain.Init(); // Initialize OpenGL resources for terrain
  checkGLError("terrain init");

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window, false);
  glfwSetCursorPosCallback(window, mouse_callback);
  glfwSetScrollCallback(window, scroll_callback);
  glfwSetMouseButtonCallback(window, mouse_button_callback);
  glfwSetKeyCallback(window, key_callback);
  glfwSetCharCallback(window, char_callback);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Load larger font for HUD text overlays (Retina-aware)
  float contentScale = 1.0f;
  {
    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    contentScale = xscale; // 2.0 on Retina, 1.0 on non-Retina
  }
  ImFont* hudFont = nullptr;
  {
    ImFontConfig cfg;
    cfg.SizePixels = 18.0f * contentScale; // Render at native framebuffer resolution
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    hudFont = io.Fonts->AddFontDefault(&cfg);
    io.Fonts->Build();
    io.FontGlobalScale = 1.0f / contentScale; // Keep screen-space coordinates
  }

  // Initialize modern HUD (centered at 70% scale)
  g_hudCoords.window = window;
  g_hudCoords.SetCenteredScale(0.7f);

  std::string hudAssetPath = "../lab-studio/modern-ui/assets";
  HUD g_hud;
  g_hud.Init(hudAssetPath, window);
  g_hud.hudFont = hudFont;

  // Wire HUD button callbacks to panel toggles
  g_hud.onToggleCharInfo = []() { g_showCharInfo = !g_showCharInfo; };
  g_hud.onToggleInventory = []() { g_showInventory = !g_showInventory; };

  MockData hudData = MockData::CreateDK50();

  // Load Terrain for testing
  std::string data_path = "/Users/karlisfeldmanis/Desktop/mu_remaster/"
                          "references/other/MuMain/src/bin/Data";
  TerrainData terrainData = TerrainParser::LoadWorld(1, data_path);

  // Reconstruct TW_NOGROUND for bridge cells.
  // The .att file for this data version lacks these flags (verified: 0 cells).
  // Original engine reads them from .att (ZzzLodTerrain.cpp:1665).
  // We reconstruct from bridge objects (type 80) with orientation awareness.
  {
    const int S = TerrainParser::TERRAIN_SIZE;
    int count = 0;
    for (const auto &obj : terrainData.objects) {
      if (obj.type != 80)
        continue;
      int gz = (int)(obj.position.x / 100.0f);
      int gx = (int)(obj.position.z / 100.0f);
      float angZ =
          std::abs(std::fmod(glm::degrees(obj.rotation.z) + 360.0f, 180.0f));
      bool spanAlongGZ = (std::abs(angZ - 90.0f) < 45.0f);
      // +1 buffer for bilinear neighbor coverage in shader
      int rGZ = spanAlongGZ ? 4 : 2;
      int rGX = spanAlongGZ ? 2 : 4;
      for (int dz = -rGZ; dz <= rGZ; ++dz) {
        for (int dx = -rGX; dx <= rGX; ++dx) {
          int cz = gz + dz, cx = gx + dx;
          if (cz >= 0 && cz < S && cx >= 0 && cx < S) {
            terrainData.mapping.attributes[cz * S + cx] |= 0x08;
            count++;
          }
        }
      }
    }
    // Expand TW_NOGROUND to adjacent water cells so bilinear sampling in the
    // shader never mixes unmarked water into bridge road tiles.
    std::vector<uint8_t> expanded = terrainData.mapping.attributes;
    for (int z = 0; z < S; ++z) {
      for (int x = 0; x < S; ++x) {
        if (!(terrainData.mapping.attributes[z * S + x] & 0x08))
          continue;
        for (int dz = -1; dz <= 1; ++dz) {
          for (int dx = -1; dx <= 1; ++dx) {
            int nz = z + dz, nx = x + dx;
            if (nz >= 0 && nz < S && nx >= 0 && nx < S) {
              if (terrainData.mapping.layer1[nz * S + nx] == 5)
                expanded[nz * S + nx] |= 0x08;
            }
          }
        }
      }
    }
    terrainData.mapping.attributes = expanded;

    int finalCount = 0;
    for (int idx = 0; idx < S * S; ++idx)
      if (terrainData.mapping.attributes[idx] & 0x08)
        finalCount++;
    std::cout << "[Terrain] Marked " << finalCount
              << " bridge cells as TW_NOGROUND (" << count
              << " from objects + expansion)" << std::endl;
  }

  // Make terrain data accessible for movement/height
  g_terrainDataPtr = &terrainData;

  g_terrain.Load(terrainData, 1, data_path);
  std::cout << "Loaded Map 1 (Lorencia): " << terrainData.heightmap.size()
            << " height samples, " << terrainData.objects.size() << " objects"
            << std::endl;

  // Load world objects
  g_objectRenderer.Init();
  g_objectRenderer.SetTerrainLightmap(terrainData.lightmap);
  g_objectRenderer.SetTerrainMapping(&terrainData.mapping);
  g_objectRenderer.SetTerrainHeightmap(terrainData.heightmap);
  std::string object1_path = data_path + "/Object1";
  g_objectRenderer.LoadObjects(terrainData.objects, object1_path);
  checkGLError("object renderer load");
  std::cout << "[ObjectRenderer] Loaded " << terrainData.objects.size()
            << " object instances, " << g_objectRenderer.GetModelCount()
            << " unique models" << std::endl;
  g_grass.Init();
  g_grass.Load(terrainData, 1, data_path);
  checkGLError("grass load");

  // Initialize sky
  g_sky.Init(data_path + "/");
  checkGLError("sky init");

  // Initialize fire effects and register emitters from fire-type objects
  g_fireEffect.Init(data_path + "/Effect");
  checkGLError("fire init");
  for (auto &inst : g_objectRenderer.GetInstances()) {
    auto &offsets = GetFireOffsets(inst.type);
    for (auto &off : offsets) {
      // Extract rotation without scale (original CreateFire only rotates, no
      // scale)
      glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
      glm::mat3 rot;
      for (int c = 0; c < 3; c++)
        rot[c] = glm::normalize(glm::vec3(inst.modelMatrix[c]));
      g_fireEffect.AddEmitter(worldPos + rot * off);
    }
  }
  std::cout << "[FireEffect] Registered " << g_fireEffect.GetEmitterCount()
            << " fire emitters" << std::endl;
  // Print fire-type objects for debugging/testing
  for (int i = 0; i < (int)terrainData.objects.size(); ++i) {
    int t = terrainData.objects[i].type;
    if (t == 50 || t == 51 || t == 52 || t == 55 || t == 80 || t == 130)
      std::cout << "  fire obj idx=" << i << " type=" << t << std::endl;
  }

  // Collect point lights from light-emitting objects
  g_pointLights.clear();
  for (auto &inst : g_objectRenderer.GetInstances()) {
    const LightTemplate *props = GetLightProperties(inst.type);
    if (!props)
      continue;
    // Extract world position from model matrix translation column
    glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
    PointLight light;
    light.position = worldPos + glm::vec3(0.0f, props->heightOffset, 0.0f);
    light.color = props->color;
    light.range = props->range;
    g_pointLights.push_back(light);
  }
  // Cap at shader maximum
  if ((int)g_pointLights.size() > MAX_POINT_LIGHTS)
    g_pointLights.resize(MAX_POINT_LIGHTS);
  std::cout << "[Lights] Collected " << g_pointLights.size()
            << " point lights from world objects" << std::endl;

  // Initialize hero character and click effect
  g_hero.Init(data_path);
  g_hero.SetTerrainData(&terrainData);
  g_hero.SetTerrainLightmap(terrainData.lightmap);
  g_hero.SetPointLights(g_pointLights);
  g_hero.SnapToTerrain();

  g_clickEffect.Init();
  g_clickEffect.LoadAssets(data_path);
  g_clickEffect.SetTerrainData(&terrainData);
  checkGLError("hero init");

  // Connect to server via persistent NetworkClient
  g_npcManager.SetTerrainData(&terrainData);
  ServerData serverData;

  // Set up packet handler BEFORE connecting so no packets are lost
  g_net.onPacket = [&serverData](const uint8_t *pkt, int size) {
    parseInitialPacket(pkt, size, serverData);
  };

  if (g_net.Connect("127.0.0.1", 44405)) {
    serverData.connected = true;

    // Receive initial data burst (welcome + NPCs + monsters + equipment + stats)
    // Give server time to send all initial packets, poll to parse them
    for (int attempt = 0; attempt < 100; attempt++) {
      g_net.Poll();
      usleep(10000); // 10ms
    }
  }

  // Switch to ongoing packet handler for game loop
  g_net.onPacket = [](const uint8_t *pkt, int size) {
    handleServerPacket(pkt, size);
  };

  if (serverData.connected && !serverData.npcs.empty()) {
    g_npcManager.InitModels(data_path);
    for (auto &npc : serverData.npcs) {
      g_npcManager.AddNpcByType(npc.type, npc.gridX, npc.gridY, npc.dir);
    }
    std::cout << "[NPC] Loaded " << serverData.npcs.size()
              << " NPCs from server" << std::endl;
  } else {
    std::cout << "[NPC] No server connection, using hardcoded NPCs"
              << std::endl;
    g_npcManager.Init(data_path);
  }

  // Equip weapon from server equipment data (DB-driven)
  if (!serverData.equipment.empty()) {
    g_hero.EquipWeapon(serverData.equipment[0]);
  }
  g_npcManager.SetTerrainLightmap(terrainData.lightmap);
  g_npcManager.SetPointLights(g_pointLights);
  checkGLError("npc init");

  // Initialize monster manager and spawn monsters from server data
  g_monsterManager.InitModels(data_path);
  g_monsterManager.SetTerrainData(&terrainData);
  g_monsterManager.SetTerrainLightmap(terrainData.lightmap);
  g_monsterManager.SetPointLights(g_pointLights);
  if (!serverData.monsters.empty()) {
    for (auto &mon : serverData.monsters) {
      g_monsterManager.AddMonster(mon.monsterType, mon.gridX, mon.gridY,
                                  mon.dir, mon.serverIndex);
    }
    std::cout << "[Monster] Spawned " << serverData.monsters.size()
              << " monsters from server" << std::endl;
  }
  checkGLError("monster init");

  // Load saved camera state (persists position/angle/zoom across restarts)
  g_camera.LoadState("camera_save.txt");

  // Sync hero position from loaded camera state
  g_hero.SetPosition(g_camera.GetPosition());
  g_hero.SnapToTerrain();

  // Fix: if hero spawned on a non-walkable tile, move to a known safe position
  {
    glm::vec3 heroPos = g_hero.GetPosition();
    const int S = TerrainParser::TERRAIN_SIZE;
    int gz = (int)(heroPos.x / 100.0f);
    int gx = (int)(heroPos.z / 100.0f);
    bool walkable = (gx >= 0 && gz >= 0 && gx < S && gz < S) &&
                    (terrainData.mapping.attributes[gz * S + gx] & 0x04) == 0;
    if (!walkable) {
      std::cout << "[Hero] Spawn position non-walkable (attr=0x"
                << std::hex << (int)terrainData.mapping.attributes[gz * S + gx]
                << std::dec << "), searching for walkable tile..." << std::endl;
      // Spiral search from Lorencia town center for nearest walkable tile
      int startGX = 125, startGZ = 135;
      bool found = false;
      for (int radius = 0; radius < 30 && !found; radius++) {
        for (int dy = -radius; dy <= radius && !found; dy++) {
          for (int dx = -radius; dx <= radius && !found; dx++) {
            if (radius > 0 && std::abs(dx) != radius && std::abs(dy) != radius) continue;
            int cx = startGX + dx, cz = startGZ + dy;
            if (cx < 1 || cz < 1 || cx >= S-1 || cz >= S-1) continue;
            uint8_t attr = terrainData.mapping.attributes[cz * S + cx];
            if ((attr & 0x04) == 0 && (attr & 0x08) == 0) {
              float wx = (float)cz * 100.0f;
              float wz = (float)cx * 100.0f;
              std::cout << "[Hero] Found walkable tile at grid (" << cx << "," << cz
                        << ") attr=0x" << std::hex << (int)attr << std::dec << std::endl;
              g_hero.SetPosition(glm::vec3(wx, 0.0f, wz));
              g_hero.SnapToTerrain();
              found = true;
            }
          }
        }
      }
      if (!found) {
        std::cout << "[Hero] WARNING: No walkable tile found nearby" << std::endl;
        g_hero.SetPosition(glm::vec3(13000.0f, 0.0f, 13000.0f));
        g_hero.SnapToTerrain();
      }
    }
  }
  g_camera.SetPosition(g_hero.GetPosition());

  // Auto-diagnostic mode: --diag flag captures all debug views and exits
  bool autoDiag = false;
  bool autoScreenshot = false;
  bool autoGif = false;
  int gifFrameCount = 72; // ~3 seconds at 24fps
  int gifDelay = 4;       // centiseconds between frames (4cs = 25fps)
  int objectDebugIdx = -1;
  std::string objectDebugName;
  bool hasCustomPos = false;
  float customX = 0, customY = 0, customZ = 0;
  std::string outputName;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--diag")
      autoDiag = true;
    if (std::string(argv[i]) == "--screenshot")
      autoScreenshot = true;
    if (std::string(argv[i]) == "--debug" && i + 1 < argc) {
      g_terrain.SetDebugMode(std::atoi(argv[i + 1]));
      ++i;
    }
    if (std::string(argv[i]) == "--gif")
      autoGif = true;
    if (std::string(argv[i]) == "--gif-frames" && i + 1 < argc) {
      gifFrameCount = std::atoi(argv[i + 1]);
      ++i;
    }
    if (std::string(argv[i]) == "--pos" && i + 3 < argc) {
      customX = std::atof(argv[i + 1]);
      customY = std::atof(argv[i + 2]);
      customZ = std::atof(argv[i + 3]);
      hasCustomPos = true;
      i += 3;
    }
    if (std::string(argv[i]) == "--output" && i + 1 < argc) {
      outputName = argv[i + 1];
      ++i;
    }
    if (std::string(argv[i]) == "--object-debug" && i + 1 < argc) {
      objectDebugIdx = std::atoi(argv[i + 1]);
      ++i;
    }
  }
  if (autoDiag) {
    // Top-down zoomed view over Lorencia tavern area for clear tile diagnostics
    g_camera.SetPosition(glm::vec3(12800.0f, 3000.0f, 12800.0f));
    g_camera.SetAngles(0.0f, -89.0f); // Look straight down
    g_camera.SetZoom(3000.0f);
  }
  if ((autoScreenshot || autoGif) && !hasCustomPos) {
    // Lorencia town center at original MU isometric angle (default for
    // captures)
    g_camera.SetPosition(glm::vec3(13000.0f, 350.0f, 13500.0f));
  }
  // --pos X Y Z: override camera position with exact coordinates
  if (hasCustomPos) {
    g_hero.SetPosition(glm::vec3(customX, customY, customZ));
    g_hero.SnapToTerrain();
    g_camera.SetPosition(g_hero.GetPosition());
    std::cout << "[camera] Position set to (" << customX << ", " << customY
              << ", " << customZ << ")" << std::endl;
  }
  // --object-debug <index>: position camera to look at a specific object
  if (objectDebugIdx >= 0 && objectDebugIdx < (int)terrainData.objects.size()) {
    auto &debugObj = terrainData.objects[objectDebugIdx];
    // Position camera offset from the object, looking at it
    glm::vec3 objPos = debugObj.position;
    // Check for --topdown flag for bird's eye view
    bool topDown = false;
    for (int ii = 1; ii < argc; ++ii) {
      if (std::string(argv[ii]) == "--topdown")
        topDown = true;
    }
    if (topDown) {
      g_camera.SetPosition(glm::vec3(objPos.x, objPos.y + 1500.0f, objPos.z));
      g_camera.SetAngles(0.0f, -89.0f);
      g_camera.SetZoom(1500.0f);
    } else {
      // Position camera at the object using the fixed isometric angle
      g_hero.SetPosition(objPos);
      g_hero.SnapToTerrain();
      g_camera.SetPosition(g_hero.GetPosition());
    }
    objectDebugName = "obj_type" + std::to_string(debugObj.type) + "_idx" +
                      std::to_string(objectDebugIdx);
    if (autoGif)
      ; // keep autoGif, skip autoScreenshot
    else
      autoScreenshot = true;
    std::cout << "[object-debug] Targeting object " << objectDebugIdx
              << " type=" << debugObj.type << " at gl_pos=(" << objPos.x << ", "
              << objPos.y << ", " << objPos.z << ")" << std::endl;
  }
  int diagFrame = 0;
  const char *diagNames[] = {"normal", "tileindex", "tileuv",
                             "alpha",  "lightmap",  "nolightmap"};

  // Pass point lights to renderers
  {
    std::vector<glm::vec3> lightPos, lightCol;
    std::vector<float> lightRange;
    for (auto &pl : g_pointLights) {
      lightPos.push_back(pl.position);
      lightCol.push_back(pl.color);
      lightRange.push_back(pl.range);
    }
    g_objectRenderer.SetPointLights(lightPos, lightCol, lightRange);
    g_terrain.SetPointLights(lightPos, lightCol, lightRange);
  }

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  ImVec4 clear_color = ImVec4(
      0.0f, 0.0f, 0.0f, 1.00f); // Black: matches edge fog at map boundaries
  float lastFrame = 0.0f;
  while (!glfwWindowShouldClose(window)) {
    float currentFrame = glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    glfwPollEvents();
    processInput(window, deltaTime);
    g_camera.Update(deltaTime);

    // Poll persistent network connection for server packets
    g_net.Poll();
    g_net.Flush();

    // Send player position to server periodically (~4Hz)
    {
      static float posTimer = 0.0f;
      posTimer += deltaTime;
      if (posTimer >= 0.25f) {
        posTimer = 0.0f;
        glm::vec3 hp = g_hero.GetPosition();
        PMSG_PRECISE_POS_RECV posPkt{};
        posPkt.h = MakeC1Header(sizeof(posPkt), 0xD7);
        posPkt.worldX = hp.x;
        posPkt.worldZ = hp.z;
        g_net.Send(&posPkt, sizeof(posPkt));
      }
    }

    // Update monster manager (state machines, animation)
    g_monsterManager.SetPlayerPosition(g_hero.GetPosition());
    g_monsterManager.SetPlayerDead(g_hero.IsDead());
    g_monsterManager.Update(deltaTime);

    // Hero combat: update attack state machine, send attack packet on hit
    g_hero.UpdateAttack(deltaTime);
    g_hero.UpdateState(deltaTime);
    if (g_hero.CheckAttackHit()) {
      int targetIdx = g_hero.GetAttackTarget();
      if (targetIdx >= 0 && targetIdx < g_monsterManager.GetMonsterCount()) {
        uint16_t serverIdx = g_monsterManager.GetServerIndex(targetIdx);
        PMSG_ATTACK_RECV atkPkt{};
        atkPkt.h = MakeC1Header(sizeof(atkPkt), 0x28);
        atkPkt.monsterIndex = serverIdx;
        g_net.Send(&atkPkt, sizeof(atkPkt));
      }
    }
    // Auto-attack: re-engage after cooldown if target still alive
    if (g_hero.GetAttackState() == AttackState::NONE &&
        g_hero.GetAttackTarget() >= 0) {
      int targetIdx = g_hero.GetAttackTarget();
      if (targetIdx < g_monsterManager.GetMonsterCount()) {
        MonsterInfo mi = g_monsterManager.GetMonsterInfo(targetIdx);
        if (mi.state != MonsterState::DYING && mi.state != MonsterState::DEAD &&
            mi.hp > 0) {
          g_hero.AttackMonster(targetIdx, mi.position);
        }
      }
    }

    // Hero respawn: after death timer expires, respawn in Lorencia safe zone
    if (g_hero.ReadyToRespawn()) {
      // Find walkable safe zone tile (same spiral search as init)
      const int S = TerrainParser::TERRAIN_SIZE;
      int startGX = 125, startGZ = 133;
      glm::vec3 spawnPos(13300.0f, 0.0f, 12300.0f);
      for (int radius = 0; radius < 30; radius++) {
        bool found = false;
        for (int dy = -radius; dy <= radius && !found; dy++) {
          for (int dx = -radius; dx <= radius && !found; dx++) {
            if (radius > 0 && std::abs(dx) != radius && std::abs(dy) != radius) continue;
            int cx = startGX + dx, cz = startGZ + dy;
            if (cx < 1 || cz < 1 || cx >= S-1 || cz >= S-1) continue;
            uint8_t attr = g_terrainDataPtr->mapping.attributes[cz * S + cx];
            if ((attr & 0x04) == 0 && (attr & 0x08) == 0) {
              spawnPos = glm::vec3((float)cz * 100.0f, 0.0f, (float)cx * 100.0f);
              found = true;
            }
          }
        }
        if (found) break;
      }
      g_hero.Respawn(spawnPos);
      g_hero.SnapToTerrain();
      g_camera.SetPosition(g_hero.GetPosition());
      g_serverHP = g_serverMaxHP; // Reset HUD HP

      // Notify server that player is alive (clears session.dead)
      {
        PMSG_CHARSAVE_RECV save{};
        save.h = MakeC1Header(sizeof(save), 0x26);
        save.characterId = 1;
        save.level = (uint16_t)g_serverLevel;
        save.strength = (uint16_t)g_serverStr;
        save.dexterity = (uint16_t)g_serverDex;
        save.vitality = (uint16_t)g_serverVit;
        save.energy = (uint16_t)g_serverEne;
        save.life = (uint16_t)g_serverMaxHP;
        save.maxLife = (uint16_t)g_serverMaxHP;
        save.levelUpPoints = (uint16_t)g_serverLevelUpPoints;
        save.experienceLo = (uint32_t)((uint64_t)g_serverXP & 0xFFFFFFFF);
        save.experienceHi = (uint32_t)((uint64_t)g_serverXP >> 32);
        g_net.Send(&save, sizeof(save));
      }
    }

    // Auto-pickup: walk near a ground item to pick it up
    {
      glm::vec3 heroPos = g_hero.GetPosition();
      for (auto &gi : g_groundItems) {
        if (!gi.active) continue;
        gi.timer += deltaTime;
        // Snap drop Y to terrain
        if (gi.position.y == 0.0f && g_terrainDataPtr) {
          float gx = gi.position.z / 100.0f;
          float gz = gi.position.x / 100.0f;
          int ix = (int)gx, iz = (int)gz;
          if (ix >= 0 && iz >= 0 && ix < 256 && iz < 256) {
            float h = g_terrainDataPtr->heightmap[iz * 256 + ix] * 1.5f;
            gi.position.y = h + 5.0f;
          }
        }
        float dist = glm::length(glm::vec3(heroPos.x - gi.position.x, 0, heroPos.z - gi.position.z));
        if (dist < 120.0f && !g_hero.IsDead()) {
          PMSG_PICKUP_RECV pkt{};
          pkt.h = MakeC1Header(sizeof(pkt), 0x2C);
          pkt.dropIndex = gi.dropIndex;
          g_net.Send(&pkt, sizeof(pkt));
          gi.active = false; // Optimistic remove
        }
        // Despawn after 60s
        if (gi.timer > 60.0f) gi.active = false;
      }
    }

    // Roof hiding: read layer1 tile at hero position, fade types 125/126
    if (g_terrainDataPtr) {
      glm::vec3 heroPos = g_hero.GetPosition();
      const int S = TerrainParser::TERRAIN_SIZE;
      int gz = (int)(heroPos.x / 100.0f);
      int gx = (int)(heroPos.z / 100.0f);
      uint8_t heroTile = 0;
      if (gx >= 0 && gz >= 0 && gx < S && gz < S)
        heroTile = g_terrainDataPtr->mapping.layer1[gz * S + gx];
      // Original: HeroTile == 4 hides roof meshes
      float target = (heroTile == 4) ? 0.0f : 1.0f;
      g_typeAlphaTarget[125] = target;
      g_typeAlphaTarget[126] = target;
      // Fast fade — nearly instant (95%+ in 1-2 frames)
      float blend = 1.0f - std::exp(-20.0f * deltaTime);
      for (auto &[type, alpha] : g_typeAlpha) {
        alpha += (g_typeAlphaTarget[type] - alpha) * blend;
      }
      g_objectRenderer.SetTypeAlpha(g_typeAlpha);

      // SafeZone detection: attribute 0x01 = TW_SAFEZONE
      uint8_t heroAttr = 0;
      if (gx >= 0 && gz >= 0 && gx < S && gz < S)
        heroAttr = g_terrainDataPtr->mapping.attributes[gz * S + gx];
      g_hero.SetInSafeZone((heroAttr & 0x01) != 0);
    }

    // Auto-diagnostic: set debug mode BEFORE render
    if (autoDiag && diagFrame >= 2) {
      int mode = (diagFrame - 2) / 2;
      if (mode < 6 && (diagFrame - 2) % 2 == 0) {
        g_terrain.SetDebugMode(mode);
      }
    }

    // Use framebuffer size for viewport (Retina displays are 2x window size)
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);

    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);
    glm::mat4 projection = g_camera.GetProjectionMatrix((float)winW, (float)winH);
    glm::mat4 view = g_camera.GetViewMatrix();
    glm::vec3 camPos = g_camera.GetPosition();

    // Sky renders first (behind everything, no depth write)
    g_sky.Render(view, projection, camPos);

    g_terrain.Render(view, projection, currentFrame, camPos);

    // Render world objects first (before grass, so tall grass billboards
    // don't block thin fence bar meshes via depth buffer)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_objectRenderer.Render(view, projection, g_camera.GetPosition(),
                            currentFrame);

    // Render grass billboards (after objects so grass doesn't occlude fences)
    {
      std::vector<GrassRenderer::PushSource> pushSources;
      pushSources.push_back({g_hero.GetPosition(), 100.0f});
      g_grass.Render(view, projection, currentFrame, camPos, pushSources);
    }

    // Update and render fire effects
    g_fireEffect.Update(deltaTime);
    g_fireEffect.Render(view, projection);

    // Render NPC characters with shadows
    g_npcManager.RenderShadows(view, projection);
    g_npcManager.Render(view, projection, camPos, deltaTime);

    // Render NPC selection outline (green glow on hover)
    if (g_hoveredNpc >= 0)
      g_npcManager.RenderOutline(g_hoveredNpc, view, projection);

    // Render monsters with shadows
    g_monsterManager.RenderShadows(view, projection);
    g_monsterManager.Render(view, projection, camPos, deltaTime);

    // Render hero character, shadow, and click effect (after all world
    // geometry)
    g_clickEffect.Render(view, projection, deltaTime, g_hero.GetShader());
    g_hero.Render(view, projection, camPos, deltaTime);
    g_hero.RenderShadow(view, projection);

    // Auto-GIF: capture with warmup for fire particle buildup
    // Capture BEFORE ImGui rendering so debug overlay is not in the output
    if (autoGif && !Screenshot::IsRecording() && diagFrame == 0) {
      std::string gifPath =
          !outputName.empty()
              ? "screenshots/" + outputName + ".gif"
              : (objectDebugName.empty()
                     ? "screenshots/fire_effect.gif"
                     : "screenshots/" + objectDebugName + ".gif");
      Screenshot::StartRecording(window, gifPath, gifFrameCount, gifDelay);
      std::cout << "[GIF] Starting capture (" << gifFrameCount << " frames)"
                << std::endl;
    }
    if (Screenshot::TickRecording(window)) {
      break; // GIF saved, exit
    }

    // Auto-screenshot flag (capture happens after ImGui render to include HUD)
    bool captureScreenshot = (autoScreenshot && diagFrame == 60);

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Update and render modern HUD (via ImGui foreground draw list)
    {
      // Feed server stats to HUD
      hudData.hp = g_serverHP;
      hudData.maxHp = g_serverMaxHP;
      hudData.mp = g_serverMP;
      hudData.maxMp = g_serverMaxMP;
      hudData.level = g_serverLevel;
      hudData.strength = g_serverStr;
      hudData.agility = g_serverDex;
      hudData.vitality = g_serverVit;
      hudData.energy = g_serverEne;
      hudData.levelUpPoints = g_serverLevelUpPoints;
      hudData.xp = g_serverXP;
      g_hud.Update(hudData);

      // Recalculate centered scale each frame (handles window resize)
      g_hudCoords.SetCenteredScale(0.7f);

      ImDrawList* dl = ImGui::GetForegroundDrawList();
      g_hud.Render(dl, g_hudCoords);

      // ── Character Info and Inventory panels ──
      if (g_showCharInfo) RenderCharInfoPanel(dl, g_hudCoords);
      if (g_showInventory) RenderInventoryPanel(dl, g_hudCoords);

      // ── Floating damage numbers (world-space → screen projection) ──
      {
        glm::mat4 vp = projection * view;
        for (auto &d : g_floatingDmg) {
          if (!d.active) continue;
          d.timer += deltaTime;
          if (d.timer >= d.maxTime) { d.active = false; continue; }

          // Float upward
          glm::vec3 pos = d.worldPos + glm::vec3(0, d.timer * 60.0f, 0);

          // Project to screen
          glm::vec4 clip = vp * glm::vec4(pos, 1.0f);
          if (clip.w <= 0.0f) continue;
          float sx = ((clip.x / clip.w) * 0.5f + 0.5f) * winW;
          float sy = ((1.0f - (clip.y / clip.w)) * 0.5f) * winH;

          // Fade out in last 0.5s
          float alpha = d.timer > 1.0f ? 1.0f - (d.timer - 1.0f) / 0.5f : 1.0f;
          alpha = std::max(0.0f, std::min(1.0f, alpha));

          // Color by type
          ImU32 col;
          const char* text;
          char buf[16];
          if (d.type == 7) {
            col = IM_COL32(180, 180, 180, (int)(alpha * 255));
            text = "MISS";
          } else {
            snprintf(buf, sizeof(buf), "%d", d.damage);
            text = buf;
            if (d.type == 8) // incoming (red)
              col = IM_COL32(255, 60, 60, (int)(alpha * 255));
            else if (d.type == 2) // critical (blue)
              col = IM_COL32(80, 180, 255, (int)(alpha * 255));
            else if (d.type == 3) // excellent (green)
              col = IM_COL32(80, 255, 120, (int)(alpha * 255));
            else // normal (orange)
              col = IM_COL32(255, 200, 60, (int)(alpha * 255));
          }

          // Draw with shadow
          float scale = 1.5f - d.timer * 0.3f;
          float fontSize = 18.0f * scale;
          ImVec2 tpos(sx, sy);
          // Shadow
          dl->AddText(hudFont, fontSize, ImVec2(tpos.x + 1, tpos.y + 1),
                      IM_COL32(0, 0, 0, (int)(alpha * 200)), text);
          // Main text
          dl->AddText(hudFont, fontSize, tpos, col, text);
        }
      }

      // ── Monster nameplates (name + level + HP bar) ──
      {
        glm::mat4 vp = projection * view;
        for (int i = 0; i < g_monsterManager.GetMonsterCount(); ++i) {
          MonsterInfo mi = g_monsterManager.GetMonsterInfo(i);
          if (mi.state == MonsterState::DEAD) continue;

          // Project nameplate position (above monster head)
          glm::vec3 namePos = mi.position + glm::vec3(0, mi.height + 15.0f, 0);
          glm::vec4 clip = vp * glm::vec4(namePos, 1.0f);
          if (clip.w <= 0.0f) continue;
          float sx = ((clip.x / clip.w) * 0.5f + 0.5f) * winW;
          float sy = ((1.0f - (clip.y / clip.w)) * 0.5f) * winH;

          // Distance culling — don't show names for far-away monsters
          float dist = glm::length(mi.position - camPos);
          if (dist > 2000.0f) continue;

          // Fade based on distance
          float alpha = dist < 1000.0f ? 1.0f : 1.0f - (dist - 1000.0f) / 1000.0f;
          alpha = std::max(0.0f, std::min(1.0f, alpha));
          if (mi.state == MonsterState::DYING) alpha *= 0.5f;

          // Name + level text
          char nameText[64];
          snprintf(nameText, sizeof(nameText), "%s  Lv.%d", mi.name.c_str(), mi.level);
          ImVec2 textSize = hudFont->CalcTextSizeA(14.0f, FLT_MAX, 0, nameText);
          float tx = sx - textSize.x * 0.5f;
          float ty = sy - textSize.y;

          // Name color: white normally, red if attacking hero
          ImU32 nameCol = (mi.state == MonsterState::ATTACKING || mi.state == MonsterState::CHASING)
            ? IM_COL32(255, 100, 100, (int)(alpha * 255))
            : IM_COL32(255, 255, 255, (int)(alpha * 220));

          // Shadow + text
          dl->AddText(hudFont, 14.0f, ImVec2(tx + 1, ty + 1),
                      IM_COL32(0, 0, 0, (int)(alpha * 180)), nameText);
          dl->AddText(hudFont, 14.0f, ImVec2(tx, ty), nameCol, nameText);

          // HP bar below name
          float barW = 50.0f, barH = 4.0f;
          float barX = sx - barW * 0.5f;
          float barY = sy + 2.0f;
          float hpFrac = mi.maxHp > 0 ? (float)mi.hp / mi.maxHp : 0.0f;
          hpFrac = std::max(0.0f, std::min(1.0f, hpFrac));
          // Background
          dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
                            IM_COL32(0, 0, 0, (int)(alpha * 160)));
          // HP fill (green → yellow → red)
          ImU32 hpCol = hpFrac > 0.5f ? IM_COL32(60, 220, 60, (int)(alpha * 220))
                      : hpFrac > 0.25f ? IM_COL32(220, 220, 60, (int)(alpha * 220))
                      : IM_COL32(220, 60, 60, (int)(alpha * 220));
          if (hpFrac > 0.0f)
            dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW * hpFrac, barY + barH), hpCol);
        }
      }

      // ── Ground item labels (floating above drops) ──
      {
        glm::mat4 vp = projection * view;
        for (auto &gi : g_groundItems) {
          if (!gi.active) continue;
          // Bob animation
          float bob = sinf(gi.timer * 3.0f) * 5.0f;
          glm::vec3 labelPos = gi.position + glm::vec3(0, 30.0f + bob, 0);
          glm::vec4 clip = vp * glm::vec4(labelPos, 1.0f);
          if (clip.w <= 0.0f) continue;
          float sx = ((clip.x / clip.w) * 0.5f + 0.5f) * winW;
          float sy = ((1.0f - (clip.y / clip.w)) * 0.5f) * winH;

          float dist = glm::length(gi.position - camPos);
          if (dist > 1500.0f) continue;

          const char *name = GetDropName(gi.defIndex);
          char label[64];
          if (gi.defIndex == -1)
            snprintf(label, sizeof(label), "%d Zen", gi.quantity);
          else if (gi.itemLevel > 0)
            snprintf(label, sizeof(label), "%s +%d", name, gi.itemLevel);
          else
            snprintf(label, sizeof(label), "%s", name);

          ImVec2 ts = hudFont->CalcTextSizeA(13.0f, FLT_MAX, 0, label);
          float tx = sx - ts.x * 0.5f, ty = sy - ts.y * 0.5f;
          // Yellow for items, gold for Zen
          ImU32 col = gi.defIndex == -1
            ? IM_COL32(255, 215, 0, 220)
            : IM_COL32(180, 255, 180, 220);
          dl->AddText(hudFont, 13.0f, ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 160), label);
          dl->AddText(hudFont, 13.0f, ImVec2(tx, ty), col, label);
        }
      }
    }

    // Add some debug info
    ImGui::Begin("Terrain Debug");
    ImGui::Text("Camera Pos: %.1f, %.1f, %.1f", camPos.x, camPos.y, camPos.z);
    ImGui::Text("Zoom: %.1f", g_camera.GetZoom());
    ImGui::Text("Objects: %d instances, %d models",
                g_objectRenderer.GetInstanceCount(),
                g_objectRenderer.GetModelCount());
    ImGui::Text("Fire: %d emitters, %d particles",
                g_fireEffect.GetEmitterCount(),
                g_fireEffect.GetParticleCount());

    static int debugMode = 0;
    const char *debugModes[] = {"Normal",     "Tile Index", "Tile UV",
                                "Alpha",      "Lightmap",   "No Lightmap",
                                "Layer1 Only"};
    if (ImGui::Combo("Debug View", &debugMode, debugModes, 7)) {
      g_terrain.SetDebugMode(debugMode);
    }
    ImGui::End();

    // Hero coordinate overlay (top-left)
    {
      glm::vec3 hPos = g_hero.GetPosition();
      ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.5f);
      ImGui::Begin("##HeroCoords", nullptr,
                   ImGuiWindowFlags_NoDecoration |
                       ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoFocusOnAppearing |
                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
      float muX = hPos.z / 100.0f;
      float muY = hPos.x / 100.0f;
      ImGui::Text("World: %.0f, %.0f, %.0f", hPos.x, hPos.y, hPos.z);
      ImGui::Text("Grid:  %.1f, %.1f", muX, muY);
      ImGui::Text("Height: %.1f", hPos.y);
      ImGui::Text("State: %s", g_hero.IsMoving() ? "Walking" : "Idle");
      if (g_terrainDataPtr) {
        const int S = TerrainParser::TERRAIN_SIZE;
        int gz = (int)(hPos.x / 100.0f);
        int gx = (int)(hPos.z / 100.0f);
        if (gx >= 0 && gz >= 0 && gx < S && gz < S) {
          uint8_t attr = g_terrainDataPtr->mapping.attributes[gz * S + gx];
          ImGui::Text(
              "Attr: 0x%02X%s%s%s%s%s", attr, (attr & 0x01) ? " SAFE" : "",
              (attr & 0x04) ? " NOMOVE" : "", (attr & 0x08) ? " NOGROUND" : "",
              (attr & 0x10) ? " WATER" : "", (attr & 0x20) ? " ACTION" : "");
          uint8_t tile = g_terrainDataPtr->mapping.layer1[gz * S + gx];
          ImGui::Text("Tile: %d%s", tile, (tile == 4) ? " (ROOF HIDE)" : "");
          ImGui::Text("Roof: %.0f%%", g_typeAlpha[125] * 100.0f);
        }
      }
      ImGui::End();
    }

    // NPC name labels — original MU style (ZzzInterface.cpp RenderBoolean)
    // NPC text color: RGB(150, 255, 240) cyan, BG: RGBA(10, 30, 50, 150) dark
    // blue Hovered NPC: brighter green text, green-tinted BG
    {
      int winW, winH;
      glfwGetWindowSize(window, &winW, &winH);
      auto *drawList = ImGui::GetForegroundDrawList();
      const float padX = 4.0f, padY = 2.0f;

      for (int i = 0; i < g_npcManager.GetNpcCount(); ++i) {
        NpcInfo info = g_npcManager.GetNpcInfo(i);
        if (info.name.empty())
          continue;

        float dist = glm::distance(camPos, info.position);
        if (dist > 2000.0f)
          continue;

        glm::vec3 labelPos =
            info.position + glm::vec3(0, info.height + 30.0f, 0);
        glm::vec4 clip = projection * view * glm::vec4(labelPos, 1.0f);
        if (clip.w <= 0)
          continue;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        float sx = (ndc.x * 0.5f + 0.5f) * (float)winW;
        float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)winH;

        ImVec2 textSize = ImGui::CalcTextSize(info.name.c_str());
        float x0 = sx - textSize.x / 2 - padX;
        float y0 = sy - textSize.y / 2 - padY;
        float x1 = sx + textSize.x / 2 + padX;
        float y1 = sy + textSize.y / 2 + padY;

        bool hovered = (i == g_hoveredNpc);
        ImU32 bgCol =
            hovered ? IM_COL32(10, 50, 20, 180) : IM_COL32(10, 30, 50, 150);
        ImU32 textCol = hovered ? IM_COL32(100, 255, 100, 255)
                                : IM_COL32(150, 255, 240, 255);

        drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), bgCol);
        drawList->AddText(ImVec2(sx - textSize.x / 2, sy - textSize.y / 2),
                          textCol, info.name.c_str());
      }
    }

    // NPC click interaction dialog
    if (g_selectedNpc >= 0) {
      NpcInfo info = g_npcManager.GetNpcInfo(g_selectedNpc);
      int winW, winH;
      glfwGetWindowSize(window, &winW, &winH);
      ImGui::SetNextWindowPos(
          ImVec2((float)winW / 2 - 150, (float)winH / 2 - 100),
          ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(300, 200));
      ImGui::Begin("NPC Dialog", nullptr,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
      ImGui::TextWrapped("Hello adventurer! I am %s.", info.name.c_str());
      ImGui::Separator();
      if (ImGui::Button("Shop (Coming Soon)", ImVec2(-1, 0))) {
      }
      if (ImGui::Button("Close", ImVec2(-1, 0))) {
        g_selectedNpc = -1;
      }
      ImGui::End();

      // Close on Escape
      if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        g_selectedNpc = -1;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Auto-screenshot: capture AFTER ImGui render (includes HUD overlay)
    if (captureScreenshot) {
      std::string ssPath;
      if (!outputName.empty()) {
        ssPath = "screenshots/" + outputName + ".jpg";
      } else if (!objectDebugName.empty()) {
        ssPath = "screenshots/" + objectDebugName + ".jpg";
      } else {
        ssPath =
            "screenshots/verif_" + std::to_string(std::time(nullptr)) + ".jpg";
      }
      int sw, sh;
      glfwGetFramebufferSize(window, &sw, &sh);
      std::vector<unsigned char> px(sw * sh * 3);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, sw, sh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
      std::vector<unsigned char> flipped(sw * sh * 3);
      for (int y = 0; y < sh; ++y)
        memcpy(&flipped[y * sw * 3], &px[(sh - 1 - y) * sw * 3], sw * 3);
      tjhandle comp = tjInitCompress();
      unsigned char *jbuf = nullptr;
      unsigned long jsize = 0;
      tjCompress2(comp, flipped.data(), sw, 0, sh, TJPF_RGB, &jbuf, &jsize,
                  TJSAMP_444, 95, TJFLAG_FASTDCT);
      std::filesystem::create_directories("screenshots");
      FILE *f = fopen(ssPath.c_str(), "wb");
      if (f) {
        fwrite(jbuf, 1, jsize, f);
        fclose(f);
      }
      tjFree(jbuf);
      tjDestroy(comp);
      std::cout << "[screenshot] Saved " << ssPath << std::endl;
      break;
    }

    // Auto-diagnostic: capture AFTER render, BEFORE swap (back buffer has
    // current frame)
    if (autoDiag && diagFrame >= 2) {
      int mode = (diagFrame - 2) / 2;
      if (mode < 6 && (diagFrame - 2) % 2 == 1) {
        std::string diagPath =
            "screenshots/diag_" + std::string(diagNames[mode]) + ".jpg";
        int sw, sh;
        glfwGetFramebufferSize(window, &sw, &sh);
        std::vector<unsigned char> px(sw * sh * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, sw, sh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
        std::vector<unsigned char> flipped(sw * sh * 3);
        for (int y = 0; y < sh; ++y)
          memcpy(&flipped[y * sw * 3], &px[(sh - 1 - y) * sw * 3], sw * 3);
        tjhandle comp = tjInitCompress();
        unsigned char *jbuf = nullptr;
        unsigned long jsize = 0;
        tjCompress2(comp, flipped.data(), sw, 0, sh, TJPF_RGB, &jbuf, &jsize,
                    TJSAMP_444, 95, TJFLAG_FASTDCT);
        std::filesystem::create_directories("screenshots");
        FILE *f = fopen(diagPath.c_str(), "wb");
        if (f) {
          fwrite(jbuf, 1, jsize, f);
          fclose(f);
        }
        tjFree(jbuf);
        tjDestroy(comp);
        std::cout << "[diag] Saved " << diagPath << std::endl;
      } else if (mode >= 6) {
        break;
      }
    }
    if (autoDiag || autoScreenshot || autoGif)
      diagFrame++;

    // Per-frame GL error check (only first 10 frames to avoid log spam)
    {
      static int frameNum = 0;
      if (frameNum < 10)
        checkGLError(("frame " + std::to_string(frameNum)).c_str());
      frameNum++;
    }

    glfwSwapBuffers(window);
  }

  // Save hero position via camera state for next launch
  g_camera.SetPosition(g_hero.GetPosition());
  g_camera.SaveState("camera_save.txt");

  // Disconnect from server
  g_net.Disconnect();

  // Cleanup
  g_hud.Cleanup();
  g_monsterManager.Cleanup();
  g_npcManager.Cleanup();
  g_hero.Cleanup();
  g_clickEffect.Cleanup();
  g_sky.Cleanup();
  g_fireEffect.Cleanup();
  g_objectRenderer.Cleanup();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  // Restore original stream buffers before log file closes
  if (origCout)
    std::cout.rdbuf(origCout);
  if (origCerr)
    std::cerr.rdbuf(origCerr);
  delete coutTee;
  delete cerrTee;

  return 0;
}
