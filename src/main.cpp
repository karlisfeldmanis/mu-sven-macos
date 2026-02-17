#include "../server/include/PacketDefs.hpp"
#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "Camera.hpp"
#include "ClickEffect.hpp"
#include "FireEffect.hpp"
#include "GrassRenderer.hpp"
#include "HeroCharacter.hpp"
#include "MockData.hpp"
#include "MonsterManager.hpp"
#include "NetworkClient.hpp"
#include "NpcManager.hpp"
#include "ObjectRenderer.hpp"
#include "Screenshot.hpp"
#include "Shader.hpp"
#include "Sky.hpp"
#include "Terrain.hpp"
#include "TerrainParser.hpp"
#include "UICoords.hpp"
#include "VFXManager.hpp"
#include "ViewerCommon.hpp"
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
VFXManager g_vfxManager;

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
  glm::vec3 worldPos; // World position where damage occurred
  int damage;
  uint8_t type;  // 0=normal(orange), 2=excellent(green), 3=critical(blue),
                 // 7=miss, 8=incoming(red)
  float timer;   // Counts up from 0
  float maxTime; // When to remove (1.5s)
  bool active;
};
static constexpr int MAX_FLOATING_DAMAGE = 32;
static FloatingDamage g_floatingDmg[MAX_FLOATING_DAMAGE] = {};

// ── Ground item drops ──
struct GroundItem {
  uint16_t dropIndex;
  int16_t defIndex; // -1=Zen
  uint8_t quantity;
  uint8_t itemLevel;
  glm::vec3 position;
  float timer; // Time alive (for bob animation)
  bool active;
};
static constexpr int MAX_GROUND_ITEMS = 64;
static GroundItem g_groundItems[MAX_GROUND_ITEMS] = {};
static const std::string g_dataPath =
    "/Users/karlisfeldmanis/Desktop/mu_remaster/references/other/MuMain/src/"
    "bin/Data"; // Default, updated in main

// Helper to get model filename for drop index
// Drop item definitions (Name, Model)
struct DropDef {
  const char *name;
  const char *model;
  int dmgMin;  // Weapon damage min bonus
  int dmgMax;  // Weapon damage max bonus
  int defense; // Armor defense bonus
};

static const DropDef *GetDropInfo(int16_t defIndex) {
  static const DropDef zen = {"Zen", "Zen.bmd", 0, 0, 0};
  // MU 0.97d complete item database (Mapped to Cat/Idx)
  static const DropDef items[] = {
      // Category 0: Swords
      {"Kris", "Sword01.bmd", 6, 11, 0},                   // 0
      {"Short Sword", "Sword02.bmd", 3, 7, 0},             // 1
      {"Rapier", "Sword03.bmd", 9, 13, 0},                 // 2
      {"Katane", "Sword04.bmd", 12, 18, 0},                // 3
      {"Sword of Assassin", "Sword05.bmd", 15, 22, 0},     // 4
      {"Blade", "Sword06.bmd", 21, 31, 0},                 // 5
      {"Gladius", "Sword07.bmd", 18, 26, 0},               // 6
      {"Falchion", "Sword08.bmd", 24, 34, 0},              // 7
      {"Serpent Sword", "Sword09.bmd", 30, 42, 0},         // 8
      {"Salamander", "Sword10.bmd", 36, 51, 0},            // 9
      {"Light Sabre", "Sword11.bmd", 42, 57, 0},           // 10
      {"Legendary Sword", "Sword12.bmd", 48, 64, 0},       // 11
      {"Heliacal Sword", "Sword13.bmd", 56, 72, 0},        // 12
      {"Double Blade", "Sword14.bmd", 44, 61, 0},          // 13
      {"Lighting Sword", "Sword15.bmd", 52, 68, 0},        // 14
      {"Giant Sword", "Sword16.bmd", 64, 82, 0},           // 15
      {"Sword of Destruction", "Sword17.bmd", 84, 108, 0}, // 16
      {"Dark Breaker", "Sword18.bmd", 96, 124, 0},         // 17
      {"Thunder Blade", "Sword19.bmd", 102, 132, 0},       // 18
      {"Divine Sword", "Sword20.bmd", 110, 140, 0},        // 19

      // Category 6: Shields (Starting at index 100 for better mapping)
      [100] = {"Small Shield", "Shield01.bmd", 0, 0, 3},
      {"Horn Shield", "Shield02.bmd", 0, 0, 6},
      {"Kite Shield", "Shield03.bmd", 0, 0, 10},
      {"Elven Shield", "Shield04.bmd", 0, 0, 15},
      {"Buckler", "Shield05.bmd", 0, 0, 20},
      {"Dragon Slayer Shield", "Shield06.bmd", 0, 0, 26},
      {"Skull Shield", "Shield07.bmd", 0, 0, 33},
      {"Spiked Shield", "Shield08.bmd", 0, 0, 41},
      {"Tower Shield", "Shield09.bmd", 0, 0, 50},
      {"Plate Shield", "Shield10.bmd", 0, 0, 60},
      {"Big Round Shield", "Shield11.bmd", 0, 0, 72},
      {"Serpent Shield", "Shield12.bmd", 0, 0, 85},
      {"Bronze Shield", "Shield13.bmd", 0, 0, 100},
      {"Dragon Shield", "Shield14.bmd", 0, 0, 115},
      {"Legendary Shield", "Shield15.bmd", 0, 0, 132},

      // Category 8: Armor (Starting at index 175)
      [175] = {"Bronze Armor", "ArmorMale01.bmd", 0, 0, 15},
      {"Dragon Armor", "ArmorMale10.bmd", 0, 0, 65},
      {"Pad Armor", "ArmorClass01.bmd", 0, 0, 5},
      {"Legendary Armor", "ArmorClass02.bmd", 0, 0, 42},
      {"Bone Armor", "ArmorClass03.bmd", 0, 0, 24},
      {"Leather Armor", "ArmorMale06.bmd", 0, 0, 8},

      // Category 7: Helms (Starting at index 150)
      [150] = {"Bronze Helm", "HelmMale01.bmd", 0, 0, 8},
      {"Dragon Helm", "HelmMale10.bmd", 0, 0, 48},
      {"Pad Helm", "HelmClass01.bmd", 0, 0, 2},
      {"Legendary Helm", "HelmClass02.bmd", 0, 0, 28},
      {"Bone Helm", "HelmClass03.bmd", 0, 0, 14},
      {"Leather Helm", "HelmMale06.bmd", 0, 0, 4},

      // Category 9: Pants (Starting at index 200)
      [200] = {"Bronze Pants", "PantMale01.bmd", 0, 0, 12},
      {"Dragon Pants", "PantMale10.bmd", 0, 0, 55},
      {"Pad Pants", "PantClass01.bmd", 0, 0, 4},
      {"Legendary Pants", "PantClass02.bmd", 0, 0, 35},
      {"Bone Pants", "PantClass03.bmd", 0, 0, 19},
      {"Leather Pants", "PantMale06.bmd", 0, 0, 6},

      // Category 10: Gloves (Starting at index 225)
      [225] = {"Bronze Gloves", "GloveMale01.bmd", 0, 0, 6},
      {"Dragon Gloves", "GloveMale10.bmd", 0, 0, 40},
      {"Pad Gloves", "GloveClass01.bmd", 0, 0, 1},
      {"Legendary Gloves", "GloveClass02.bmd", 0, 0, 22},
      {"Bone Gloves", "GloveClass03.bmd", 0, 0, 10},
      {"Leather Gloves", "GloveMale06.bmd", 0, 0, 2},

      // Category 11: Boots (Starting at index 250)
      [250] = {"Bronze Boots", "BootMale01.bmd", 0, 0, 6},
      {"Dragon Boots", "BootMale10.bmd", 0, 0, 40},
      {"Pad Boots", "BootClass01.bmd", 0, 0, 1},
      {"Legendary Boots", "BootClass02.bmd", 0, 0, 22},
      {"Bone Boots", "BootClass03.bmd", 0, 0, 10},
      {"Leather Boots", "BootMale06.bmd", 0, 0, 2},

      // Category 14: Potions (Starting at index 400)
      [400] = {"Apple", "Potion01.bmd", 0, 0, 0},
      {"Small Health Potion", "Potion01.bmd", 0, 0, 0},
      {"Medium Health Potion", "Potion02.bmd", 0, 0, 0},
      {"Large Health Potion", "Potion03.bmd", 0, 0, 0},
      {"Small Mana Potion", "Potion04.bmd", 0, 0, 0},
      {"Medium Mana Potion", "Potion05.bmd", 0, 0, 0},
      {"Large Mana Potion", "Potion06.bmd", 0, 0, 0},
      [413] = {"Jewel of Bless", "Jewel01.bmd", 0, 0, 0},
      {"Jewel of Soul", "Jewel02.bmd", 0, 0, 0},
      {"Zen", "Zen.bmd", 0, 0, 0},
      {"Jewel of Life", "Jewel03.bmd", 0, 0, 0},
  };

  if (defIndex == -1)
    return &zen;
  if (defIndex >= 0 && defIndex < (int)(sizeof(items) / sizeof(items[0])))
    return &items[defIndex];
  return nullptr;
}

static const char *GetDropName(int16_t defIndex) {
  auto *def = GetDropInfo(defIndex);
  return def ? def->name : "Item";
}

static const char *GetDropModelName(int16_t defIndex) {
  auto *def = GetDropInfo(defIndex);
  return def ? def->model : nullptr;
}

// Map equipment category+index to Player body part BMD filename
// Returns empty string if not a body part (e.g. weapons/potions)
static std::string GetBodyPartModelFile(uint8_t category, uint8_t index) {
  // Category 7=Helm, 8=Armor, 9=Pants, 10=Gloves, 11=Boots
  // MU Online naming: HelmMale01.bmd, ArmorMale02.bmd, etc.
  // index is 0-based in our system, files are XX = index+1 padded to 2 digits
  const char *prefixes[] = {"Helm", "Armor", "Pant", "Glove", "Boot"};
  int partCat = category - 7; // 0=Helm...4=Boot
  if (partCat < 0 || partCat > 4)
    return "";
  char buf[64];
  snprintf(buf, sizeof(buf), "%sMale%02d.bmd", prefixes[partCat], index + 1);
  return buf;
}

// Map category to body part index (0=Helm, 1=Armor, 2=Pants, 3=Gloves, 4=Boots)
static int GetBodyPartIndex(uint8_t category) {
  int idx = category - 7;
  if (idx >= 0 && idx <= 4)
    return idx;
  return -1;
}

// Minimal mapping from Client DefIndex -> Server Category/Index
// Based on GetDropInfo table:
// 0: Kris          -> Cat 0,  Idx 0
// 1: Short Sword   -> Cat 0,  Idx 1
// 100: Small Shield -> Cat 6,  Idx 0
// 155: Leather Armor -> Cat 8,  Idx 5
// 201: Small HP    -> Cat 14, Idx 1
// 204: Small MP    -> Cat 14, Idx 4
static void GetItemCategoryAndIndex(int16_t defIndex, uint8_t &cat,
                                    uint8_t &idx) {
  if (defIndex >= 0 && defIndex < 100) {
    cat = 0; // Swords
    idx = defIndex;
  } else if (defIndex >= 100 && defIndex < 150) {
    cat = 6; // Shields
    idx = defIndex - 100;
  } else if (defIndex >= 150 && defIndex < 175) {
    cat = 7; // Helms
    idx = defIndex - 150;
  } else if (defIndex >= 175 && defIndex < 200) {
    cat = 8; // Armor
    idx = defIndex - 175;
  } else if (defIndex >= 200 && defIndex < 225) {
    cat = 9; // Pants
    idx = defIndex - 200;
  } else if (defIndex >= 225 && defIndex < 250) {
    cat = 10; // Gloves
    idx = defIndex - 225;
  } else if (defIndex >= 250 && defIndex < 275) {
    cat = 11; // Boots
    idx = defIndex - 250;
  } else if (defIndex >= 400 && defIndex < 450) {
    cat = 14; // Potions
    idx = defIndex - 400;
  } else {
    cat = 0xFF;
    idx = 0;
  }
}

static int g_dragFromEquipSlot = -1; // -1 if dragging from inventory, else 0-6

static void SpawnDamageNumber(const glm::vec3 &pos, int damage, uint8_t type) {
  for (auto &d : g_floatingDmg) {
    if (!d.active) {
      d.worldPos = pos + glm::vec3(((rand() % 40) - 20), 80.0f + (rand() % 30),
                                   ((rand() % 40) - 20));
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
struct ClientItemDefinition {
  uint8_t category = 0;
  uint8_t itemIndex = 0;
  std::string name;
  std::string modelFile;
  uint16_t reqStr = 0;
  uint16_t reqDex = 0;
  uint16_t reqVit = 0;
  uint16_t reqEne = 0;
  uint16_t levelReq = 0;
  uint8_t width = 1;
  uint8_t height = 1;
  uint32_t classFlags = 0xFFFFFFFF;
};
static std::map<int16_t, ClientItemDefinition> g_itemDefs;

static int16_t GetDefIndexFromCategory(uint8_t category, uint8_t index) {
  for (auto const &[id, def] : g_itemDefs) {
    if (def.category == category && def.itemIndex == index) {
      return id;
    }
  }
  return -1;
}

struct ClientInventoryItem {
  int16_t defIndex = -2; // matches server item_definitions.id
  uint8_t quantity = 0;
  uint8_t itemLevel = 0;
  bool occupied = false;
  bool primary = false;
};
static constexpr int INVENTORY_SLOTS = 64;
static ClientInventoryItem g_inventory[INVENTORY_SLOTS] = {};
static uint32_t g_zen = 0;
static bool g_syncDone =
    false; // Safeguard: don't send updates until initial sync done

static void ClearBagItem(int slot) {
  if (slot < 0 || slot >= INVENTORY_SLOTS)
    return;
  if (!g_inventory[slot].occupied)
    return;

  // Find primary slot if current is secondary
  int primarySlot = slot;
  if (!g_inventory[slot].primary) {
    // Search backward or use stored defIndex to find root
    // For simplicity, we assume we always have the primary slot from the drag
    // start.
  }

  int16_t defIdx = g_inventory[primarySlot].defIndex;
  auto it = g_itemDefs.find(defIdx);
  if (it != g_itemDefs.end()) {
    int w = it->second.width;
    int h = it->second.height;
    int r = primarySlot / 8;
    int c = primarySlot % 8;
    for (int hh = 0; hh < h; hh++) {
      for (int ww = 0; ww < w; ww++) {
        int s = (r + hh) * 8 + (c + ww);
        if (s < INVENTORY_SLOTS) {
          g_inventory[s] = {};
        }
      }
    }
  } else {
    g_inventory[primarySlot] = {};
  }
}

static void SetBagItem(int slot, int16_t defIdx, uint8_t qty, uint8_t lvl) {
  auto it = g_itemDefs.find(defIdx);
  if (it == g_itemDefs.end())
    return;
  int w = it->second.width;
  int h = it->second.height;
  int r = slot / 8;
  int c = slot % 8;

  for (int hh = 0; hh < h; hh++) {
    for (int ww = 0; ww < w; ww++) {
      int s = (r + hh) * 8 + (c + ww);
      if (s < INVENTORY_SLOTS) {
        g_inventory[s].occupied = true;
        g_inventory[s].primary = (hh == 0 && ww == 0);
        g_inventory[s].defIndex = defIdx;
        if (g_inventory[s].primary) {
          g_inventory[s].quantity = qty;
          g_inventory[s].itemLevel = lvl;
        }
      }
    }
  }
}

// Equipment display (populated from 0x24 packet)
struct ClientEquipSlot {
  uint8_t category = 0xFF;
  uint8_t itemIndex = 0;
  uint8_t itemLevel = 0;
  std::string modelFile;
  bool equipped = false;
};
static ClientEquipSlot g_equipSlots[7] = {}; // 7 equipment slots

// Recalculate weapon/defense bonuses from all equipped items
static void RecalcEquipmentStats() {
  int totalDmgMin = 0, totalDmgMax = 0, totalDef = 0;
  for (int s = 0; s < 7; ++s) {
    if (!g_equipSlots[s].equipped)
      continue;
    int16_t defIdx = GetDefIndexFromCategory(g_equipSlots[s].category,
                                             g_equipSlots[s].itemIndex);
    auto *info = GetDropInfo(defIdx);
    if (info) {
      totalDmgMin += info->dmgMin;
      totalDmgMax += info->dmgMax;
      totalDef += info->defense;
    }
  }
  g_hero.SetWeaponBonus(totalDmgMin, totalDmgMax);
  g_hero.SetDefenseBonus(totalDef);
}

// ─── Item Model Rendering System ───
struct LoadedItemModel {
  std::shared_ptr<BMDData> bmd;
  std::vector<MeshBuffers> meshes; // Static buffers (bind pose)
  glm::vec3 transformedMin{0};     // AABB from bone-transformed vertices
  glm::vec3 transformedMax{0};
};

static void UploadStaticMesh(const Mesh_t &mesh, const std::string &texPath,
                             const std::vector<BoneWorldMatrix> &bones,
                             std::vector<MeshBuffers> &outBuffers) {
  MeshBuffers mb;
  mb.vertexCount = (int)mesh.Vertices.size();
  mb.indexCount = (int)mesh.Triangles.size() * 3;
  mb.isDynamic = false;

  // Resolve texture
  std::string fullTexPath = texPath + mesh.TextureName;
  auto texInfo = TextureLoader::ResolveWithInfo(texPath, mesh.TextureName);
  mb.texture = texInfo.textureID;
  mb.hasAlpha = texInfo.hasAlpha;

  // Parse script flags from texture name
  auto flags = TextureLoader::ParseScriptFlags(mesh.TextureName);
  mb.bright = flags.bright;
  mb.hidden = flags.hidden;
  mb.noneBlend = flags.noneBlend;

  if (mb.hidden)
    return;

  // Build static vertices (transform by bind pose)
  std::vector<float> vertices;
  vertices.reserve(mb.vertexCount * 8); // Pos(3) + Norm(3) + UV(2)

  // Map vertex index to normal
  std::vector<glm::vec3> finalNormals(mb.vertexCount, glm::vec3(0, 1, 0));
  for (const auto &n : mesh.Normals) {
    if (n.Node >= 0 && n.Node < (int)bones.size() && n.BindVertex >= 0 &&
        n.BindVertex < mb.vertexCount) {
      glm::vec3 worldNorm =
          MuMath::RotateVector((const float(*)[4]) & bones[n.Node], n.Normal);
      finalNormals[n.BindVertex] = worldNorm;
    }
  }

  for (int i = 0; i < mb.vertexCount; ++i) {
    const auto &v = mesh.Vertices[i];
    glm::vec3 pos = v.Position;
    if (v.Node >= 0 && v.Node < (int)bones.size()) {
      pos = MuMath::TransformPoint((const float(*)[4]) & bones[v.Node], pos);
    }

    // Pos
    vertices.push_back(pos.x);
    vertices.push_back(pos.y);
    vertices.push_back(pos.z);

    // Norm
    vertices.push_back(finalNormals[i].x);
    vertices.push_back(finalNormals[i].y);
    vertices.push_back(finalNormals[i].z);

    // UV
    if (i < (int)mesh.TexCoords.size()) {
      vertices.push_back(mesh.TexCoords[i].TexCoordU);
      vertices.push_back(mesh.TexCoords[i].TexCoordV);
    } else {
      vertices.push_back(0.0f);
      vertices.push_back(0.0f);
    }
  }

  // Build indices
  std::vector<unsigned int> indices;
  indices.reserve(mb.indexCount);
  for (const auto &t : mesh.Triangles) {
    indices.push_back(t.VertexIndex[0]);
    indices.push_back(t.VertexIndex[1]);
    indices.push_back(t.VertexIndex[2]);
  }

  // Upload to GPU
  glGenVertexArrays(1, &mb.vao);
  glGenBuffers(1, &mb.vbo);
  glGenBuffers(1, &mb.ebo);

  glBindVertexArray(mb.vao);

  glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float),
               vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mb.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  // Layout matches shaders/model.vert:
  // 0: Pos(3), 1: Norm(3), 2: UV(2)
  // Stride = 8 * float
  GLsizei stride = 8 * sizeof(float);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                        (void *)(6 * sizeof(float)));

  glBindVertexArray(0);
  outBuffers.push_back(mb);
}

class ItemModelManager {
public:
  static LoadedItemModel *Get(const std::string &filename) {
    if (filename.empty())
      return nullptr;
    auto it = s_cache.find(filename);
    if (it != s_cache.end())
      return &it->second;

    // Load new — try Item/ first, then Player/ (armor models live there)
    LoadedItemModel model;
    std::string foundDir = "Item"; // default
    const char *searchDirs[] = {"Item", "Player"};
    for (const char *dir : searchDirs) {
      std::string path = g_dataPath + "/" + dir + "/" + filename;
      model.bmd = BMDParser::Parse(path);
      if (model.bmd) {
        foundDir = dir;
        break;
      }
    }
    if (!model.bmd) {
      std::cerr << "[Item] Failed to load " << filename
                << " (searched Item/ and Player/)" << std::endl;
      s_cache[filename] = {}; // Cache empty to avoid retry
      return nullptr;
    }

    // Compute static bind pose
    auto bones =
        ComputeBoneMatrices(model.bmd.get(), 0, 0); // Action 0, Frame 0
    std::string texPath = g_dataPath + "/" + foundDir + "/";

    // Compute transformed AABB from bone-transformed vertices
    glm::vec3 tMin(1e9f), tMax(-1e9f);
    for (const auto &mesh : model.bmd->Meshes) {
      UploadStaticMesh(mesh, texPath, bones, model.meshes);
      // Accumulate AABB from transformed positions
      for (int vi = 0; vi < (int)mesh.Vertices.size(); ++vi) {
        glm::vec3 pos = mesh.Vertices[vi].Position;
        int boneIdx = mesh.Vertices[vi].Node;
        if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
          pos = MuMath::TransformPoint((const float(*)[4])bones[boneIdx].data(),
                                       pos);
        }
        tMin = glm::min(tMin, pos);
        tMax = glm::max(tMax, pos);
      }
    }
    model.transformedMin = tMin;
    model.transformedMax = tMax;

    s_cache[filename] = std::move(model);
    return &s_cache[filename];
  }

  // Render for Inventory/UI (uses glViewport)
  static void RenderItemUI(const std::string &filename, int x, int y, int w,
                           int h) {
    LoadedItemModel *model = Get(filename);
    if (!model || !model->bmd)
      return;

    // Preserve GL state
    GLint lastViewport[4];
    glGetIntegerv(GL_VIEWPORT, lastViewport);
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);

    // Setup viewport
    glViewport(x, y, w, h); // Note: y is from bottom in GL
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT); // Clear depth only for this slot

    // Check shader
    Shader *shader = g_hero.GetShader();
    if (!shader)
      return;
    shader->use();

    // Auto-fit camera/model based on bone-transformed AABB
    glm::vec3 min = model->transformedMin;
    glm::vec3 max = model->transformedMax;
    glm::vec3 size = max - min;
    glm::vec3 center = (min + max) * 0.5f;
    float maxDim = std::max(std::max(size.x, size.y), size.z);
    if (maxDim < 10.0f)
      maxDim = 10.0f; // Prevent div/0 or tiny items

    // Fit to view: FOV 45 deg
    float scale = 60.0f / maxDim;

    float camDist = 150.0f;
    glm::mat4 proj =
        glm::perspective(glm::radians(45.0f), (float)w / h, 1.0f, 1000.0f);

    // Camera looking at origin from front
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, camDist), glm::vec3(0, 0, 0),
                                 glm::vec3(0, 1, 0));

    // Model: static upright orientation with slight tilt for classic MU look
    // Rotate -90° around X to stand items upright (BMD models lie flat in XZ)
    glm::mat4 mod = glm::mat4(1.0f);
    mod = glm::rotate(mod, glm::radians(-90.0f),
                      glm::vec3(1, 0, 0)); // Stand upright
    mod = glm::rotate(mod, glm::radians(15.0f),
                      glm::vec3(0, 0, 1)); // Slight tilt
    mod = glm::scale(mod, glm::vec3(scale));
    mod = glm::translate(mod, -center);

    shader->setMat4("projection", proj);
    shader->setMat4("view", view);
    shader->setMat4("model", mod);
    // Light at camera position for even, bright illumination
    shader->setVec3("lightPos", glm::vec3(0, 50, camDist));
    shader->setVec3("viewPos", glm::vec3(0, 0, camDist));
    shader->setFloat("luminosity", 2.0f); // Bright for UI items

    // Render
    for (const auto &mb : model->meshes) {
      if (mb.hidden)
        continue;
      glBindVertexArray(mb.vao);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      shader->setInt("diffuseMap", 0);
      shader->setBool("useTexture", true);
      shader->setVec3("colorTint", glm::vec3(1));

      // Alpha blend if needed
      if (mb.hasAlpha || mb.bright) {
        glEnable(GL_BLEND);
        if (mb.bright)
          glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        else
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      } else {
        glDisable(GL_BLEND); // Opaque
      }

      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);

    // Restore
    glViewport(lastViewport[0], lastViewport[1], lastViewport[2],
               lastViewport[3]);
    if (!depthTest)
      glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
  }

  // Render for World (Ground drops)
  static void RenderItemWorld(const std::string &filename, const glm::vec3 &pos,
                              const glm::mat4 &view, const glm::mat4 &proj,
                              float scale = 1.0f) {
    LoadedItemModel *model = Get(filename);
    if (!model || !model->bmd)
      return;

    Shader *shader = g_hero.GetShader();
    if (!shader)
      return;
    shader->use();

    // Center the model using transformed AABB before rotating
    glm::vec3 tCenter = (model->transformedMin + model->transformedMax) * 0.5f;
    glm::mat4 mod = glm::translate(glm::mat4(1.0f), pos);
    mod = glm::rotate(mod, (float)glfwGetTime() * 2.0f,
                      glm::vec3(0, 1, 0)); // Spin
    mod = glm::scale(mod, glm::vec3(scale));
    mod = glm::translate(mod, -tCenter); // Center before rotate

    shader->setMat4("projection", proj);
    shader->setMat4("view", view);
    shader->setMat4("model", mod);
    shader->setVec3("colorTint", glm::vec3(1)); // Reset tint

    for (const auto &mb : model->meshes) {
      if (mb.hidden)
        continue;
      glBindVertexArray(mb.vao);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      shader->setInt("diffuseMap", 0);
      shader->setBool("useTexture", true);

      if (mb.hasAlpha || mb.bright) {
        glEnable(GL_BLEND);
        if (mb.bright)
          glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        else
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      } else {
        glDisable(GL_BLEND);
      }

      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);
  }

private:
  static std::map<std::string, LoadedItemModel> s_cache;
};
std::map<std::string, LoadedItemModel> ItemModelManager::s_cache;

// Render queue for items (to render on top of UI)
struct ItemRenderJob {
  std::string modelFile;
  int x, y, w, h;
};
static std::vector<ItemRenderJob> g_renderQueue;

// Forward declarations for panel rendering
static UICoords g_hudCoords; // File-scope for mouse callback access

// Data received from server initial burst
struct ServerEquipSlot {
  uint8_t slot = 0;
  WeaponEquipInfo info;
};

struct ServerData {
  std::vector<ServerNpcSpawn> npcs;
  std::vector<ServerMonsterSpawn> monsters;
  std::vector<ServerEquipSlot> equipment;
  bool connected = false;
};

// Parse a single MU packet from the initial server data burst
static void parseInitialPacket(const uint8_t *pkt, int pktSize,
                               ServerData &result) {
  if (pktSize < 3)
    return;
  uint8_t type = pkt[0];

  // C2 packets (4-byte header)
  if (type == 0xC2 && pktSize >= 5) {
    uint8_t headcode = pkt[3];
    std::cout << "[Net] Debug: C2 Packet. Headcode=0x" << std::hex
              << (int)headcode << std::dec << " Size=" << pktSize << std::endl;

    // NPC viewport (0x13)
    if (headcode == 0x13) {
      uint8_t count = pkt[4];
      std::cout << "[Net] NPC viewport: " << (int)count << " NPCs" << std::endl;
      int entryStart = 5;
      for (int i = 0; i < count; i++) {
        int off = entryStart + i * 9;
        if (off + 9 > pktSize)
          break;
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
        if (off + entrySize > pktSize)
          break;
        ServerMonsterSpawn mon;
        mon.serverIndex = (uint16_t)((pkt[off + 0] << 8) | pkt[off + 1]);
        mon.monsterType = (uint16_t)((pkt[off + 2] << 8) | pkt[off + 3]);
        mon.gridX = pkt[off + 4];
        mon.gridY = pkt[off + 5];
        mon.dir = pkt[off + 6];
        result.monsters.push_back(mon);
        std::cout << "[Net]   Monster idx=" << mon.serverIndex
                  << " type=" << mon.monsterType << " grid=(" << (int)mon.gridX
                  << "," << (int)mon.gridY << ") dir=" << (int)mon.dir
                  << std::endl;
      }
    }

    // Inventory sync (0x36) — C2: header(4) + zen(4) + count(1) + N*item(4)
    if (headcode == 0x36 && pktSize >= 9) {
      uint32_t zen;
      std::memcpy(&zen, pkt + 4, 4);
      g_zen = zen;
      uint8_t count = pkt[8];
      for (auto &item : g_inventory)
        item = {};
      for (int i = 0; i < count; i++) {
        int off = 9 + i * 5; // Updated entrySize to 5
        if (off + 5 > pktSize)
          break;
        uint8_t slot = pkt[off];
        if (slot < INVENTORY_SLOTS) {
          int16_t defIdx = (int16_t)(pkt[off + 1] | (pkt[off + 2] << 8));
          uint8_t qty = pkt[off + 3];
          uint8_t lvl = pkt[off + 4];

          g_inventory[slot].defIndex = defIdx;
          g_inventory[slot].quantity = qty;
          g_inventory[slot].itemLevel = lvl;
          g_inventory[slot].occupied = true;
          g_inventory[slot].primary = true;

          // Mark secondary slots
          auto it = g_itemDefs.find(defIdx);
          if (it != g_itemDefs.end()) {
            int w = it->second.width;
            int h = it->second.height;
            int row = slot / 8;
            int col = slot % 8;
            for (int hh = 0; hh < h; hh++) {
              for (int ww = 0; ww < w; ww++) {
                if (hh == 0 && ww == 0)
                  continue;
                int s = (row + hh) * 8 + (col + ww);
                if (s < INVENTORY_SLOTS && (col + ww) < 8) {
                  g_inventory[s].occupied = true;
                  g_inventory[s].primary = false;
                  g_inventory[s].defIndex = defIdx;
                }
              }
            }
          }
        }
      }
      std::cout << "[Net] Inventory sync: " << (int)count
                << " items, zen=" << g_zen << std::endl;
    }

    // Equipment (0x24) - Upgraded to C2 to support >255 bytes (7 slots * 36 =
    // 252 + 5 = 257)
    if (headcode == 0x24 && pktSize >= 5) {
      uint8_t count = pkt[4];
      std::cout << "[Net] Equipment (C2): " << (int)count << " slots"
                << std::endl;
      int entryStart = 5;
      int entrySize = 4 + 32;
      for (int i = 0; i < count; i++) {
        int off = entryStart + i * entrySize;
        if (off + entrySize > pktSize)
          break;
        WeaponEquipInfo weapon;
        uint8_t slot = pkt[off + 0];
        weapon.category = pkt[off + 1];
        weapon.itemIndex = pkt[off + 2];
        weapon.itemLevel = pkt[off + 3];
        char modelFile[33] = {};
        std::memcpy(modelFile, &pkt[off + 4], 32);
        weapon.modelFile = modelFile;
        std::cout << "[HANDLER:Initial] Slot " << (int)slot << ": "
                  << weapon.modelFile << " cat=" << (int)weapon.category
                  << " idx=" << (int)weapon.itemIndex << " +"
                  << (int)weapon.itemLevel << std::endl;

        // Collect ALL equipment slots (weapon, shield, armor etc.)
        result.equipment.push_back({slot, weapon});
        // Populate all equipment display slots
        if (slot < 7) {
          g_equipSlots[slot].category = weapon.category;
          g_equipSlots[slot].itemIndex = weapon.itemIndex;
          g_equipSlots[slot].itemLevel = weapon.itemLevel;
          g_equipSlots[slot].modelFile = weapon.modelFile;
          g_equipSlots[slot].equipped = (weapon.category != 0xFF);
        }
      }
    }
  }

  // C1 packets (2-byte header)
  if (type == 0xC1 && pktSize >= 3) {
    uint8_t headcode = pkt[2];
    std::cout << "[Net] Debug: C1 Packet. Headcode=0x" << std::hex
              << (int)headcode << std::dec << " Size=" << pktSize << std::endl;

    // Monster viewport V1 (0x1F) — all Lorencia monster spawns
    if (headcode == 0x1F && pktSize >= 4) {
      uint8_t count = pkt[3];
      std::cout << "[Net] Monster viewport V1: " << (int)count << " monsters"
                << std::endl;
      int entryStart = 4;
      int entrySize = 5; // sizeof(PMSG_MONSTER_VIEWPORT_ENTRY)
      for (int i = 0; i < count; i++) {
        int off = entryStart + i * entrySize;
        if (off + entrySize > pktSize)
          break;
        ServerMonsterSpawn mon;
        mon.monsterType = (uint16_t)((pkt[off] << 8) | pkt[off + 1]);
        mon.gridX = pkt[off + 2];
        mon.gridY = pkt[off + 3];
        mon.dir = pkt[off + 4];
        result.monsters.push_back(mon);
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
  if (pktSize < 3)
    return;
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
        g_monsterManager.TriggerHitAnimation(idx);

        // Spawn blood and hit sparks (P1)
        glm::vec3 monPos = g_monsterManager.GetMonsterInfo(idx).position;
        g_vfxManager.SpawnBurst(ParticleType::BLOOD,
                                monPos + glm::vec3(0, 50, 0), 12);
        g_vfxManager.SpawnBurst(ParticleType::HIT_SPARK,
                                monPos + glm::vec3(0, 50, 0), 6);

        // Floating damage number above monster (simple version)
        std::cout << "[Combat] Hit monster " << p->monsterIndex << " for "
                  << p->damage << " damage" << std::endl;
      }
    }

    // Monster death (0x2A)
    if (headcode == 0x2A && pktSize >= (int)sizeof(PMSG_MONSTER_DEATH_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_DEATH_SEND *>(pkt);
      int idx = g_monsterManager.FindByServerIndex(p->monsterIndex);
      if (idx >= 0)
        g_monsterManager.SetMonsterDying(idx);

      // Apply XP reward and sync to server DB
      uint32_t xp = p->xpReward;
      if (xp > 0) {
        g_hero.GainExperience(xp);
        g_serverXP = (int64_t)g_hero.GetExperience();
        g_serverLevel = g_hero.GetLevel();
        g_serverLevelUpPoints = g_hero.GetLevelUpPoints();
        g_serverHP = g_hero.GetHP();
        g_serverMaxHP = g_hero.GetMaxHP();

        // Persist to DB via save packet
        PMSG_CHARSAVE_RECV save{};
        save.h = MakeC1Header(sizeof(save), 0x26);
        save.characterId = 1;
        save.level = (uint16_t)g_serverLevel;
        save.strength = (uint16_t)g_serverStr;
        save.dexterity = (uint16_t)g_serverDex;
        save.vitality = (uint16_t)g_serverVit;
        save.energy = (uint16_t)g_serverEne;
        save.life = (uint16_t)g_serverHP;
        save.maxLife = (uint16_t)g_serverMaxHP;
        save.levelUpPoints = (uint16_t)g_serverLevelUpPoints;
        save.experienceLo = (uint32_t)((uint64_t)g_serverXP & 0xFFFFFFFF);
        save.experienceHi = (uint32_t)((uint64_t)g_serverXP >> 32);
        g_net.Send(&save, sizeof(save));
      }
    }

    // Monster attack player (0x2F) — monster hits hero
    if (headcode == 0x2F && pktSize >= (int)sizeof(PMSG_MONSTER_ATTACK_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_ATTACK_SEND *>(pkt);
      int idx = g_monsterManager.FindByServerIndex(p->monsterIndex);
      if (idx >= 0)
        g_monsterManager.TriggerAttackAnimation(idx);

      // --- SafeZone Enforcement ---
      if (g_hero.IsInSafeZone()) {
        // Ignore damage if hero is in SafeZone (city/town)
        return;
      }

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

    // Stat allocation response (0x38)
    if (headcode == 0x38 && pktSize >= (int)sizeof(PMSG_STAT_ALLOC_SEND)) {
      auto *resp = reinterpret_cast<const PMSG_STAT_ALLOC_SEND *>(pkt);
      if (resp->result) {
        switch (resp->statType) {
        case 0:
          g_serverStr = resp->newValue;
          break;
        case 1:
          g_serverDex = resp->newValue;
          break;
        case 2:
          g_serverVit = resp->newValue;
          break;
        case 3:
          g_serverEne = resp->newValue;
          break;
        }
        g_serverLevelUpPoints = resp->levelUpPoints;
        g_serverMaxHP = resp->maxLife;
        std::cout << "[Net] Stat alloc OK: type=" << (int)resp->statType
                  << " val=" << resp->newValue << " pts=" << resp->levelUpPoints
                  << std::endl;
      }
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
                    << " (idx=" << p->dropIndex << ") at (" << p->worldX << ","
                    << p->worldZ << ")" << std::endl;
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
        case 0:
          g_serverStr = resp->newValue;
          break;
        case 1:
          g_serverDex = resp->newValue;
          break;
        case 2:
          g_serverVit = resp->newValue;
          break;
        case 3:
          g_serverEne = resp->newValue;
          break;
        }
        g_serverLevelUpPoints = resp->levelUpPoints;
        g_serverMaxHP = resp->maxLife;
        std::cout << "[Net] Stat alloc: type=" << (int)resp->statType
                  << " val=" << resp->newValue << " pts=" << resp->levelUpPoints
                  << std::endl;
      }
    }

    // Equipment (0x24) - Sync during gameplay
    if (headcode == 0x24 && pktSize >= 4) {
      uint8_t count = pkt[3];
      int entryStart = 4;
      int entrySize = 4 + 32;
      for (int i = 0; i < count; i++) {
        int off = entryStart + i * entrySize;
        if (off + entrySize > pktSize)
          break;
        uint8_t slot = pkt[off + 0];
        if (slot < 7) {
          g_equipSlots[slot].category = pkt[off + 1];
          g_equipSlots[slot].itemIndex = pkt[off + 2];
          g_equipSlots[slot].itemLevel = pkt[off + 3];
          char modelFile[33] = {};
          std::memcpy(modelFile, &pkt[off + 4], 32);
          g_equipSlots[slot].modelFile = modelFile;
          g_equipSlots[slot].equipped = true;
        }
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
      // Clear client grid
      for (auto &item : g_inventory)
        item = {};

      const int itemSize = 5; // slot(1) + cat(1) + idx(1) + qty(1) + lvl(1)
      for (int i = 0; i < count; i++) {
        int off = 9 + i * itemSize;
        if (off + itemSize > pktSize)
          break;

        uint8_t slot = pkt[off];
        uint8_t cat = pkt[off + 1];
        uint8_t idx = pkt[off + 2];
        uint8_t qty = pkt[off + 3];
        uint8_t lvl = pkt[off + 4];

        if (slot < INVENTORY_SLOTS) {
          int16_t defIdx = GetDefIndexFromCategory(cat, idx);
          if (defIdx != -1) {
            SetBagItem(slot, defIdx, qty, lvl);
          } else {
            // Fallback for unknown items: at least mark slot as occupied so
            // it's not empty
            g_inventory[slot].occupied = true;
            g_inventory[slot].primary = true;
            g_inventory[slot].quantity = qty;
            g_inventory[slot].itemLevel = lvl;
          }
        }
      }
      g_syncDone = true;
    }

    // Equipment sync (0x24)
    if (headcode == 0x24 && pktSize >= 5) {
      uint8_t count = pkt[4];
      std::cout << "[HANDLER:Gameplay] Equipment Update: " << (int)count
                << " slots" << std::endl;
      int entryStart = 5;
      int entrySize = 1 + 1 + 1 + 1 + 32;
      for (int i = 0; i < count; i++) {
        int off = entryStart + i * entrySize;
        if (off + entrySize > pktSize)
          break;

        uint8_t slot = pkt[off + 0];
        WeaponEquipInfo weapon;
        weapon.category = pkt[off + 1];
        weapon.itemIndex = pkt[off + 2];
        weapon.itemLevel = pkt[off + 3];
        char modelBuf[33] = {};
        std::memcpy(modelBuf, &pkt[off + 4], 32);
        weapon.modelFile = modelBuf;
        std::cout << "  Slot " << (int)slot << ": " << weapon.modelFile
                  << std::endl;

        // Apply to g_hero immediately
        if (slot == 0) {
          g_hero.EquipWeapon(weapon);
        } else if (slot == 1) {
          g_hero.EquipShield(weapon);
        } else {
          int bodyPart = GetBodyPartIndex(weapon.category);
          if (bodyPart >= 0) {
            std::string partModel =
                GetBodyPartModelFile(weapon.category, weapon.itemIndex);
            if (!partModel.empty())
              g_hero.EquipBodyPart(bodyPart, partModel);
          }
        }

        // Apply to UI slots
        if (slot < 7) {
          g_equipSlots[slot].category = weapon.category;
          g_equipSlots[slot].itemIndex = weapon.itemIndex;
          g_equipSlots[slot].itemLevel = weapon.itemLevel;
          g_equipSlots[slot].modelFile = weapon.modelFile;
          g_equipSlots[slot].equipped = (weapon.category != 0xFF);
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
        std::cout << "[Ray] Hit monster " << i << " (dist=" << t << ")"
                  << std::endl;
      }
    }
  }
  if (bestIdx == -1) {
    // std::cout << "[Ray] No monster hit. RayO=" << rayO.x << "," << rayO.y <<
    // "," << rayO.z << std::endl;
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
    float r =
        info.radius * 3.5f; // Inflated pick radius for easier monster targeting
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

    // Check cylinder walls
    for (float t : {t0, t1}) {
      if (t < 0)
        continue;
      float hitY = rayO.y + rayD.y * t;
      if (hitY >= yMin && hitY <= yMax && t < bestT) {
        bestT = t;
        bestIdx = i;
      }
    }

    // Check Top Cap (Disk at yMax)
    if (rayD.y != 0.0f) {
      float tCap = (yMax - rayO.y) / rayD.y;
      if (tCap > 0 && tCap < bestT) {
        glm::vec3 pCap = rayO + rayD * tCap;
        float distSq = (pCap.x - info.position.x) * (pCap.x - info.position.x) +
                       (pCap.z - info.position.z) * (pCap.z - info.position.z);
        if (distSq <= r * r) {
          bestT = tCap;
          bestIdx = i;
        }
      }
    }
  }
  if (bestIdx != -1) {
    std::cout << "[Mouse] RayPick hit Monster " << bestIdx << " dist=" << bestT
              << std::endl;
  }
  return bestIdx;
}

// Drag state for inventory (declared here for mouse callback access)
static int g_dragFromSlot = -1;
static int16_t g_dragDefIndex = -2;
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
        if (HandlePanelClick(vx, vy))
          return;
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
  if (ImGui::GetIO().WantCaptureKeyboard)
    return;

  if (action == GLFW_PRESS) {
    if (key == GLFW_KEY_C)
      g_showCharInfo = !g_showCharInfo;
    if (key == GLFW_KEY_I)
      g_showInventory = !g_showInventory;
    if (key == GLFW_KEY_ESCAPE) {
      if (g_showCharInfo)
        g_showCharInfo = false;
      else if (g_showInventory)
        g_showInventory = false;
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

static const char *GetEquipSlotName(int slot) {
  static const char *names[] = {"R.Hand", "L.Hand", "Helm", "Armor",
                                "Pants",  "Gloves", "Boots"};
  if (slot >= 0 && slot < 7)
    return names[slot];
  return "???";
}

static void InitItemDefinitions() {
  // Matches 0.97d server seeding
  auto addDef = [](int16_t id, uint8_t cat, uint8_t idx, const char *name,
                   const char *mod, uint8_t w, uint8_t h, uint16_t s,
                   uint16_t d, uint16_t v, uint16_t e, uint16_t l,
                   uint32_t cf) {
    ClientItemDefinition cd;
    cd.category = cat;
    cd.itemIndex = idx;
    cd.name = name;
    cd.modelFile = mod;
    cd.width = w;
    cd.height = h;
    cd.reqStr = s;
    cd.reqDex = d;
    cd.reqVit = v;
    cd.reqEne = e;
    cd.levelReq = l;
    cd.classFlags = cf;
    g_itemDefs[id] = cd;
  };

  // IDs are used locally as keys in g_itemDefs.
  // We'll use IDs that won't collide with the server's autoincrement range if
  // possible, but since we sync by (Cat, Idx), the actual ID value here is
  // arbitrary as long as it's unique.

  // Category 0: Swords
  //                id  cat idx  name              model         w  h  str dex
  addDef(0, 0, 0, "Kris", "Sword01.bmd", 1, 2, 80, 40, 0, 0, 0, 15);
  addDef(1, 0, 1, "Short Sword", "Sword02.bmd", 1, 3, 100, 0, 0, 0, 0, 15);
  addDef(2, 0, 2, "Rapier", "Sword03.bmd", 1, 3, 50, 40, 0, 0, 0, 15);
  addDef(3, 0, 3, "Katana", "Sword04.bmd", 1, 3, 80, 40, 0, 0, 0, 2);
  addDef(4, 0, 4, "Sword of Assassin", "Sword05.bmd", 1, 3, 60, 40, 0, 0, 0, 2);
  addDef(5, 0, 5, "Blade", "Sword06.bmd", 1, 3, 80, 50, 0, 0, 0, 15);
  addDef(6, 0, 6, "Gladius", "Sword07.bmd", 1, 3, 110, 0, 0, 0, 0, 15);
  addDef(7, 0, 7, "Falchion", "Sword08.bmd", 1, 3, 120, 0, 0, 0, 0, 2);
  addDef(8, 0, 8, "Serpent Sword", "Sword09.bmd", 1, 3, 130, 0, 0, 0, 0, 2);
  addDef(9, 0, 9, "Sword of Salamander", "Sword10.bmd", 2, 3, 103, 0, 0, 0, 0,
         2);
  addDef(10, 0, 10, "Light Base", "Sword11.bmd", 2, 4, 80, 60, 0, 0, 0, 15);
  addDef(11, 0, 11, "Legendary Sword", "Sword12.bmd", 2, 3, 120, 0, 0, 0, 0, 2);
  addDef(12, 0, 12, "Heliacal Sword", "Sword13.bmd", 2, 3, 140, 0, 0, 0, 0, 2);
  addDef(13, 0, 13, "Double Blade", "Sword14.bmd", 1, 3, 70, 70, 0, 0, 0, 15);
  addDef(14, 0, 14, "Lighting Sword", "Sword15.bmd", 1, 3, 90, 50, 0, 0, 0, 15);
  addDef(15, 0, 15, "Giant Sword", "Sword16.bmd", 2, 3, 140, 0, 0, 0, 0, 2);

  // Category 1: Axes
  addDef(20, 1, 0, "Small Axe", "Axe01.bmd", 1, 3, 50, 0, 0, 0, 0, 15);
  addDef(21, 1, 1, "Hand Axe", "Axe02.bmd", 1, 3, 70, 0, 0, 0, 0, 15);
  addDef(22, 1, 2, "Double Axe", "Axe03.bmd", 1, 3, 90, 0, 0, 0, 0, 2);
  addDef(23, 1, 3, "Tomahawk", "Axe04.bmd", 1, 3, 100, 0, 0, 0, 0, 2);
  addDef(24, 1, 4, "Elven Axe", "Axe05.bmd", 1, 3, 50, 70, 0, 0, 0, 5);
  addDef(25, 1, 5, "Battle Axe", "Axe06.bmd", 2, 3, 120, 0, 0, 0, 0, 15);
  addDef(26, 1, 6, "Nikkea Axe", "Axe07.bmd", 2, 3, 130, 0, 0, 0, 0, 15);
  addDef(27, 1, 7, "Larkan Axe", "Axe08.bmd", 2, 3, 140, 0, 0, 0, 0, 2);
  addDef(28, 1, 8, "Crescent Axe", "Axe09.bmd", 2, 3, 100, 40, 0, 0, 0, 11);

  // Category 2: Maces
  addDef(30, 2, 0, "Mace", "Mace01.bmd", 1, 3, 100, 0, 0, 0, 0, 2);
  addDef(31, 2, 1, "Morning Star", "Mace02.bmd", 1, 3, 100, 0, 0, 0, 0, 2);
  addDef(32, 2, 2, "Flail", "Mace03.bmd", 1, 3, 80, 50, 0, 0, 0, 2);
  addDef(33, 2, 3, "Great Hammer", "Mace04.bmd", 2, 3, 150, 0, 0, 0, 0, 2);
  addDef(34, 2, 4, "Crystal Morning Star", "Mace05.bmd", 2, 3, 130, 0, 0, 0, 0,
         15);
  addDef(35, 2, 5, "Crystal Sword", "Mace06.bmd", 2, 4, 130, 70, 0, 0, 0, 15);
  addDef(36, 2, 6, "Chaos Dragon Axe", "Mace07.bmd", 2, 4, 140, 50, 0, 0, 0, 2);

  // Category 3: Spears
  addDef(40, 3, 0, "Light Spear", "Spear01.bmd", 2, 4, 60, 70, 0, 0, 0, 15);
  addDef(41, 3, 1, "Spear", "Spear02.bmd", 2, 4, 70, 50, 0, 0, 0, 15);
  addDef(42, 3, 2, "Dragon Lance", "Spear03.bmd", 2, 4, 70, 50, 0, 0, 0, 15);
  addDef(43, 3, 3, "Giant Trident", "Spear04.bmd", 2, 4, 90, 30, 0, 0, 0, 15);
  addDef(44, 3, 4, "Serpent Spear", "Spear05.bmd", 2, 4, 90, 30, 0, 0, 0, 15);
  addDef(45, 3, 5, "Double Poleaxe", "Spear06.bmd", 2, 4, 70, 50, 0, 0, 0, 15);
  addDef(46, 3, 6, "Halberd", "Spear07.bmd", 2, 4, 70, 50, 0, 0, 0, 15);
  addDef(47, 3, 7, "Berdysh", "Spear08.bmd", 2, 4, 80, 50, 0, 0, 0, 15);
  addDef(48, 3, 8, "Great Scythe", "Spear09.bmd", 2, 4, 90, 50, 0, 0, 0, 15);
  addDef(49, 3, 9, "Bill of Balrog", "Spear10.bmd", 2, 4, 80, 50, 0, 0, 0, 15);

  // Category 4: Bows & Crossbows
  addDef(50, 4, 0, "Short Bow", "Bow01.bmd", 2, 3, 20, 80, 0, 0, 0, 4);
  addDef(51, 4, 1, "Bow", "Bow02.bmd", 2, 3, 30, 90, 0, 0, 0, 4);
  addDef(52, 4, 2, "Elven Bow", "Bow03.bmd", 2, 3, 30, 90, 0, 0, 0, 4);
  addDef(53, 4, 3, "Battle Bow", "Bow04.bmd", 2, 3, 30, 90, 0, 0, 0, 4);
  addDef(54, 4, 4, "Tiger Bow", "Bow05.bmd", 2, 4, 30, 100, 0, 0, 0, 4);
  addDef(55, 4, 5, "Silver Bow", "Bow06.bmd", 2, 4, 30, 100, 0, 0, 0, 4);
  addDef(56, 4, 6, "Chaos Nature Bow", "Bow07.bmd", 2, 4, 40, 150, 0, 0, 0, 4);
  addDef(57, 4, 8, "Crossbow", "CrossBow01.bmd", 2, 2, 20, 90, 0, 0, 0, 4);
  addDef(58, 4, 9, "Golden Crossbow", "CrossBow02.bmd", 2, 2, 30, 90, 0, 0, 0,
         4);
  addDef(59, 4, 10, "Arquebus", "CrossBow03.bmd", 2, 2, 30, 90, 0, 0, 0, 4);
  addDef(60, 4, 11, "Light Crossbow", "CrossBow04.bmd", 2, 3, 30, 90, 0, 0, 0,
         4);
  addDef(61, 4, 12, "Serpent Crossbow", "CrossBow05.bmd", 2, 3, 30, 100, 0, 0,
         0, 4);
  addDef(62, 4, 13, "Bluewing Crossbow", "CrossBow06.bmd", 2, 3, 40, 110, 0, 0,
         0, 4);
  addDef(63, 4, 14, "Aquagold Crossbow", "CrossBow07.bmd", 2, 3, 50, 130, 0, 0,
         0, 4);

  // Category 5: Staffs
  addDef(70, 5, 0, "Skull Staff", "Staff01.bmd", 1, 3, 40, 0, 0, 0, 0, 1);
  addDef(71, 5, 1, "Angelic Staff", "Staff02.bmd", 2, 3, 50, 0, 0, 0, 0, 1);
  addDef(72, 5, 2, "Serpent Staff", "Staff03.bmd", 2, 3, 50, 0, 0, 0, 0, 1);
  addDef(73, 5, 3, "Thunder Staff", "Staff04.bmd", 2, 4, 40, 10, 0, 0, 0, 1);
  addDef(74, 5, 4, "Gorgon Staff", "Staff05.bmd", 2, 4, 50, 0, 0, 0, 0, 1);
  addDef(75, 5, 5, "Legendary Staff", "Staff06.bmd", 1, 4, 50, 0, 0, 0, 0, 1);
  addDef(76, 5, 6, "Staff of Resurrection", "Staff07.bmd", 1, 4, 60, 10, 0, 0,
         0, 1);
  addDef(77, 5, 7, "Chaos Lightning Staff", "Staff08.bmd", 2, 4, 60, 10, 0, 0,
         0, 1);

  // Category 6: Shields
  addDef(100, 6, 0, "Small Shield", "Shield01.bmd", 2, 2, 70, 0, 0, 0, 0, 15);
  addDef(101, 6, 1, "Horn Shield", "Shield02.bmd", 2, 2, 100, 0, 0, 0, 0, 2);
  addDef(102, 6, 2, "Kite Shield", "Shield03.bmd", 2, 2, 110, 0, 0, 0, 0, 2);
  addDef(103, 6, 3, "Elven Shield", "Shield04.bmd", 2, 2, 30, 100, 0, 0, 0, 4);
  addDef(104, 6, 4, "Buckler", "Shield05.bmd", 2, 2, 80, 0, 0, 0, 0, 15);
  addDef(105, 6, 5, "Dragon Slayer Shield", "Shield06.bmd", 2, 2, 100, 40, 0, 0,
         0, 2);
  addDef(106, 6, 6, "Skull Shield", "Shield07.bmd", 2, 2, 110, 0, 0, 0, 0, 15);
  addDef(107, 6, 7, "Spiked Shield", "Shield08.bmd", 2, 2, 130, 0, 0, 0, 0, 2);
  addDef(108, 6, 8, "Tower Shield", "Shield09.bmd", 2, 2, 130, 0, 0, 0, 0, 11);
  addDef(109, 6, 9, "Plate Shield", "Shield10.bmd", 2, 2, 120, 0, 0, 0, 0, 2);
  addDef(110, 6, 10, "Big Round Shield", "Shield11.bmd", 2, 2, 120, 0, 0, 0, 0,
         2);
  addDef(111, 6, 11, "Serpent Shield", "Shield12.bmd", 2, 2, 130, 0, 0, 0, 0,
         2);
  addDef(112, 6, 12, "Bronze Shield", "Shield13.bmd", 2, 2, 140, 0, 0, 0, 0, 2);
  addDef(113, 6, 13, "Dragon Shield", "Shield14.bmd", 2, 2, 120, 40, 0, 0, 0,
         2);
  addDef(114, 6, 14, "Legendary Shield", "Shield15.bmd", 2, 3, 90, 25, 0, 0, 0,
         5);

  // Category 7: Helmets
  addDef(200, 7, 0, "Bronze Helm", "HelmMale01.bmd", 2, 2, 60, 0, 0, 0, 0, 2);
  addDef(201, 7, 1, "Dragon Helm", "HelmMale02.bmd", 2, 2, 120, 30, 0, 0, 0, 2);
  addDef(202, 7, 2, "Pad Helm", "HelmClass01.bmd", 2, 2, 20, 0, 0, 0, 0, 1);
  addDef(203, 7, 3, "Legendary Helm", "HelmClass02.bmd", 2, 2, 30, 0, 0, 0, 0,
         1);
  addDef(204, 7, 4, "Bone Helm", "HelmClass03.bmd", 2, 2, 30, 0, 0, 0, 0, 1);
  addDef(205, 7, 5, "Leather Helm", "HelmMale06.bmd", 2, 2, 80, 0, 0, 0, 0, 2);
  addDef(206, 7, 6, "Scale Helm", "HelmMale07.bmd", 2, 2, 110, 0, 0, 0, 0, 2);
  addDef(207, 7, 7, "Sphinx Mask", "HelmClass04.bmd", 2, 2, 30, 0, 0, 0, 0, 1);
  addDef(208, 7, 8, "Brass Helm", "HelmMale09.bmd", 2, 2, 100, 30, 0, 0, 0, 2);
  addDef(209, 7, 9, "Plate Helm", "HelmMale10.bmd", 2, 2, 130, 0, 0, 0, 0, 2);
  addDef(210, 7, 10, "Vine Helm", "HelmClass05.bmd", 2, 2, 30, 60, 0, 0, 0, 4);
  addDef(211, 7, 11, "Silk Helm", "HelmClass06.bmd", 2, 2, 30, 70, 0, 0, 0, 4);
  addDef(212, 7, 12, "Wind Helm", "HelmClass07.bmd", 2, 2, 30, 80, 0, 0, 0, 4);
  addDef(213, 7, 13, "Spirit Helm", "HelmClass08.bmd", 2, 2, 40, 80, 0, 0, 0,
         4);
  addDef(214, 7, 14, "Guardian Helm", "HelmClass09.bmd", 2, 2, 40, 80, 0, 0, 0,
         4);

  // Category 8: Armors
  addDef(300, 8, 0, "Bronze Armor", "ArmorMale01.bmd", 2, 3, 80, 20, 0, 0, 0,
         2);
  addDef(301, 8, 1, "Dragon Armor", "ArmorMale02.bmd", 2, 3, 120, 30, 0, 0, 0,
         2);
  addDef(302, 8, 2, "Pad Armor", "ArmorClass01.bmd", 2, 2, 30, 0, 0, 0, 0, 1);
  addDef(303, 8, 3, "Legendary Armor", "ArmorClass02.bmd", 2, 2, 40, 0, 0, 0, 0,
         1);
  addDef(304, 8, 4, "Bone Armor", "ArmorClass03.bmd", 2, 2, 40, 0, 0, 0, 0, 1);
  addDef(305, 8, 5, "Leather Armor", "ArmorMale06.bmd", 2, 3, 80, 0, 0, 0, 0,
         2);
  addDef(306, 8, 6, "Scale Armor", "ArmorMale07.bmd", 2, 2, 110, 0, 0, 0, 0, 2);
  addDef(307, 8, 7, "Sphinx Armor", "ArmorClass04.bmd", 2, 3, 40, 0, 0, 0, 0,
         1);
  addDef(308, 8, 8, "Brass Armor", "ArmorMale09.bmd", 2, 2, 100, 30, 0, 0, 0,
         2);
  addDef(309, 8, 9, "Plate Armor", "ArmorMale10.bmd", 2, 2, 130, 0, 0, 0, 0, 2);
  addDef(310, 8, 10, "Vine Armor", "ArmorClass05.bmd", 2, 2, 30, 60, 0, 0, 0,
         4);
  addDef(311, 8, 11, "Silk Armor", "ArmorClass06.bmd", 2, 2, 30, 70, 0, 0, 0,
         4);
  addDef(312, 8, 12, "Wind Armor", "ArmorClass07.bmd", 2, 2, 30, 80, 0, 0, 0,
         4);
  addDef(313, 8, 13, "Spirit Armor", "ArmorClass08.bmd", 2, 2, 40, 80, 0, 0, 0,
         4);
  addDef(314, 8, 14, "Guardian Armor", "ArmorClass09.bmd", 2, 2, 40, 80, 0, 0,
         0, 4);

  // Category 9: Pants
  addDef(400, 9, 0, "Bronze Pants", "PantMale01.bmd", 2, 2, 80, 20, 0, 0, 0, 2);
  addDef(401, 9, 1, "Dragon Pants", "PantMale02.bmd", 2, 2, 120, 30, 0, 0, 0,
         2);
  addDef(402, 9, 2, "Pad Pants", "PantClass01.bmd", 2, 2, 30, 0, 0, 0, 0, 1);
  addDef(403, 9, 3, "Legendary Pants", "PantClass02.bmd", 2, 2, 40, 0, 0, 0, 0,
         1);
  addDef(404, 9, 4, "Bone Pants", "PantClass03.bmd", 2, 2, 40, 0, 0, 0, 0, 1);
  addDef(405, 9, 5, "Leather Pants", "PantMale06.bmd", 2, 2, 80, 0, 0, 0, 0, 2);
  addDef(406, 9, 6, "Scale Pants", "PantMale07.bmd", 2, 2, 110, 0, 0, 0, 0, 2);
  addDef(407, 9, 7, "Sphinx Pants", "PantClass04.bmd", 2, 2, 40, 0, 0, 0, 0, 1);
  addDef(408, 9, 8, "Brass Pants", "PantMale09.bmd", 2, 2, 100, 30, 0, 0, 0, 2);
  addDef(409, 9, 9, "Plate Pants", "PantMale10.bmd", 2, 2, 130, 0, 0, 0, 0, 2);
  addDef(410, 9, 10, "Vine Pants", "PantClass05.bmd", 2, 2, 30, 60, 0, 0, 0, 4);
  addDef(411, 9, 11, "Silk Pants", "PantClass06.bmd", 2, 2, 30, 70, 0, 0, 0, 4);
  addDef(412, 9, 12, "Wind Pants", "PantClass07.bmd", 2, 2, 30, 80, 0, 0, 0, 4);
  addDef(413, 9, 13, "Spirit Pants", "PantClass08.bmd", 2, 2, 40, 80, 0, 0, 0,
         4);
  addDef(414, 9, 14, "Guardian Pants", "PantClass09.bmd", 2, 2, 40, 80, 0, 0, 0,
         4);

  // Category 10: Gloves
  addDef(500, 10, 0, "Bronze Gloves", "GloveMale01.bmd", 2, 2, 80, 20, 0, 0, 0,
         2);
  addDef(501, 10, 1, "Dragon Gloves", "GloveMale02.bmd", 2, 2, 120, 30, 0, 0, 0,
         2);
  addDef(502, 10, 2, "Pad Gloves", "GloveClass01.bmd", 2, 2, 20, 0, 0, 0, 0, 1);
  addDef(503, 10, 3, "Legendary Gloves", "GloveClass02.bmd", 2, 2, 20, 0, 0, 0,
         0, 1);
  addDef(504, 10, 4, "Bone Gloves", "GloveClass03.bmd", 2, 2, 20, 0, 0, 0, 0,
         1);
  addDef(505, 10, 5, "Leather Gloves", "GloveMale06.bmd", 2, 2, 80, 0, 0, 0, 0,
         2);
  addDef(506, 10, 6, "Scale Gloves", "GloveMale07.bmd", 2, 2, 110, 0, 0, 0, 0,
         2);
  addDef(507, 10, 7, "Sphinx Gloves", "GloveClass04.bmd", 2, 2, 20, 0, 0, 0, 0,
         1);
  addDef(508, 10, 8, "Brass Gloves", "GloveMale09.bmd", 2, 2, 100, 30, 0, 0, 0,
         2);
  addDef(509, 10, 9, "Plate Gloves", "GloveMale10.bmd", 2, 2, 130, 0, 0, 0, 0,
         2);
  addDef(510, 10, 10, "Vine Gloves", "GloveClass05.bmd", 2, 2, 30, 60, 0, 0, 0,
         4);
  addDef(511, 10, 11, "Silk Gloves", "GloveClass06.bmd", 2, 2, 30, 70, 0, 0, 0,
         4);
  addDef(512, 10, 12, "Wind Gloves", "GloveClass07.bmd", 2, 2, 30, 80, 0, 0, 0,
         4);
  addDef(513, 10, 13, "Spirit Gloves", "GloveClass08.bmd", 2, 2, 40, 80, 0, 0,
         0, 4);
  addDef(514, 10, 14, "Guardian Gloves", "GloveClass09.bmd", 2, 2, 40, 80, 0, 0,
         0, 4);

  // Category 11: Boots
  addDef(600, 11, 0, "Bronze Boots", "BootMale01.bmd", 2, 2, 80, 20, 0, 0, 0,
         2);
  addDef(601, 11, 1, "Dragon Boots", "BootMale02.bmd", 2, 2, 120, 30, 0, 0, 0,
         2);
  addDef(602, 11, 2, "Pad Boots", "BootClass01.bmd", 2, 2, 20, 0, 0, 0, 0, 1);
  addDef(603, 11, 3, "Legendary Boots", "BootClass02.bmd", 2, 2, 30, 0, 0, 0, 0,
         1);
  addDef(604, 11, 4, "Bone Boots", "BootClass03.bmd", 2, 2, 30, 0, 0, 0, 0, 1);
  addDef(605, 11, 5, "Leather Boots", "BootMale06.bmd", 2, 2, 80, 0, 0, 0, 0,
         2);
  addDef(606, 11, 6, "Scale Boots", "BootMale07.bmd", 2, 2, 110, 0, 0, 0, 0, 2);
  addDef(607, 11, 7, "Sphinx Boots", "BootClass04.bmd", 2, 2, 30, 0, 0, 0, 0,
         1);
  addDef(608, 11, 8, "Brass Boots", "BootMale09.bmd", 2, 2, 100, 30, 0, 0, 0,
         2);
  addDef(609, 11, 9, "Plate Boots", "BootMale10.bmd", 2, 2, 130, 0, 0, 0, 0, 2);
  addDef(610, 11, 10, "Vine Boots", "BootClass05.bmd", 2, 2, 30, 60, 0, 0, 0,
         4);
  addDef(611, 11, 11, "Silk Boots", "BootClass06.bmd", 2, 2, 30, 70, 0, 0, 0,
         4);
  addDef(612, 11, 12, "Wind Boots", "BootClass07.bmd", 2, 2, 30, 80, 0, 0, 0,
         4);
  addDef(613, 11, 13, "Spirit Boots", "BootClass08.bmd", 2, 2, 40, 80, 0, 0, 0,
         4);
  addDef(614, 11, 14, "Guardian Boots", "BootClass09.bmd", 2, 2, 40, 80, 0, 0,
         0, 4);

  // Category 12: Wings (IDs 700+)
  addDef(700, 12, 0, "Wings of Elf", "Wings01.bmd", 2, 3, 0, 0, 0, 0, 100, 4);
  addDef(701, 12, 1, "Wings of Heaven", "Wings02.bmd", 3, 5, 0, 0, 0, 0, 100,
         1);
  addDef(702, 12, 2, "Wings of Satan", "Wings03.bmd", 2, 5, 0, 0, 0, 0, 100, 2);
  addDef(703, 12, 3, "Wings of Spirits", "Wings04.bmd", 3, 5, 0, 0, 0, 0, 150,
         4);
  addDef(704, 12, 4, "Wings of Soul", "Wings05.bmd", 3, 5, 0, 0, 0, 0, 150, 1);
  addDef(705, 12, 5, "Wings of Dragon", "Wings06.bmd", 3, 3, 0, 0, 0, 0, 150,
         2);
  addDef(706, 12, 6, "Wings of Darkness", "Wings07.bmd", 3, 4, 0, 0, 0, 0, 150,
         8);

  // Category 12: Orbs (IDs 750+)
  addDef(757, 12, 7, "Orb of Twisting Slash", "Orb01.bmd", 1, 1, 0, 0, 0, 0, 47,
         2);
  addDef(758, 12, 8, "Orb of Healing", "Orb02.bmd", 1, 1, 0, 0, 0, 100, 8, 4);
  addDef(759, 12, 9, "Orb of Greater Defense", "Orb03.bmd", 1, 1, 0, 0, 0, 100,
         13, 4);
  addDef(760, 12, 10, "Orb of Greater Damage", "Orb04.bmd", 1, 1, 0, 0, 0, 100,
         18, 4);
  addDef(761, 12, 11, "Orb of Summoning", "Orb05.bmd", 1, 1, 0, 0, 0, 0, 3, 4);
  addDef(762, 12, 12, "Orb of Rageful Blow", "Orb06.bmd", 1, 1, 170, 0, 0, 0,
         78, 2);
  addDef(763, 12, 13, "Orb of Impale", "Orb07.bmd", 1, 1, 28, 0, 0, 0, 20, 2);
  addDef(764, 12, 14, "Orb of Greater Fortitude", "Orb08.bmd", 1, 1, 120, 0, 0,
         0, 60, 2);
  addDef(766, 12, 16, "Orb of Fire Slash", "Orb09.bmd", 1, 1, 320, 0, 0, 0, 60,
         8);
  addDef(767, 12, 17, "Orb of Penetration", "Orb10.bmd", 1, 1, 130, 0, 0, 0, 64,
         4);
  addDef(768, 12, 18, "Orb of Ice Arrow", "Orb11.bmd", 1, 1, 0, 258, 0, 0, 81,
         4);
  addDef(769, 12, 19, "Orb of Death Stab", "Orb12.bmd", 1, 1, 160, 0, 0, 0, 72,
         2);

  // Category 12 (Jewels mix) & Category 13 (Jewelry/Pets) (IDs 800+)
  addDef(815, 12, 15, "Jewel of Chaos", "Jewel01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(800, 13, 0, "Guardian Angel", "Pet01.bmd", 1, 1, 0, 0, 0, 0, 23, 15);
  addDef(801, 13, 1, "Imp", "Pet02.bmd", 1, 1, 0, 0, 0, 0, 28, 15);
  addDef(802, 13, 2, "Horn of Uniria", "Pet03.bmd", 1, 1, 0, 0, 0, 0, 25, 15);
  addDef(803, 13, 3, "Horn of Dinorant", "Pet04.bmd", 1, 1, 0, 0, 0, 0, 110,
         15);
  addDef(808, 13, 8, "Ring of Ice", "Ring01.bmd", 1, 1, 0, 0, 0, 0, 20, 15);
  addDef(809, 13, 9, "Ring of Poison", "Ring02.bmd", 1, 1, 0, 0, 0, 0, 17, 15);
  addDef(810, 13, 10, "Transformation Ring", "Ring03.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(812, 13, 12, "Pendant of Lighting", "Pendant01.bmd", 1, 1, 0, 0, 0, 0,
         21, 15);
  addDef(813, 13, 13, "Pendant of Fire", "Pendant02.bmd", 1, 1, 0, 0, 0, 0, 13,
         15);

  // Category 14: Consumables (IDs 850+)
  addDef(850, 14, 0, "Apple", "Apple.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(851, 14, 1, "Small HP Potion", "Potion01.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(852, 14, 2, "Medium HP Potion", "Potion02.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(853, 14, 3, "Large HP Potion", "Potion03.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(854, 14, 4, "Small Mana Potion", "Potion04.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(855, 14, 5, "Medium Mana Potion", "Potion05.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(856, 14, 6, "Large Mana Potion", "Potion06.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(858, 14, 8, "Antidote", "Potion08.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(859, 14, 9, "Ale", "Potion09.bmd", 1, 2, 0, 0, 0, 0, 0, 15);
  addDef(860, 14, 10, "Town Portal", "ScrollTw.bmd", 1, 2, 0, 0, 0, 0, 0, 15);
  addDef(863, 14, 13, "Jewel of Bless", "Jewel01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(864, 14, 14, "Jewel of Soul", "Jewel02.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(866, 14, 16, "Jewel of Life", "Jewel03.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(872, 14, 22, "Jewel of Creation", "Jewel04.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);

  // Category 15: Scrolls (IDs 900+)
  addDef(900, 15, 0, "Scroll of Poison", "Scroll01.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(901, 15, 1, "Scroll of Meteorite", "Scroll02.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(902, 15, 2, "Scroll of Lightning", "Scroll03.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(903, 15, 3, "Scroll of Fire Ball", "Scroll04.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(904, 15, 4, "Scroll of Flame", "Scroll05.bmd", 1, 2, 0, 0, 0, 0, 0, 1);
  addDef(905, 15, 5, "Scroll of Teleport", "Scroll06.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(906, 15, 6, "Scroll of Ice", "Scroll07.bmd", 1, 2, 0, 0, 0, 0, 0, 1);
  addDef(907, 15, 7, "Scroll of Twister", "Scroll08.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(908, 15, 8, "Scroll of Evil Spirit", "Scroll09.bmd", 1, 2, 0, 0, 0, 0,
         0, 1);
  addDef(909, 15, 9, "Scroll of Hellfire", "Scroll10.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(910, 15, 10, "Scroll of Power Wave", "Scroll11.bmd", 1, 2, 0, 0, 0, 0,
         0, 1);
  addDef(911, 15, 11, "Scroll of Aqua Beam", "Scroll12.bmd", 1, 2, 0, 0, 0, 0,
         0, 1);
  addDef(912, 15, 12, "Scroll of Cometfall", "Scroll13.bmd", 1, 2, 0, 0, 0, 0,
         0, 1);
  addDef(913, 15, 13, "Scroll of Inferno", "Scroll14.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
}

static const char *GetItemNameByDef(int16_t defIndex) {
  auto it = g_itemDefs.find(defIndex);
  if (it != g_itemDefs.end())
    return it->second.name.c_str();
  return "Item";
}

// Panel layout constants (virtual coords 1280x720)
static constexpr float PANEL_W = 290.0f;
static constexpr float PANEL_H = 560.0f;
static constexpr float PANEL_Y = 20.0f;
static constexpr float PANEL_X_RIGHT = 970.0f;

static float GetCharInfoPanelX() { return PANEL_X_RIGHT; }
static float GetInventoryPanelX() {
  return g_showCharInfo ? PANEL_X_RIGHT - PANEL_W - 10.0f : PANEL_X_RIGHT;
}

// Check if virtual point is inside a panel
static bool IsPointInPanel(float vx, float vy, float panelX) {
  return vx >= panelX && vx < panelX + PANEL_W && vy >= PANEL_Y &&
         vy < PANEL_Y + PANEL_H;
}

// Helper: draw centered text with shadow
static void DrawPanelText(ImDrawList *dl, const UICoords &c, float vx, float vy,
                          const char *text, ImU32 color,
                          ImFont *font = nullptr) {
  float sx = c.ToScreenX(vx), sy = c.ToScreenY(vy);
  if (font)
    dl->AddText(font, font->LegacySize, ImVec2(sx + 1, sy + 1),
                IM_COL32(0, 0, 0, 180), text);
  dl->AddText(ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 180), text);
  if (font)
    dl->AddText(font, font->LegacySize, ImVec2(sx, sy), color, text);
  else
    dl->AddText(ImVec2(sx, sy), color, text);
}

// Helper: draw right-aligned text
static void DrawPanelTextRight(ImDrawList *dl, const UICoords &c, float vx,
                               float vy, float width, const char *text,
                               ImU32 color) {
  ImVec2 sz = ImGui::CalcTextSize(text);
  float sx = c.ToScreenX(vx + width) - sz.x;
  float sy = c.ToScreenY(vy);
  dl->AddText(ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 180), text);
  dl->AddText(ImVec2(sx, sy), color, text);
}

// Helper: draw centered text horizontally
static void DrawPanelTextCentered(ImDrawList *dl, const UICoords &c, float vx,
                                  float vy, float width, const char *text,
                                  ImU32 color) {
  ImVec2 sz = ImGui::CalcTextSize(text);
  float sx = c.ToScreenX(vx + width * 0.5f) - sz.x * 0.5f;
  float sy = c.ToScreenY(vy);
  dl->AddText(ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 180), text);
  dl->AddText(ImVec2(sx, sy), color, text);
}

static void RenderCharInfoPanel(ImDrawList *dl, const UICoords &c) {
  float px = GetCharInfoPanelX(), py = PANEL_Y;
  float pw = PANEL_W, ph = PANEL_H;

  // Colors
  const ImU32 colBg = IM_COL32(15, 15, 25, 235);
  const ImU32 colBorder = IM_COL32(60, 65, 90, 200);
  const ImU32 colTitle = IM_COL32(255, 210, 80, 255);
  const ImU32 colHeader = IM_COL32(200, 180, 120, 255);
  const ImU32 colLabel = IM_COL32(170, 170, 190, 255);
  const ImU32 colValue = IM_COL32(255, 255, 255, 255);
  const ImU32 colGreen = IM_COL32(100, 255, 100, 255);
  const ImU32 colBtnN = IM_COL32(40, 80, 40, 200);
  const ImU32 colBtnH = IM_COL32(60, 120, 60, 230);
  const ImU32 colClose = IM_COL32(200, 60, 60, 200);
  const ImU32 colCloseH = IM_COL32(255, 80, 80, 230);
  const ImU32 colBar = IM_COL32(30, 30, 45, 200);

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
  bool hoverClose =
      mp.x >= cMin.x && mp.x < cMax.x && mp.y >= cMin.y && mp.y < cMax.y;
  dl->AddRectFilled(cMin, cMax, hoverClose ? colCloseH : colClose, 3.0f);
  DrawPanelTextCentered(dl, c, closeX, closeY + 1, 22, "X", colValue);

  // Title
  DrawPanelTextCentered(dl, c, px, py + 10, pw, "Character Info", colTitle);

  // Separator
  dl->AddLine(ImVec2(c.ToScreenX(px + 10), c.ToScreenY(py + 32)),
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(py + 32)),
              colBorder);

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
  if (nextXp > prevXp)
    xpFrac = (float)(curXp - prevXp) / (float)(nextXp - prevXp);
  xpFrac = std::clamp(xpFrac, 0.0f, 1.0f);

  float barX = px + 15, barY = py + 100, barW = pw - 30, barH = 14;
  dl->AddRectFilled(ImVec2(c.ToScreenX(barX), c.ToScreenY(barY)),
                    ImVec2(c.ToScreenX(barX + barW), c.ToScreenY(barY + barH)),
                    colBar, 3.0f);
  if (xpFrac > 0.0f)
    dl->AddRectFilled(ImVec2(c.ToScreenX(barX + 1), c.ToScreenY(barY + 1)),
                      ImVec2(c.ToScreenX(barX + 1 + (barW - 2) * xpFrac),
                             c.ToScreenY(barY + barH - 1)),
                      IM_COL32(40, 180, 80, 255), 2.0f);
  snprintf(buf, sizeof(buf), "EXP %.1f%%", xpFrac * 100.0f);
  DrawPanelTextCentered(dl, c, barX, barY, barW, buf, colValue);

  // Separator
  dl->AddLine(ImVec2(c.ToScreenX(px + 10), c.ToScreenY(py + 122)),
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(py + 122)),
              colBorder);

  // Stats section
  DrawPanelText(dl, c, px + 15, py + 128, "Stats", colHeader);

  const char *statNames[] = {"Strength", "Dexterity", "Vitality", "Energy"};
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
      bool hoverBtn =
          mp.x >= bMin.x && mp.x < bMax.x && mp.y >= bMin.y && mp.y < bMax.y;
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
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(py + 302)),
              colBorder);

  // Derived stats
  DrawPanelText(dl, c, px + 15, py + 308, "Combat", colHeader);

  // Compute derived stats (DK formulas)
  int dmgMin = g_serverStr / 8 + g_hero.GetWeaponBonusMin();
  int dmgMax = g_serverStr / 4 + g_hero.GetWeaponBonusMax();
  int defense = g_serverDex / 3 + g_hero.GetDefenseBonus();
  int atkRate = g_serverLevel * 5 + g_serverDex * 3 / 2 + g_serverStr / 4;
  int defRate = g_serverDex / 3;

  struct {
    const char *label;
    int val;
    char fmt[32];
  } derived[] = {
      {"Damage", 0, ""},
      {"Defense", defense, ""},
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
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(py + 422)),
              colBorder);

  // HP bar
  float hpFrac = g_serverMaxHP > 0 ? (float)g_serverHP / g_serverMaxHP : 0.0f;
  hpFrac = std::clamp(hpFrac, 0.0f, 1.0f);
  float hpBarX = px + 15, hpBarY = py + 432, hpBarW = pw - 30, hpBarH = 16;
  dl->AddRectFilled(
      ImVec2(c.ToScreenX(hpBarX), c.ToScreenY(hpBarY)),
      ImVec2(c.ToScreenX(hpBarX + hpBarW), c.ToScreenY(hpBarY + hpBarH)),
      colBar, 3.0f);
  if (hpFrac > 0)
    dl->AddRectFilled(ImVec2(c.ToScreenX(hpBarX + 1), c.ToScreenY(hpBarY + 1)),
                      ImVec2(c.ToScreenX(hpBarX + 1 + (hpBarW - 2) * hpFrac),
                             c.ToScreenY(hpBarY + hpBarH - 1)),
                      IM_COL32(180, 30, 30, 255), 2.0f);
  snprintf(buf, sizeof(buf), "HP %d / %d", g_serverHP, g_serverMaxHP);
  DrawPanelTextCentered(dl, c, hpBarX, hpBarY, hpBarW, buf, colValue);

  // MP bar
  float mpBarY = py + 454;
  dl->AddRectFilled(
      ImVec2(c.ToScreenX(hpBarX), c.ToScreenY(mpBarY)),
      ImVec2(c.ToScreenX(hpBarX + hpBarW), c.ToScreenY(mpBarY + hpBarH)),
      colBar, 3.0f);
  dl->AddRectFilled(ImVec2(c.ToScreenX(hpBarX + 1), c.ToScreenY(mpBarY + 1)),
                    ImVec2(c.ToScreenX(hpBarX + 1 + (hpBarW - 2)),
                           c.ToScreenY(mpBarY + hpBarH - 1)),
                    IM_COL32(40, 80, 200, 255), 2.0f);
  snprintf(buf, sizeof(buf), "MP %d / %d", g_serverMP, g_serverMaxMP);
  DrawPanelTextCentered(dl, c, hpBarX, mpBarY, hpBarW, buf, colValue);
}

static void RenderInventoryPanel(ImDrawList *dl, const UICoords &c) {
  float px = GetInventoryPanelX(), py = PANEL_Y;
  float pw = PANEL_W, ph = PANEL_H;

  const ImU32 colBg = IM_COL32(15, 15, 25, 235);
  const ImU32 colBorder = IM_COL32(60, 65, 90, 200);
  const ImU32 colTitle = IM_COL32(255, 210, 80, 255);
  const ImU32 colHeader = IM_COL32(200, 180, 120, 255);
  const ImU32 colLabel = IM_COL32(170, 170, 190, 255);
  const ImU32 colValue = IM_COL32(255, 255, 255, 255);
  const ImU32 colSlotBg = IM_COL32(25, 25, 40, 220);
  const ImU32 colSlotBr = IM_COL32(50, 50, 70, 180);
  const ImU32 colEquip = IM_COL32(120, 200, 255, 255);
  const ImU32 colClose = IM_COL32(200, 60, 60, 200);
  const ImU32 colCloseH = IM_COL32(255, 80, 80, 230);
  const ImU32 colGold = IM_COL32(255, 215, 0, 255);
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
  bool hoverClose =
      mp.x >= cMin.x && mp.x < cMax.x && mp.y >= cMin.y && mp.y < cMax.y;
  dl->AddRectFilled(cMin, cMax, hoverClose ? colCloseH : colClose, 3.0f);
  DrawPanelTextCentered(dl, c, closeX, closeY + 1, 22, "X", colValue);

  // Title
  DrawPanelTextCentered(dl, c, px, py + 10, pw, "Inventory", colTitle);
  dl->AddLine(ImVec2(c.ToScreenX(px + 10), c.ToScreenY(py + 32)),
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(py + 32)),
              colBorder);

  // Equipment section header
  DrawPanelText(dl, c, px + 15, py + 36, "Equipment", colHeader);

  // Equipment layout: body-shaped arrangement
  //           Helm
  //    R.Hand Armor L.Hand
  //    Gloves Pants Boots
  struct EquipPos {
    int slot;
    float x;
    float y;
    float w;
    float h;
  };
  EquipPos equipLayout[] = {
      {2, px + 118, py + 56, 54, 42},  // Helm - top center
      {0, px + 52, py + 102, 54, 54},  // R.Hand - left
      {3, px + 118, py + 102, 54, 54}, // Armor - center
      {1, px + 184, py + 102, 54, 54}, // L.Hand - right
      {5, px + 52, py + 162, 54, 42},  // Gloves - bottom left
      {4, px + 118, py + 162, 54, 42}, // Pants - bottom center
      {6, px + 184, py + 162, 54, 42}, // Boots - bottom right
  };

  char buf[64];
  for (auto &ep : equipLayout) {
    ImVec2 sMin(c.ToScreenX(ep.x), c.ToScreenY(ep.y));
    ImVec2 sMax(c.ToScreenX(ep.x + ep.w), c.ToScreenY(ep.y + ep.h));

    // Highlight if drag hovering over this slot
    bool hoverSlot =
        mp.x >= sMin.x && mp.x < sMax.x && mp.y >= sMin.y && mp.y < sMax.y;

    dl->AddRectFilled(sMin, sMax, colSlotBg, 3.0f);

    // Render 3D item if valid
    bool hasItem = false;
    std::string modelName;

    if (g_equipSlots[ep.slot].equipped) {
      hasItem = true;
      modelName = g_equipSlots[ep.slot].modelFile;
    }

    if (hasItem && !modelName.empty()) {
      // Queue render for AFTER ImGui
      int winH = (int)ImGui::GetIO().DisplaySize.y;
      int glX = (int)sMin.x;
      int glY = winH - (int)sMax.y; // Convert top-y to bottom-y
      int glW = (int)(sMax.x - sMin.x);
      int glH = (int)(sMax.y - sMin.y);

      g_renderQueue.push_back({modelName, glX, glY, glW, glH});
    }

    dl->AddRect(sMin, sMax, hoverSlot && g_isDragging ? colDragHi : colSlotBr,
                3.0f);

    if (g_equipSlots[ep.slot].equipped) {
      // Show abbreviated item name
      const char *catNames[] = {"Sword", "Axe",   "Mace",  "Spear",
                                "Bow",   "Staff", "Shield"};
      uint8_t cat = g_equipSlots[ep.slot].category;
      const char *name = (cat < 7) ? catNames[cat] : "Item";
      if (g_equipSlots[ep.slot].itemLevel > 0) {
        snprintf(buf, sizeof(buf), "%s+%d", name,
                 g_equipSlots[ep.slot].itemLevel);
      } else {
        snprintf(buf, sizeof(buf), "%s", name);
      }
      // Centered in slot
      ImVec2 sz = ImGui::CalcTextSize(buf);
      float tx = (sMin.x + sMax.x) * 0.5f - sz.x * 0.5f;
      float ty = (sMin.y + sMax.y) * 0.5f - sz.y * 0.5f;
      dl->AddText(ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 180), buf);
      dl->AddText(ImVec2(tx, ty), colEquip, buf);
    } else {
      // Show slot name dimmed
      const char *slotName = GetEquipSlotName(ep.slot);
      ImVec2 sz = ImGui::CalcTextSize(slotName);
      float tx = (sMin.x + sMax.x) * 0.5f - sz.x * 0.5f;
      float ty = (sMin.y + sMax.y) * 0.5f - sz.y * 0.5f;
      dl->AddText(ImVec2(tx, ty), IM_COL32(80, 80, 100, 150), slotName);
    }
  }

  // Separator
  dl->AddLine(ImVec2(c.ToScreenX(px + 10), c.ToScreenY(py + 210)),
              ImVec2(c.ToScreenX(px + pw - 10), c.ToScreenY(py + 210)),
              colBorder);

  // Bag grid header
  DrawPanelText(dl, c, px + 15, py + 214, "Bag", colHeader);

  // 8x8 grid of items
  float gridX = px + 15, gridY = py + 234;
  float cellW = 32.0f, cellH = 32.0f, gap = 1.0f;

  bool processed[INVENTORY_SLOTS] = {false};
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int slot = row * 8 + col;
      if (processed[slot])
        continue;

      float cx = gridX + col * (cellW + gap);
      float cy = gridY + row * (cellH + gap);

      ImVec2 sMin(c.ToScreenX(cx), c.ToScreenY(cy));
      ImVec2 sMax(c.ToScreenX(cx + cellW), c.ToScreenY(cy + cellH));

      bool hoverCell =
          mp.x >= sMin.x && mp.x < sMax.x && mp.y >= sMin.y && mp.y < sMax.y;

      // Check if this cell belongs to the item currently being dragged
      // (handles multi-slot items that span multiple grid cells)
      bool isBeingDragged = false;
      if (g_isDragging && g_dragFromSlot >= 0) {
        int pRow = g_dragFromSlot / 8;
        int pCol = g_dragFromSlot % 8;
        auto dit = g_itemDefs.find(g_dragDefIndex);
        if (dit != g_itemDefs.end()) {
          int dw = dit->second.width;
          int dh = dit->second.height;
          if (row >= pRow && row < pRow + dh && col >= pCol &&
              col < pCol + dw) {
            isBeingDragged = true;
          }
        } else {
          isBeingDragged = (g_dragFromSlot == slot);
        }
      }

      dl->AddRectFilled(sMin, sMax,
                        isBeingDragged ? IM_COL32(40, 40, 20, 200) : colSlotBg,
                        2.0f);
      dl->AddRect(sMin, sMax,
                  hoverCell ? IM_COL32(100, 100, 140, 200) : colSlotBr, 2.0f);

      if (g_inventory[slot].occupied && !isBeingDragged) {
        auto it = g_itemDefs.find(g_inventory[slot].defIndex);
        if (it != g_itemDefs.end()) {
          const auto &def = it->second;
          // Mark all occupied slots as processed
          for (int hh = 0; hh < def.height; hh++) {
            for (int ww = 0; ww < def.width; ww++) {
              int s = (row + hh) * 8 + (col + ww);
              if (s < INVENTORY_SLOTS)
                processed[s] = true;
            }
          }

          // Draw item over its spanned area
          ImVec2 iMin = sMin;
          ImVec2 iMax(c.ToScreenX(cx + def.width * (cellW + gap) - gap),
                      c.ToScreenY(cy + def.height * (cellH + gap) - gap));

          // Draw background for multi-slot if hovered
          bool hoverItem = mp.x >= iMin.x && mp.x < iMax.x && mp.y >= iMin.y &&
                           mp.y < iMax.y;
          if (hoverItem) {
            dl->AddRectFilled(iMin, iMax, IM_COL32(200, 200, 255, 40), 2.0f);
          }

          // Queue 3D render for bag item
          if (!def.modelFile.empty()) {
            int winH = (int)ImGui::GetIO().DisplaySize.y;
            int glX = (int)iMin.x;
            int glY = winH - (int)iMax.y;
            int glW = (int)(iMax.x - iMin.x);
            int glH = (int)(iMax.y - iMin.y);
            g_renderQueue.push_back({def.modelFile, glX, glY, glW, glH});
          }

          // Item name (abbreviated or full depending on size)
          const char *name = def.name.c_str();
          char abbr[32];
          if (def.width < 2)
            snprintf(abbr, sizeof(abbr), "%.4s", name);
          else
            snprintf(abbr, sizeof(abbr), "%s", name);

          if (g_inventory[slot].itemLevel > 0) {
            snprintf(buf, sizeof(buf), "%s+%d", abbr,
                     g_inventory[slot].itemLevel);
          } else {
            snprintf(buf, sizeof(buf), "%s", abbr);
          }
          ImVec2 sz = ImGui::CalcTextSize(buf);
          float tx = (iMin.x + iMax.x) * 0.5f - sz.x * 0.5f;
          float ty = (iMin.y + iMax.y) * 0.5f - sz.y * 0.5f;
          dl->AddText(ImVec2(tx, ty), colValue, buf);
        } else {
          processed[slot] = true; // Unknown item
        }
      }
    }
  }
  // Drop-target preview: highlight the item's footprint on the grid
  if (g_isDragging) {
    auto dit = g_itemDefs.find(g_dragDefIndex);
    if (dit != g_itemDefs.end()) {
      int iw = dit->second.width;
      int ih = dit->second.height;
      // Check if mouse is over the bag grid
      if (mp.x >= c.ToScreenX(gridX) &&
          mp.x < c.ToScreenX(gridX + 8 * (cellW + gap)) &&
          mp.y >= c.ToScreenY(gridY) &&
          mp.y < c.ToScreenY(gridY + 8 * (cellH + gap))) {
        // Compute which cell the mouse is over
        float localX = (mp.x - c.ToScreenX(gridX)) /
                       (c.ToScreenX(gridX + cellW + gap) - c.ToScreenX(gridX));
        float localY = (mp.y - c.ToScreenY(gridY)) /
                       (c.ToScreenY(gridY + cellH + gap) - c.ToScreenY(gridY));
        int hCol = (int)localX;
        int hRow = (int)localY;
        if (hCol >= 0 && hCol < 8 && hRow >= 0 && hRow < 8) {
          bool fits = (hCol + iw <= 8 && hRow + ih <= 8);
          if (fits) {
            // Check occupancy (ignoring the item being dragged)
            for (int rr = 0; rr < ih && fits; rr++) {
              for (int cc = 0; cc < iw && fits; cc++) {
                int s = (hRow + rr) * 8 + (hCol + cc);
                if (g_inventory[s].occupied) {
                  // If dragging from bag, ignore source cells
                  if (g_dragFromSlot >= 0) {
                    int pRow = g_dragFromSlot / 8;
                    int pCol = g_dragFromSlot % 8;
                    if (hRow + rr >= pRow && hRow + rr < pRow + ih &&
                        hCol + cc >= pCol && hCol + cc < pCol + iw)
                      continue; // Source cell, ignore
                  }
                  fits = false;
                }
              }
            }
          }
          // Draw the preview outline
          ImU32 previewCol =
              fits ? IM_COL32(50, 200, 50, 160) : IM_COL32(200, 50, 50, 160);
          float ox = gridX + hCol * (cellW + gap);
          float oy = gridY + hRow * (cellH + gap);
          float ow = iw * (cellW + gap) - gap;
          float oh = ih * (cellH + gap) - gap;
          ImVec2 pMin(c.ToScreenX(ox), c.ToScreenY(oy));
          ImVec2 pMax(c.ToScreenX(ox + ow), c.ToScreenY(oy + oh));
          dl->AddRectFilled(pMin, pMax, (previewCol & 0x00FFFFFF) | 0x30000000,
                            2.0f);
          dl->AddRect(pMin, pMax, previewCol, 2.0f, 0, 2.0f);
        }
      }
    }
  }

  if (g_isDragging) {
    auto it = g_itemDefs.find(g_dragDefIndex);
    if (it != g_itemDefs.end()) {
      const auto &def = it->second;
      float dw = def.width * 32.0f;
      float dh = def.height * 32.0f;
      ImVec2 iMin(mp.x - dw * 0.5f, mp.y - dh * 0.5f);
      ImVec2 iMax(iMin.x + dw, iMin.y + dh);

      dl->AddRectFilled(iMin, iMax, IM_COL32(30, 30, 50, 180), 3.0f);
      // Queue 3D render for dragged item
      int winH = (int)ImGui::GetIO().DisplaySize.y;
      g_renderQueue.push_back(
          {def.modelFile, (int)iMin.x, winH - (int)iMax.y, (int)dw, (int)dh});

      if (g_dragItemLevel > 0)
        snprintf(buf, sizeof(buf), "%s +%d", def.name.c_str(), g_dragItemLevel);
      else
        snprintf(buf, sizeof(buf), "%s", def.name.c_str());
      dl->AddText(ImVec2(iMin.x, iMax.y + 2), colEquip, buf);
    }
  }

  // Tooltip on hover (bag items)
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int slot = row * 8 + col;
      if (!g_inventory[slot].occupied || !g_inventory[slot].primary)
        continue;

      auto it = g_itemDefs.find(g_inventory[slot].defIndex);
      if (it == g_itemDefs.end())
        continue;
      const auto &def = it->second;

      float cx = gridX + col * (cellW + gap);
      float cy = gridY + row * (cellH + gap);
      ImVec2 iMin(c.ToScreenX(cx), c.ToScreenY(cy));
      ImVec2 iMax(c.ToScreenX(cx + def.width * (cellW + gap) - gap),
                  c.ToScreenY(cy + def.height * (cellH + gap) - gap));

      if (mp.x >= iMin.x && mp.x < iMax.x && mp.y >= iMin.y && mp.y < iMax.y &&
          !g_isDragging) {
        // Render Tooltip Box
        ImVec2 tPos(mp.x + 15, mp.y + 15);
        float tw = 160.0f;
        float th = 100.0f;

        dl->AddRectFilled(tPos, ImVec2(tPos.x + tw, tPos.y + th),
                          IM_COL32(0, 0, 0, 240), 4.0f);
        dl->AddRect(tPos, ImVec2(tPos.x + tw, tPos.y + th),
                    IM_COL32(150, 150, 255, 180), 4.0f);

        float curY = tPos.y + 8;
        ImU32 nameCol = (g_inventory[slot].itemLevel >= 7)
                            ? IM_COL32(255, 255, 100, 255)
                            : colValue;
        dl->AddText(ImVec2(tPos.x + 10, curY), nameCol, def.name.c_str());
        curY += 20;

        auto addReq = [&](const char *label, int current, int req) {
          char rBuf[64];
          snprintf(rBuf, sizeof(rBuf), "%s: %d", label, req);
          ImU32 col = (current >= req) ? IM_COL32(200, 200, 200, 255)
                                       : IM_COL32(255, 100, 100, 255);
          dl->AddText(ImVec2(tPos.x + 10, curY), col, rBuf);
          curY += 15;
        };

        if (def.levelReq > 0)
          addReq("Level", g_serverLevel, def.levelReq);
        if (def.reqStr > 0)
          addReq("Strength", g_serverStr, def.reqStr);
        if (def.reqDex > 0)
          addReq("Dexterity", g_serverDex, def.reqDex);
        if (def.reqVit > 0)
          addReq("Vitality", g_serverVit, def.reqVit);
        if (def.reqEne > 0)
          addReq("Energy", g_serverEne, def.reqEne);
      }
    }
  }
}

// Equipment slot layout (relative to panel origin)
struct EquipSlotRect {
  int slot;
  float x, y, w, h;
};
static const EquipSlotRect g_equipLayoutRects[] = {
    {2, 118, 56, 54, 42},  {0, 52, 102, 54, 54}, {3, 118, 102, 54, 54},
    {1, 184, 102, 54, 54}, {5, 52, 162, 54, 42}, {4, 118, 162, 54, 42},
    {6, 184, 162, 54, 42},
};

// Handle panel click interactions (returns true if click was consumed)
static bool CanEquipItem(int16_t defIdx) {
  auto it = g_itemDefs.find(defIdx);
  if (it == g_itemDefs.end())
    return false;
  const auto &def = it->second;

  if (g_serverLevel < def.levelReq) {
    std::cout << "[UI] Level requirement not met (" << g_serverLevel << "/"
              << def.levelReq << ")" << std::endl;
    return false;
  }
  if (g_serverStr < def.reqStr) {
    std::cout << "[UI] Strength requirement not met (" << g_serverStr << "/"
              << def.reqStr << ")" << std::endl;
    return false;
  }
  if (g_serverDex < def.reqDex) {
    std::cout << "[UI] Dexterity requirement not met (" << g_serverDex << "/"
              << def.reqDex << ")" << std::endl;
    return false;
  }
  if (g_serverVit < def.reqVit) {
    std::cout << "[UI] Vitality requirement not met (" << g_serverVit << "/"
              << def.reqVit << ")" << std::endl;
    return false;
  }
  if (g_serverEne < def.reqEne) {
    std::cout << "[UI] Energy requirement not met (" << g_serverEne << "/"
              << def.reqEne << ")" << std::endl;
    return false;
  }

  // Class check: Bit 0 = DK (TestDK character is class 0)
  if (!(def.classFlags & (1 << 0))) {
    std::cout << "[UI] This item cannot be equipped by your class!"
              << std::endl;
    return false;
  }

  return true;
}

static bool CheckBagFit(int16_t defIdx, int targetSlot, int ignoreSlot = -1) {
  auto it = g_itemDefs.find(defIdx);
  if (it == g_itemDefs.end())
    return false;
  int w = it->second.width;
  int h = it->second.height;
  int targetRow = targetSlot / 8;
  int targetCol = targetSlot % 8;

  if (targetCol + w > 8 || targetRow + h > 8)
    return false;

  for (int hh = 0; hh < h; hh++) {
    for (int ww = 0; ww < w; ww++) {
      int s = (targetRow + hh) * 8 + (targetCol + ww);
      if (s == ignoreSlot)
        continue;
      if (g_inventory[s].occupied) {
        // If we are overlapping with exactly the same item (if we're moving
        // it), we need to be careful. But we usually clear the old slots before
        // checking fit for a new move.
        return false;
      }
    }
  }
  return true;
}

static bool HandlePanelClick(float vx, float vy) {
  // Character Info panel
  if (g_showCharInfo && IsPointInPanel(vx, vy, GetCharInfoPanelX())) {
    float px = GetCharInfoPanelX(), py = PANEL_Y;

    // Close button
    if (vx >= px + PANEL_W - 28 && vx < px + PANEL_W - 6 && vy >= py + 6 &&
        vy < py + 24) {
      g_showCharInfo = false;
      return true;
    }

    // Stat "+" buttons
    if (g_serverLevelUpPoints > 0) {
      for (int i = 0; i < 4; i++) {
        float btnX = px + PANEL_W - 42, btnY = py + 150 + i * 32;
        // Debug click coordinates
        std::cout << "[UI] Check Stat " << i << ": Click(" << vx << "," << vy
                  << ") vs Btn(" << btnX << "," << btnY << ",26,20)"
                  << std::endl;
        if (vx >= btnX && vx < btnX + 26 && vy >= btnY && vy < btnY + 20) {
          std::cout << "[UI] Clicked Stat Button " << i
                    << " (Pts=" << g_serverLevelUpPoints << ")" << std::endl;
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
    if (vx >= px + PANEL_W - 28 && vx < px + PANEL_W - 6 && vy >= py + 6 &&
        vy < py + 24) {
      g_showInventory = false;
      return true;
    }

    // Equipment slots: start drag
    for (const auto &ep : g_equipLayoutRects) {
      float ex = px + ep.x;
      float ey = py + ep.y;
      if (vx >= ex && vx < ex + ep.w && vy >= ey && vy < ey + ep.h) {
        if (g_equipSlots[ep.slot].equipped) {
          g_dragFromSlot = -1; // Flag for equipping vs inventory
          g_dragFromEquipSlot = ep.slot;

          // Map category/index back to defIndex for display/logic
          g_dragDefIndex = GetDefIndexFromCategory(
              g_equipSlots[ep.slot].category, g_equipSlots[ep.slot].itemIndex);
          if (g_dragDefIndex == -1)
            g_dragDefIndex = 0; // Fallback

          g_dragQuantity = 1;
          g_dragItemLevel = g_equipSlots[ep.slot].itemLevel;
          g_isDragging = true;
        }
        return true;
      }
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
            // Find primary slot if this is a secondary one
            int primarySlot = slot;
            if (!g_inventory[slot].primary) {
              // We need to find the root. Let's search top-left.
              int16_t di = g_inventory[slot].defIndex;
              bool found = false;
              for (int r = 0; r <= row && !found; r++) {
                for (int c = 0; c <= col && !found; c++) {
                  int s = r * 8 + c;
                  if (g_inventory[s].occupied && g_inventory[s].primary &&
                      g_inventory[s].defIndex == di) {
                    // Check if this primary covers our clicked slot
                    auto it = g_itemDefs.find(di);
                    if (it != g_itemDefs.end()) {
                      if (r + it->second.height > row &&
                          c + it->second.width > col) {
                        primarySlot = s;
                        found = true;
                      }
                    }
                  }
                }
              }
            }
            g_dragFromSlot = primarySlot;
            g_dragFromEquipSlot = -1;
            g_dragDefIndex = g_inventory[primarySlot].defIndex;
            g_dragQuantity = g_inventory[primarySlot].quantity;
            g_dragItemLevel = g_inventory[primarySlot].itemLevel;
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
  if (!g_isDragging)
    return;
  g_isDragging = false;

  if (g_showInventory) {
    float px = GetInventoryPanelX(), py = PANEL_Y;

    // 1. Check drop on Equipment slots
    for (const auto &ep : g_equipLayoutRects) {
      float ex = px + ep.x;
      float ey = py + ep.y;
      if (vx >= ex && vx < ex + ep.w && vy >= ey && vy < ey + ep.h) {
        // Dragging FROM Inventory TO Equipment
        if (g_dragFromSlot >= 0) {
          if (!CanEquipItem(g_dragDefIndex)) {
            g_isDragging = false;
            g_dragFromSlot = -1;
            return;
          }

          uint8_t cat, idx;
          GetItemCategoryAndIndex(g_dragDefIndex, cat, idx);

          // Enforce Slot Compatibility
          // Slot 0 (Right Hand): Sword(0), Axe(1), Mace(2), Spear(3)
          // Slot 1 (Left Hand): Shield(6), Bow(4), Staff(5)
          if (ep.slot == 0 && cat == 6) {
            std::cout << "[UI] Cannot equip Shield in Right Hand!" << std::endl;
            g_isDragging = false;
            g_dragFromSlot = -1;
            return;
          }
          if (ep.slot == 1 && cat != 6 && cat > 5) {
            // Allow Bow/Staff/Shield in Left?
            // Block Swords (Cat 0,1,2,3) in Left Hand
            if (cat <= 3) {
              std::cout << "[UI] Cannot equip Weapon in Left Hand!"
                        << std::endl;
              g_isDragging = false;
              g_dragFromSlot = -1;
              return;
            }
          }

          // Prepare logic for swap if equipped
          ClientInventoryItem swapItem = {};
          if (g_equipSlots[ep.slot].equipped) {
            swapItem.defIndex =
                GetDefIndexFromCategory(g_equipSlots[ep.slot].category,
                                        g_equipSlots[ep.slot].itemIndex);
            swapItem.quantity = 1;
            swapItem.itemLevel = g_equipSlots[ep.slot].itemLevel;
            swapItem.occupied = true;
          }

          // Equip the new item
          g_equipSlots[ep.slot].category = cat;
          g_equipSlots[ep.slot].itemIndex = idx;
          g_equipSlots[ep.slot].itemLevel = g_dragItemLevel;
          g_equipSlots[ep.slot].equipped = true;
          g_equipSlots[ep.slot].modelFile = GetDropModelName(g_dragDefIndex);

          // Update Hero Visuals Immediately
          WeaponEquipInfo info;
          info.category = cat;
          info.itemIndex = idx;
          info.itemLevel = g_dragItemLevel;
          info.modelFile = g_equipSlots[ep.slot].modelFile;

          if (ep.slot == 0)
            g_hero.EquipWeapon(info);
          if (ep.slot == 1)
            g_hero.EquipShield(info);

          // Body part equipment (Helm/Armor/Pants/Gloves/Boots)
          int bodyPart = GetBodyPartIndex(cat);
          if (bodyPart >= 0) {
            std::string partModel = GetBodyPartModelFile(cat, idx);
            if (!partModel.empty())
              g_hero.EquipBodyPart(bodyPart, partModel);
          }

          // Send Equip packet
          PMSG_EQUIP_RECV eq{};
          eq.h = MakeC1Header(sizeof(eq), 0x27);
          eq.characterId = 1;
          eq.slot = static_cast<uint8_t>(ep.slot);
          eq.category = cat;
          eq.itemIndex = idx;
          eq.itemLevel = g_dragItemLevel;
          if (g_syncDone) {
            g_net.Send(&eq, sizeof(eq));
          }

          // Handle source slot (Clear)
          ClearBagItem(g_dragFromSlot);
          std::cout << "[UI] Equipped item from Inv " << g_dragFromSlot
                    << " to Equip " << ep.slot << std::endl;
          RecalcEquipmentStats();
        }
        // Dragging FROM Equipment TO Equipment (e.g. swap rings) - TODO if
        // needed
        return;
      }
    }

    // 2. Check drop on Inventory Grid
    float gridX = px + 15, gridY = py + 234;
    float cellW = 32.0f, cellH = 32.0f, gap = 1.0f;

    // Check if mouse is within grid bounds roughly
    if (vx >= gridX && vx < gridX + 8 * (cellW + gap) && vy >= gridY &&
        vy < gridY + 8 * (cellH + gap)) {
      int col = (int)((vx - gridX) / (cellW + gap));
      int row = (int)((vy - gridY) / (cellH + gap));
      if (col >= 0 && col < 8 && row >= 0 && row < 8) {
        int targetSlot = row * 8 + col;

        // Dragging FROM Equipment TO Inventory (Unequip)
        if (g_dragFromEquipSlot >= 0) {
          if (CheckBagFit(g_dragDefIndex, targetSlot)) {
            // Move item to inventory
            SetBagItem(targetSlot, g_dragDefIndex, g_dragQuantity,
                       g_dragItemLevel);

            // Clear equip slot
            g_equipSlots[g_dragFromEquipSlot].equipped = false;
            g_equipSlots[g_dragFromEquipSlot].category = 0xFF;

            // Update Hero Visuals Immediately
            WeaponEquipInfo info;
            if (g_dragFromEquipSlot == 0)
              g_hero.EquipWeapon(info);
            if (g_dragFromEquipSlot == 1)
              g_hero.EquipShield(info);

            if (g_dragFromEquipSlot >= 2 && g_dragFromEquipSlot <= 6) {
              int partIdx = g_dragFromEquipSlot - 2;
              g_hero.EquipBodyPart(partIdx, "");
            }

            // Send Unequip packet
            PMSG_EQUIP_RECV eq{};
            eq.h = MakeC1Header(sizeof(eq), 0x27);
            eq.characterId = 1;
            eq.slot = static_cast<uint8_t>(g_dragFromEquipSlot);
            eq.category = 0xFF;
            if (g_syncDone)
              g_net.Send(&eq, sizeof(eq));

            std::cout << "[UI] Unequipped item to Inv " << targetSlot
                      << std::endl;
            RecalcEquipmentStats();
          } else {
            std::cout << "[UI] Not enough space for unequipped item"
                      << std::endl;
          }
        }
        // Dragging FROM Inventory TO Inventory (Move)
        else if (g_dragFromSlot >= 0 && g_dragFromSlot != targetSlot) {
          // Temporarily clear old area to check fit
          int16_t di = g_dragDefIndex;
          uint8_t dq = g_dragQuantity;
          uint8_t dl = g_dragItemLevel;

          ClearBagItem(g_dragFromSlot);
          if (CheckBagFit(di, targetSlot)) {
            SetBagItem(targetSlot, di, dq, dl);
            // Send move packet to server
            PMSG_INVENTORY_MOVE_RECV mv{};
            mv.h = MakeC1Header(sizeof(mv), 0x39);
            mv.fromSlot = static_cast<uint8_t>(g_dragFromSlot);
            mv.toSlot = static_cast<uint8_t>(targetSlot);
            if (g_syncDone)
              g_net.Send(&mv, sizeof(mv));

            std::cout << "[UI] Moved item from " << g_dragFromSlot << " to "
                      << targetSlot << std::endl;
          } else {
            // Restore old area
            SetBagItem(g_dragFromSlot, di, dq, dl);
            std::cout << "[UI] Cannot move: target area occupied" << std::endl;
          }
        }
      }
    }
  }

  g_dragFromSlot = -1;
  g_dragFromEquipSlot = -1;
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
  InitItemDefinitions();

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
  ImFont *hudFont = nullptr;
  {
    ImFontConfig cfg;
    cfg.SizePixels = 16.0f * contentScale;
    cfg.OversampleH = 3;
    cfg.OversampleV = 3;
    hudFont = io.Fonts->AddFontDefault(&cfg);
    io.Fonts->Build();
    io.FontGlobalScale = 1.0f / contentScale;
  }

  // Initialize modern HUD (centered at 70% scale)
  g_hudCoords.window = window;
  g_hudCoords.SetCenteredScale(0.7f);

  std::string hudAssetPath = "../lab-studio/modern-ui/assets";
  // --- Main Render Loop ---

  // Main Loop logic continues...

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
  g_vfxManager.Init(data_path);
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

  bool connected = false;
  for (int i = 0; i < 5; ++i) {
    if (g_net.Connect("127.0.0.1", 44405)) {
      connected = true;
      break;
    }
    std::cout << "[Net] Retrying connection in 1s..." << std::endl;
    sleep(1);
  }

  if (!connected) {
    std::cerr << "[Net] FATAL: Could not connect to MU Server. Ensure the "
                 "server is running at 127.0.0.1:44405."
              << std::endl;
    return 1;
  }

  serverData.connected = true;

  // Receive initial data burst (welcome + NPCs + monsters + equipment +
  // stats) Give server time to send all initial packets, poll to parse them
  std::cout << "[Net] Connected. Syncing initial state..." << std::endl;
  for (int attempt = 0; attempt < 50; attempt++) {
    g_net.Poll();
    usleep(20000); // 20ms
  }

  if (serverData.npcs.empty() && !autoScreenshot && !autoDiag) {
    // If we didn't get initial sync data (e.g. timeout), it's probably a stale
    // connection
    std::cerr << "[Net] FATAL: Server connected but failed to sync initial "
                 "game state."
              << std::endl;
    return 1;
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

  // Equip weapon + shield + armor from server equipment data (DB-driven)
  for (auto &eq : serverData.equipment) {
    if (eq.slot == 0) {
      g_hero.EquipWeapon(eq.info); // Right hand weapon
    } else if (eq.slot == 1) {
      g_hero.EquipShield(eq.info); // Left hand shield
    }
    // Body part equipment (slot 2=Helm, 3=Armor, 4=Pants, 5=Gloves, 6=Boots)
    int bodyPart = GetBodyPartIndex(eq.info.category);
    if (bodyPart >= 0) {
      std::string partModel =
          GetBodyPartModelFile(eq.info.category, eq.info.itemIndex);
      if (!partModel.empty())
        g_hero.EquipBodyPart(bodyPart, partModel);
    }
    std::cout << "[Equip] Slot " << (int)eq.slot << ": " << eq.info.modelFile
              << " cat=" << (int)eq.info.category << std::endl;
  }
  g_syncDone = true; // Initial sync complete, allow updates
  g_npcManager.SetTerrainLightmap(terrainData.lightmap);
  RecalcEquipmentStats(); // Compute initial weapon/defense bonuses
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
      std::cout << "[Hero] Spawn position non-walkable (attr=0x" << std::hex
                << (int)terrainData.mapping.attributes[gz * S + gx] << std::dec
                << "), searching for walkable tile..." << std::endl;
      // Spiral search from Lorencia town center for nearest walkable tile
      int startGX = 125, startGZ = 135;
      bool found = false;
      for (int radius = 0; radius < 30 && !found; radius++) {
        for (int dy = -radius; dy <= radius && !found; dy++) {
          for (int dx = -radius; dx <= radius && !found; dx++) {
            if (radius > 0 && std::abs(dx) != radius && std::abs(dy) != radius)
              continue;
            int cx = startGX + dx, cz = startGZ + dy;
            if (cx < 1 || cz < 1 || cx >= S - 1 || cz >= S - 1)
              continue;
            uint8_t attr = terrainData.mapping.attributes[cz * S + cx];
            if ((attr & 0x04) == 0 && (attr & 0x08) == 0) {
              float wx = (float)cz * 100.0f;
              float wz = (float)cx * 100.0f;
              std::cout << "[Hero] Found walkable tile at grid (" << cx << ","
                        << cz << ") attr=0x" << std::hex << (int)attr
                        << std::dec << std::endl;
              g_hero.SetPosition(glm::vec3(wx, 0.0f, wz));
              g_hero.SnapToTerrain();
              found = true;
            }
          }
        }
      }
      if (!found) {
        std::cout << "[Hero] WARNING: No walkable tile found nearby"
                  << std::endl;
        g_hero.SetPosition(glm::vec3(13000.0f, 0.0f, 13000.0f));
        g_hero.SnapToTerrain();
      }
    }
  }
  g_camera.SetPosition(g_hero.GetPosition());

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
      int startGX = 125, startGZ = 125;
      glm::vec3 spawnPos(12500.0f, 0.0f, 12500.0f);
      for (int radius = 0; radius < 30; radius++) {
        bool found = false;
        for (int dy = -radius; dy <= radius && !found; dy++) {
          for (int dx = -radius; dx <= radius && !found; dx++) {
            if (radius > 0 && std::abs(dx) != radius && std::abs(dy) != radius)
              continue;
            int cx = startGX + dx, cz = startGZ + dy;
            if (cx < 1 || cz < 1 || cx >= S - 1 || cz >= S - 1)
              continue;
            uint8_t attr = g_terrainDataPtr->mapping.attributes[cz * S + cx];
            if ((attr & 0x04) == 0 && (attr & 0x08) == 0) {
              spawnPos =
                  glm::vec3((float)cz * 100.0f, 0.0f, (float)cx * 100.0f);
              found = true;
            }
          }
        }
        if (found)
          break;
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
        if (!gi.active)
          continue;
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
        float dist = glm::length(
            glm::vec3(heroPos.x - gi.position.x, 0, heroPos.z - gi.position.z));
        if (dist < 120.0f && !g_hero.IsDead()) {
          PMSG_PICKUP_RECV pkt{};
          pkt.h = MakeC1Header(sizeof(pkt), 0x2C);
          pkt.dropIndex = gi.dropIndex;
          g_net.Send(&pkt, sizeof(pkt));
          gi.active = false; // Optimistic remove
        }
        // Despawn after 60s
        if (gi.timer > 60.0f)
          gi.active = false;
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

    // Auto-screenshot/diagnostic camera override
    if ((autoScreenshot || autoDiag) && diagFrame == 60) {
      glm::vec3 hPos = g_hero.GetPosition();
      std::cout << "[Screenshot] Overriding camera to hero at (" << hPos.x
                << ", " << hPos.y << ", " << hPos.z << ") for capture."
                << std::endl;
      g_camera.SetPosition(hPos);
    }

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
    glm::mat4 projection =
        g_camera.GetProjectionMatrix((float)winW, (float)winH);
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
    g_vfxManager.Update(deltaTime);
    g_fireEffect.Render(view, projection);
    g_vfxManager.Render(view, projection);

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
    g_renderQueue.clear();
    ImGui_ImplOpenGL3_NewFrame();

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Simplified ImGui HUD
    {
      ImGuiViewport *vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - 50),
                              ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(vp->Size.x, 50), ImGuiCond_Always);

      ImGuiWindowFlags HUD_FLAGS =
          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
          ImGuiWindowFlags_NoBackground;

      if (ImGui::Begin("SimpleHUD", nullptr, HUD_FLAGS)) {

        // HP
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "HP: %d/%d",
                           g_serverHP, g_serverMaxHP);
        ImGui::SameLine(180);

        // Level
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Level: %d",
                           g_serverLevel);
        ImGui::SameLine(320);

        // XP Bar
        float dummyXP = (float)(g_serverXP % 1000) / 1000.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              ImVec4(0.2f, 0.7f, 0.9f, 1.0f));
        ImGui::ProgressBar(dummyXP, ImVec2(200, 20), "XP");
        ImGui::PopStyleColor();

        ImGui::SameLine(vp->Size.x - 220);

        // Buttons
        if (ImGui::Button("Char (C)", ImVec2(100, 30))) {
          g_showCharInfo = !g_showCharInfo;
        }
        ImGui::SameLine();
        if (ImGui::Button("Inv (I)", ImVec2(100, 30))) {
          g_showInventory = !g_showInventory;
        }
      }
      ImGui::End();

      ImDrawList *dl = ImGui::GetForegroundDrawList();

      // ── Character Info and Inventory panels ──
      if (g_showCharInfo)
        RenderCharInfoPanel(dl, g_hudCoords);
      if (g_showInventory)
        RenderInventoryPanel(dl, g_hudCoords);

      // ── Floating damage numbers (world-space → screen projection) ──
      {
        glm::mat4 vp = projection * view;
        for (auto &d : g_floatingDmg) {
          if (!d.active)
            continue;
          d.timer += deltaTime;
          if (d.timer >= d.maxTime) {
            d.active = false;
            continue;
          }

          // Float upward
          glm::vec3 pos = d.worldPos + glm::vec3(0, d.timer * 60.0f, 0);

          // Project to screen
          glm::vec4 clip = vp * glm::vec4(pos, 1.0f);
          if (clip.w <= 0.0f)
            continue;
          float sx = ((clip.x / clip.w) * 0.5f + 0.5f) * winW;
          float sy = ((1.0f - (clip.y / clip.w)) * 0.5f) * winH;

          // Fade out in last 0.5s
          float alpha = d.timer > 1.0f ? 1.0f - (d.timer - 1.0f) / 0.5f : 1.0f;
          alpha = std::max(0.0f, std::min(1.0f, alpha));

          // Color by type
          ImU32 col;
          const char *text;
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
          if (mi.state == MonsterState::DEAD)
            continue;

          // Project nameplate position (above monster head)
          glm::vec3 namePos = mi.position + glm::vec3(0, mi.height + 15.0f, 0);
          glm::vec4 clip = vp * glm::vec4(namePos, 1.0f);
          if (clip.w <= 0.0f)
            continue;
          float sx = ((clip.x / clip.w) * 0.5f + 0.5f) * winW;
          float sy = ((1.0f - (clip.y / clip.w)) * 0.5f) * winH;

          // Distance culling — don't show names for far-away monsters
          float dist = glm::length(mi.position - camPos);
          if (dist > 2000.0f)
            continue;

          // Fade based on distance
          float alpha =
              dist < 1000.0f ? 1.0f : 1.0f - (dist - 1000.0f) / 1000.0f;
          alpha = std::max(0.0f, std::min(1.0f, alpha));
          if (mi.state == MonsterState::DYING)
            alpha *= 0.5f;

          // Name + level text
          char nameText[64];
          snprintf(nameText, sizeof(nameText), "%s  Lv.%d", mi.name.c_str(),
                   mi.level);
          ImVec2 textSize = hudFont->CalcTextSizeA(14.0f, FLT_MAX, 0, nameText);
          float tx = sx - textSize.x * 0.5f;
          float ty = sy - textSize.y;

          // Name color: white normally, red if attacking hero
          ImU32 nameCol = (mi.state == MonsterState::ATTACKING ||
                           mi.state == MonsterState::CHASING)
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
          dl->AddRectFilled(ImVec2(barX, barY),
                            ImVec2(barX + barW, barY + barH),
                            IM_COL32(0, 0, 0, (int)(alpha * 160)));
          // HP fill (green → yellow → red)
          ImU32 hpCol =
              hpFrac > 0.5f    ? IM_COL32(60, 220, 60, (int)(alpha * 220))
              : hpFrac > 0.25f ? IM_COL32(220, 220, 60, (int)(alpha * 220))
                               : IM_COL32(220, 60, 60, (int)(alpha * 220));
          if (hpFrac > 0.0f)
            dl->AddRectFilled(ImVec2(barX, barY),
                              ImVec2(barX + barW * hpFrac, barY + barH), hpCol);
        }
      }

      // ── Ground item labels (floating above drops) ──
      // ── Ground item labels ──
      {
        glm::mat4 vp = projection * view;
        for (auto &gi : g_groundItems) {
          if (!gi.active)
            continue;

          // Render 3D Model
          const char *modelFile =
              GetDropModelName(gi.defIndex); // Use helper mapping
          if (modelFile) {
            ItemModelManager::RenderItemWorld(modelFile, gi.position, view,
                                              projection);
          } else if (gi.defIndex == -1) {
            // Zen model? "Zen.bmd" presumably or "money.bmd"
            // ItemModelManager::RenderItemWorld("Zen.bmd", gi.position, view,
            // projection);
          }

          // Float label logic (existing)...
          float bob = sinf(gi.timer * 3.0f) * 5.0f;

          glm::vec3 labelPos = gi.position + glm::vec3(0, 30.0f + bob, 0);
          glm::vec4 clip = vp * glm::vec4(labelPos, 1.0f);
          if (clip.w <= 0.0f)
            continue;
          float sx = ((clip.x / clip.w) * 0.5f + 0.5f) * winW;
          float sy = ((1.0f - (clip.y / clip.w)) * 0.5f) * winH;

          float dist = glm::length(gi.position - camPos);
          if (dist > 1500.0f)
            continue;

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
          ImU32 col = gi.defIndex == -1 ? IM_COL32(255, 215, 0, 220)
                                        : IM_COL32(180, 255, 180, 220);
          dl->AddText(hudFont, 13.0f, ImVec2(tx + 1, ty + 1),
                      IM_COL32(0, 0, 0, 160), label);
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

    // Flatten render queue (items on top of UI)
    for (const auto &job : g_renderQueue) {
      ItemModelManager::RenderItemUI(job.modelFile, job.x, job.y, job.w, job.h);
    }

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
  // Cleanup handled by RAII/End
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
