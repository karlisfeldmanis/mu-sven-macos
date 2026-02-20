#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "Camera.hpp"
#include "ClickEffect.hpp"
#include "ClientPacketHandler.hpp"
#include "ClientTypes.hpp"
#include "FireEffect.hpp"
#include "GrassRenderer.hpp"
#include "HeroCharacter.hpp"
#include "MockData.hpp"
#include "MonsterManager.hpp"
#include "NpcManager.hpp"
#include "ServerConnection.hpp"
#include "ObjectRenderer.hpp"
#include "Screenshot.hpp"
#include "Shader.hpp"
#include "Sky.hpp"
#include "Terrain.hpp"
#include "TerrainParser.hpp"
#include "UICoords.hpp"
#include "UITexture.hpp"
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

static const char *GetItemNameByDef(int16_t defIndex);
static void HandlePanelMouseUp(GLFWwindow *window, float vx, float vy);
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
static ServerConnection g_server;

// NPC interaction state
static int g_hoveredNpc = -1;        // Index of NPC under mouse cursor
static int g_hoveredMonster = -1;    // Index of Monster under mouse cursor
static int g_hoveredGroundItem = -1; // Index of Ground Item under mouse cursor
static int g_selectedNpc = -1; // Index of NPC that was clicked (dialog open)

// Client-side item definitions (types in ClientTypes.hpp)
static std::map<int16_t, ClientItemDefinition> g_itemDefs;

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

// Ground item drops (type in ClientTypes.hpp)
static GroundItem g_groundItems[MAX_GROUND_ITEMS] = {};
static const std::string g_dataPath = "Data";

// Helper to get model filename for drop index
// Drop item definitions (Name, Model)
struct DropDef {
  const char *name;
  const char *model;
  int dmgMin;  // Weapon damage min bonus
  int dmgMax;  // Weapon damage max bonus
  int defense; // Armor defense bonus
};

static const DropDef zen = {"Zen", "Gold01.bmd", 0, 0, 0};
// MU 0.97d complete item database (Mapped to Cat * 32 + Idx)
static const DropDef items[] = {
    // Category 0: Swords (0-31)
    [0] = {"Kris", "Sword01.bmd", 6, 11, 0},
    {"Short Sword", "Sword02.bmd", 3, 7, 0},
    {"Rapier", "Sword03.bmd", 9, 13, 0},
    {"Katana", "Sword04.bmd", 12, 18, 0},
    {"Sword of Assassin", "Sword05.bmd", 15, 22, 0},
    {"Blade", "Sword06.bmd", 21, 31, 0},
    {"Gladius", "Sword07.bmd", 18, 26, 0},
    {"Falchion", "Sword08.bmd", 24, 34, 0},
    {"Serpent Sword", "Sword09.bmd", 30, 42, 0},
    {"Salamander", "Sword10.bmd", 36, 51, 0},
    {"Light Sabre", "Sword11.bmd", 42, 57, 0},
    {"Legendary Sword", "Sword12.bmd", 48, 64, 0},
    {"Heliacal Sword", "Sword13.bmd", 56, 72, 0},
    {"Double Blade", "Sword14.bmd", 44, 61, 0},
    {"Lighting Sword", "Sword15.bmd", 52, 68, 0},
    {"Giant Sword", "Sword16.bmd", 64, 82, 0},
    {"Sword of Destruction", "Sword17.bmd", 84, 108, 0},
    {"Dark Breaker", "Sword18.bmd", 96, 124, 0},
    {"Thunder Blade", "Sword19.bmd", 102, 132, 0},
    {"Divine Sword", "Sword20.bmd", 110, 140, 0},

    // Category 1: Axes (32-63)
    [32] = {"Small Axe", "Axe01.bmd", 1, 6, 0},
    {"Hand Axe", "Axe02.bmd", 4, 9, 0},
    {"Double Axe", "Axe03.bmd", 14, 24, 0},
    {"Tomahawk", "Axe04.bmd", 18, 28, 0},
    {"Elven Axe", "Axe05.bmd", 26, 38, 0},
    {"Battle Axe", "Axe06.bmd", 30, 44, 0},
    {"Nikea Axe", "Axe07.bmd", 34, 50, 0},
    {"Larkan Axe", "Axe08.bmd", 46, 67, 0},
    {"Crescent Axe", "Axe09.bmd", 54, 69, 0},

    // Category 2: Maces (64-95)
    [64] = {"Mace", "Mace01.bmd", 7, 13, 0},
    {"Morning Star", "Mace02.bmd", 13, 22, 0},
    {"Flail", "Mace03.bmd", 22, 32, 0},
    {"Great Hammer", "Mace04.bmd", 38, 56, 0},
    {"Crystal Morning Star", "Mace05.bmd", 66, 107, 0},
    {"Crystal Sword", "Mace06.bmd", 72, 120, 0},
    {"Chaos Dragon Axe", "Mace07.bmd", 75, 130, 0},
    {"Elemental Mace", "Mace08.bmd", 62, 80, 0},
    {"Mace of the King", "Mace09.bmd", 40, 51, 0},

    // Category 3: Spears (96-127)
    [96] = {"Light Spear", "Spear01.bmd", 42, 63, 0},
    {"Spear", "Spear02.bmd", 30, 41, 0},
    {"Dragon Lance", "Spear03.bmd", 21, 33, 0},
    {"Giant Trident", "Spear04.bmd", 35, 43, 0},
    {"Serpent Spear", "Spear05.bmd", 58, 80, 0},
    {"Double Poleaxe", "Spear06.bmd", 19, 31, 0},
    {"Halberd", "Spear07.bmd", 25, 35, 0},
    {"Berdysh", "Spear08.bmd", 42, 54, 0},
    {"Great Scythe", "Spear09.bmd", 71, 92, 0},
    {"Bill of Balrog", "Spear10.bmd", 76, 102, 0},
    {"Dragon Spear", "Spear11.bmd", 112, 140, 0},

    // Category 4: Bows (128-159)
    [128] = {"Short Bow", "Bow01.bmd", 3, 5, 0},
    {"Bow", "Bow02.bmd", 9, 13, 0},
    {"Elven Bow", "Bow03.bmd", 17, 24, 0},
    {"Battle Bow", "Bow04.bmd", 28, 37, 0},
    {"Tiger Bow", "Bow05.bmd", 42, 52, 0},
    {"Silver Bow", "Bow06.bmd", 59, 71, 0},
    {"Chaos Nature Bow", "Bow07.bmd", 88, 106, 0},
    [136] = {"Crossbow", "Bow09.bmd", 5, 8, 0}, // C4I8
    {"Golden Crossbow", "Bow10.bmd", 13, 19, 0},
    {"Arquebus", "Bow11.bmd", 22, 30, 0},
    {"Light Crossbow", "Bow12.bmd", 35, 44, 0},
    {"Serpent Crossbow", "Bow13.bmd", 50, 61, 0},
    {"Bluewing Crossbow", "Bow14.bmd", 68, 82, 0},
    {"Aquagold Crossbow", "Bow15.bmd", 78, 92, 0},

    // Category 5: Staffs (160-191)
    [160] = {"Skull Staff", "Staff01.bmd", 6, 11, 0},
    {"Angelic Staff", "Staff02.bmd", 18, 26, 0},
    {"Serpent Staff", "Staff03.bmd", 30, 42, 0},
    {"Thunder Staff", "Staff04.bmd", 42, 57, 0},
    {"Gorgon Staff", "Staff05.bmd", 56, 72, 0},
    {"Legendary Staff", "Staff06.bmd", 73, 98, 0},
    {"Staff of Resurrection", "Staff07.bmd", 88, 106, 0},
    {"Chaos Lightning Staff", "Staff08.bmd", 102, 132, 0},
    {"Staff of Destruction", "Staff09.bmd", 110, 140, 0},

    // Category 6: Shields (192-223)
    [192] = {"Small Shield", "Shield01.bmd", 0, 0, 3},
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

    // Category 7: Helms (224-255)
    [224] = {"Bronze Helm", "HelmMale01.bmd", 0, 0, 8},
    {"Dragon Helm", "HelmMale10.bmd", 0, 0, 48},
    {"Pad Helm", "HelmClass01.bmd", 0, 0, 2},
    {"Legendary Helm", "HelmClass02.bmd", 0, 0, 28},
    {"Bone Helm", "HelmClass03.bmd", 0, 0, 14},
    {"Leather Helm", "HelmMale06.bmd", 0, 0, 4},
    {"Scale Helm", "HelmMale03.bmd", 0, 0, 12},   // Added for variety
    {"Sphinx Helm", "HelmClass04.bmd", 0, 0, 21}, // Added for variety
    {"Brass Helm", "HelmMale07.bmd", 0, 0, 18},   // Added for variety
    {"Plate Helm", "HelmMale08.bmd", 0, 0, 35},   // Added for variety

    // Category 8: Armor (256-287)
    [256] = {"Bronze Armor", "ArmorMale01.bmd", 0, 0, 15},
    {"Dragon Armor", "ArmorMale10.bmd", 0, 0, 65},
    {"Pad Armor", "ArmorClass01.bmd", 0, 0, 5},
    {"Legendary Armor", "ArmorClass02.bmd", 0, 0, 42},
    {"Bone Armor", "ArmorClass03.bmd", 0, 0, 24},
    {"Leather Armor", "ArmorMale06.bmd", 0, 0, 8},
    {"Scale Armor", "ArmorMale03.bmd", 0, 0, 20},
    {"Sphinx Armor", "ArmorClass04.bmd", 0, 0, 32},
    {"Brass Armor", "ArmorMale07.bmd", 0, 0, 28},
    {"Plate Armor", "ArmorMale08.bmd", 0, 0, 50},

    // Category 9: Pants (288-319)
    [288] = {"Bronze Pants", "PantMale01.bmd", 0, 0, 12},
    {"Dragon Pants", "PantMale10.bmd", 0, 0, 55},
    {"Pad Pants", "PantClass01.bmd", 0, 0, 4},
    {"Legendary Pants", "PantClass02.bmd", 0, 0, 35},
    {"Bone Pants", "PantClass03.bmd", 0, 0, 19},
    {"Leather Pants", "PantMale06.bmd", 0, 0, 6},
    {"Scale Pants", "PantMale03.bmd", 0, 0, 16},
    {"Sphinx Pants", "PantClass04.bmd", 0, 0, 27},
    {"Brass Pants", "PantMale07.bmd", 0, 0, 23},
    {"Plate Pants", "PantMale08.bmd", 0, 0, 43},

    // Category 10: Gloves (320-351)
    [320] = {"Bronze Gloves", "GloveMale01.bmd", 0, 0, 6},
    {"Dragon Gloves", "GloveMale10.bmd", 0, 0, 40},
    {"Pad Gloves", "GloveClass01.bmd", 0, 0, 1},
    {"Legendary Gloves", "GloveClass02.bmd", 0, 0, 22},
    {"Bone Gloves", "GloveClass03.bmd", 0, 0, 10},
    {"Leather Gloves", "GloveMale06.bmd", 0, 0, 2},
    {"Scale Gloves", "GloveMale03.bmd", 0, 0, 8},
    {"Sphinx Gloves", "GloveClass04.bmd", 0, 0, 15},
    {"Brass Gloves", "GloveMale07.bmd", 0, 0, 12},
    {"Plate Gloves", "GloveMale08.bmd", 0, 0, 28},

    // Category 11: Boots (352-383)
    [352] = {"Bronze Boots", "BootMale01.bmd", 0, 0, 6},
    {"Dragon Boots", "BootMale10.bmd", 0, 0, 40},
    {"Pad Boots", "BootClass01.bmd", 0, 0, 1},
    {"Legendary Boots", "BootClass02.bmd", 0, 0, 22},
    {"Bone Boots", "BootClass03.bmd", 0, 0, 10},
    {"Leather Boots", "BootMale06.bmd", 0, 0, 2},
    {"Scale Boots", "BootMale03.bmd", 0, 0, 8},
    {"Sphinx Boots", "BootClass04.bmd", 0, 0, 15},
    {"Brass Boots", "BootMale07.bmd", 0, 0, 12},
    {"Plate Boots", "BootMale08.bmd", 0, 0, 28},

    // Category 12: Wings/Orbs (384-415)
    [384] = {"Wings of Elf", "Wing01.bmd", 0, 0, 0},
    {"Wings of Heaven", "Wing02.bmd", 0, 0, 0},
    {"Wings of Satan", "Wing03.bmd", 0, 0, 0},
    {"Wings of Spirit", "Wing04.bmd", 0, 0, 0},
    {"Wings of Soul", "Wing05.bmd", 0, 0, 0},
    {"Wings of Dragon", "Wing06.bmd", 0, 0, 0},
    {"Wings of Darkness", "Wing07.bmd", 0, 0, 0},

    // Category 13: Rings (416-447)
    [416] = {"Ring of Ice", "Ring01.bmd", 0, 0, 0},
    {"Ring of Poison", "Ring02.bmd", 0, 0, 0},
    {"Ring of Fire", "Ring01.bmd", 0, 0, 0},  // Reusing Ring01
    {"Ring of Earth", "Ring02.bmd", 0, 0, 0}, // Reusing Ring02
    {"Ring of Wind", "Ring01.bmd", 0, 0, 0},  // Reusing Ring01
    {"Ring of Magic", "Ring02.bmd", 0, 0, 0}, // Reusing Ring02

    // Category 14: Potions (448-479)
    [448] = {"Apple", "Potion01.bmd", 0, 0, 0},
    {"Small Health Potion", "Potion02.bmd", 0, 0, 0},
    {"Medium Health Potion", "Potion03.bmd", 0, 0, 0},
    {"Large Health Potion", "Potion04.bmd", 0, 0, 0},
    {"Small Mana Potion", "Potion05.bmd", 0, 0, 0},
    {"Medium Mana Potion", "Potion06.bmd", 0, 0, 0},
    {"Large Mana Potion", "Potion07.bmd", 0, 0, 0},

    // Misc Items (Cat 13/14 overlap or special IDs in standard MU, but using
    // our logic)
    // Zen is special index -1
    // Jewels typically Cat 14 or 12 or 13 depending on version.
    // Item.txt says Jewels are Cat 14 (Index 13, 14, 16) or Cat 12.
    // 0.97k Item.txt: Jewel of Bless is 14, 13
    [461] = {"Jewel of Bless", "Jewel01.bmd", 0, 0, 0},
    {"Jewel of Soul", "Jewel02.bmd", 0, 0, 0},
    {"Jewel of Life", "Jewel03.bmd", 0, 0, 0},
    {"Jewel of Chaos", "Jewel04.bmd", 0, 0, 0},
};

static const DropDef *GetDropInfo(int16_t defIndex) {
  if (defIndex == -1)
    return &zen;

  if (defIndex >= 0 && defIndex < (int)(sizeof(items) / sizeof(items[0])))
    return &items[defIndex];

  return nullptr;
}

// Category names for fallback item naming
static const char *kCatNames[] = {
    "Sword",      "Axe",       "Mace",         "Spear",       "Bow",    "Staff",
    "Shield",     "Helm",      "Armor",        "Pants",       "Gloves", "Boots",
    "Wings/Misc", "Accessory", "Jewel/Potion", "Scroll/Skill"};

// Fallback model per category (used when item not in g_itemDefs)
static const char *kCatFallbackModel[] = {
    "Sword01.bmd",      // 0 Swords
    "Axe01.bmd",        // 1 Axes
    "Mace01.bmd",       // 2 Maces
    "Spear01.bmd",      // 3 Spears
    "Bow01.bmd",        // 4 Bows
    "Staff01.bmd",      // 5 Staffs
    "Shield01.bmd",     // 6 Shields
    "HelmClass02.bmd",  // 7 Helms
    "ArmorClass02.bmd", // 8 Armor
    "PantClass02.bmd",  // 9 Pants
    "GloveClass02.bmd", // 10 Gloves
    "BootClass02.bmd",  // 11 Boots
    "Ring01.bmd",       // 12 Rings
    "Pendant01.bmd",    // 13 Pendants
    "Potion01.bmd",     // 14 Potions
    "Scroll01.bmd",     // 15 Scrolls
};

// Thread-local buffer for fallback name (avoids static lifetime issues)
static std::string g_fallbackNameBuf;

static const char *GetDropName(int16_t defIndex) {
  if (defIndex == -1)
    return "Zen";
  auto it = g_itemDefs.find(defIndex);
  if (it != g_itemDefs.end())
    return it->second.name.c_str();
  // Generate fallback: "Bow [15]" from category*32+idx
  int cat = (defIndex >= 0) ? (defIndex / 32) : 0;
  int idx = (defIndex >= 0) ? (defIndex % 32) : 0;
  const char *catName = (cat >= 0 && cat < 16) ? kCatNames[cat] : "Item";
  char buf[32];
  snprintf(buf, sizeof(buf), "%s [%d]", catName, idx);
  g_fallbackNameBuf = buf;
  return g_fallbackNameBuf.c_str();
}

static const char *GetDropModelName(int16_t defIndex) {
  if (defIndex == -1)
    return "Gold01.bmd";
  auto it = g_itemDefs.find(defIndex);
  if (it != g_itemDefs.end())
    return it->second.modelFile.c_str();
  // Return category-appropriate fallback model
  int cat = (defIndex >= 0) ? (defIndex / 32) : 14;
  if (cat >= 0 && cat < 16)
    return kCatFallbackModel[cat];
  return "Potion01.bmd"; // last resort
}

// Map equipment category+index to Player body part BMD filename
// Returns empty string if not a body part (e.g. weapons/potions)
static std::string GetBodyPartModelFile(uint8_t category, uint8_t index) {
  // Category 7=Helm...11=Boot
  const char *prefixes[] = {"Helm", "Armor", "Pant", "Glove", "Boot"};
  int partCat = category - 7; // 0=Helm...4=Boot
  if (partCat < 0 || partCat > 4)
    return "";

  // Class specific prefixes
  // Simplification: We only have Male/Class01/Class02/Class03/Class04 mapped in
  // code We need to map exact filenames. For now, let's assume a simple mapping
  // or use the filename from GetDropInfo? Actually, GetDropInfo already has the
  // model filename! We can just reverse look up? No, that's slow. Or we can
  // rely on standard naming conventions if possible. The current function uses
  // a simple mapping. Let's keep it but ideally use the DropDef.

  // BUT: GetDropInfo stores "Drop Model".
  // Armor/Helm in Drop is usually same BMD as equipped?
  // Drop: ArmorMale01.bmd. Equipped: ArmorMale01.bmd. YES.
  // So we can use GetDropInfo to find the model file!

  int16_t defIndex = (category * 32) + index;
  auto *def = GetDropInfo(defIndex);
  if (def && def->model) {
    return def->model;
  }
  return "";
}

// Map category to body part index (0=Helm, 1=Armor, 2=Pants, 3=Gloves, 4=Boots)
static int GetBodyPartIndex(uint8_t category) {
  int idx = category - 7;
  if (idx >= 0 && idx <= 4)
    return idx;
  return -1;
}

// Minimal mapping from Client DefIndex -> Server Category/Index
// Use standard 32 offset
static void GetItemCategoryAndIndex(int16_t defIndex, uint8_t &cat,
                                    uint8_t &idx) {
  if (defIndex < 0) {
    cat = 0xFF;
    idx = 0;
    return;
  }
  cat = defIndex / 32;
  idx = defIndex % 32;
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
static int g_serverHP = 110, g_serverMaxHP = 110;
static int g_serverMP = 20, g_serverMaxMP = 20;
static int g_serverStr = 28, g_serverDex = 20, g_serverVit = 25,
           g_serverEne = 10;
static int g_serverLevelUpPoints = 0;
static int64_t g_serverXP = 0;
static int g_serverDefense = 0, g_serverAttackSpeed = 0, g_serverMagicSpeed = 0;

// Panel toggle state
static bool g_showCharInfo = false;
static bool g_showInventory = false;

// Quick slot (Q) item
// Quick slot assignments
static int16_t g_quickSlotDefIndex = 850; // Apple by default
static ImVec2 g_quickSlotPos = {0, 0};    // Screen pos of Q slot for overlays
static float g_potionCooldown = 0.0f;     // Potion cooldown timer (seconds)
static constexpr float POTION_COOLDOWN_TIME = 30.0f;

// Client-side inventory (synced from server via 0x36)

static int16_t GetDefIndexFromCategory(uint8_t category, uint8_t index) {
  for (auto const &[id, def] : g_itemDefs) {
    if (def.category == category && def.itemIndex == index) {
      return id;
    }
  }
  return -1;
}

// ClientInventoryItem defined in ClientTypes.hpp
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

static void ConsumeQuickSlotItem() {
  if (g_quickSlotDefIndex == -1)
    return;

  // Cooldown check — prevent potion spam
  if (g_potionCooldown > 0.0f) {
    std::cout << "[QuickSlot] Cooldown active (" << g_potionCooldown
              << "s remaining)" << std::endl;
    return;
  }

  // Search for the first instance of this item in inventory
  int foundSlot = -1;
  for (int i = 0; i < INVENTORY_SLOTS; i++) {
    if (g_inventory[i].occupied && g_inventory[i].primary &&
        g_inventory[i].defIndex == g_quickSlotDefIndex) {
      foundSlot = i;
      break;
    }
  }

  if (foundSlot != -1) {
    // Determine healing amount
    int healAmount = 0;
    auto it = g_itemDefs.find(g_quickSlotDefIndex);
    if (it != g_itemDefs.end()) {
      const auto &def = it->second;
      if (def.category == 14) {
        if (def.itemIndex == 0)
          healAmount = 10; // Apple
        else if (def.itemIndex == 1)
          healAmount = 20; // Small HP
        else if (def.itemIndex == 2)
          healAmount = 50; // Medium HP
        else if (def.itemIndex == 3)
          healAmount = 100; // Large HP
      }
    }

    if (healAmount > 0) {
      // Send use request to server
      g_server.SendItemUse((uint8_t)foundSlot);

      // Start local cooldown for UI feedback
      g_potionCooldown = POTION_COOLDOWN_TIME;

      std::cout << "[QuickSlot] Requested to use "
                << GetItemNameByDef(g_quickSlotDefIndex) << " from slot "
                << foundSlot << std::endl;
    }
  } else {
    std::cout << "[QuickSlot] No " << GetItemNameByDef(g_quickSlotDefIndex)
              << " found in inventory!" << std::endl;
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

  // Defensive: check if entire footprint is within bounds and free
  if (c + w > 8 || r + h > 8)
    return;

  // Pass 1: check occupancy
  for (int hh = 0; hh < h; hh++) {
    for (int ww = 0; ww < w; ww++) {
      int s = (r + hh) * 8 + (c + ww);
      if (s >= INVENTORY_SLOTS || g_inventory[s].occupied)
        return; // Target area is busy or out of bounds
    }
  }

  // Pass 2: mark slots
  for (int hh = 0; hh < h; hh++) {
    for (int ww = 0; ww < w; ww++) {
      int s = (r + hh) * 8 + (c + ww);
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

// Equipment display (type in ClientTypes.hpp)
static ClientEquipSlot g_equipSlots[12] = {};
static GLuint g_slotBackgrounds[12] = {0};
static UITexture g_texInventoryBg;

// UI Static Globals
static constexpr float g_uiPanelScale = 1.2f;
// g_charInfoTab removed (no more tabs)
static ImFont *g_fontDefault = nullptr;
static ImFont *g_fontBold = nullptr;

struct EquipSlotRect {
  int slot;
  float rx, ry, rw, rh;
};
static const EquipSlotRect g_equipLayoutRects[] = {
    {8, 15, 44, 46, 46},    // Pet
    {2, 75, 44, 46, 46},    // Helm
    {7, 120, 44, 61, 46},   // Wings
    {0, 15, 87, 46, 66},    // R.Hand
    {3, 75, 87, 46, 66},    // Armor
    {1, 135, 87, 46, 66},   // L.Hand
    {9, 54, 87, 28, 28},    // Pendant
    {10, 54, 150, 28, 28},  // Ring 1
    {11, 114, 150, 28, 28}, // Ring 2
    {5, 15, 150, 46, 46},   // Gloves
    {4, 75, 150, 46, 46},   // Pants
    {6, 135, 150, 46, 46}   // Boots
};

// Textures removed for simplification

// Map equipment slot index to silhouette texture (Main 5.2
// SetEquipmentSlotInfo)
// getSlotTex removed

// Buttons removed for simplification

// Table borders removed for simplification

// Recalculate weapon/defense bonuses from all equipped items
static void RecalcEquipmentStats() {
  int totalDmgMin = 0, totalDmgMax = 0, totalDef = 0;
  for (int s = 0; s < 12; ++s) {
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
                             const std::string &modelFile,
                             std::vector<MeshBuffers> &outBuffers) {
  MeshBuffers mb;
  mb.isDynamic = false;

  // Resolve texture
  auto texInfo = TextureLoader::ResolveWithInfo(texPath, mesh.TextureName);
  mb.texture = texInfo.textureID;
  mb.hasAlpha = texInfo.hasAlpha;

  // Parse script flags from texture name
  auto flags = TextureLoader::ParseScriptFlags(mesh.TextureName);
  mb.bright = flags.bright;
  mb.hidden = flags.hidden;
  mb.noneBlend = flags.noneBlend;

  // Force additive blending for Wings and specific pets to hide black JPEG
  // backgrounds
  {
    std::string texLower = mesh.TextureName;
    std::transform(texLower.begin(), texLower.end(), texLower.begin(),
                   ::tolower);
    std::string modelLower = modelFile;
    std::transform(modelLower.begin(), modelLower.end(), modelLower.begin(),
                   ::tolower);

    if (texLower.find("wing") != std::string::npos ||
        modelLower.find("wing") != std::string::npos ||
        texLower.find("fairy2") != std::string::npos ||
        texLower.find("satan2") != std::string::npos ||
        texLower.find("unicon01") != std::string::npos ||
        texLower.find("flail00") != std::string::npos) {
      mb.bright = true;
    }
  }

  if (mb.hidden)
    return;

  // Expand vertices per-triangle-corner (matching ObjectRenderer::UploadMesh).
  // BMD stores separate VertexIndex, NormalIndex, TexCoordIndex per triangle
  // corner — we must create a unique vertex for each corner to preserve
  // per-face normals and UVs.
  struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 tex;
  };
  std::vector<Vertex> vertices;
  std::vector<unsigned int> indices;

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    int startIdx = (int)vertices.size();

    // First triangle (0,1,2)
    for (int v = 0; v < 3; ++v) {
      Vertex vert;
      auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
      auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

      int boneIdx = srcVert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        const auto &bm = bones[boneIdx];
        vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                          srcVert.Position);
        vert.normal =
            MuMath::RotateVector((const float(*)[4])bm.data(), srcNorm.Normal);
      } else {
        vert.pos = srcVert.Position;
        vert.normal = srcNorm.Normal;
      }

      vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                           mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
      vertices.push_back(vert);
      indices.push_back(startIdx + v);
    }

    // Second triangle for quads (0,2,3)
    if (steps == 4) {
      int quadIndices[3] = {0, 2, 3};
      for (int v : quadIndices) {
        Vertex vert;
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

        int boneIdx = srcVert.Node;
        if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
          const auto &bm = bones[boneIdx];
          vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                            srcVert.Position);
          vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(),
                                             srcNorm.Normal);
        } else {
          vert.pos = srcVert.Position;
          vert.normal = srcNorm.Normal;
        }

        vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                             mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
        vertices.push_back(vert);
        indices.push_back((int)vertices.size() - 1);
      }
    }
  }

  mb.vertexCount = (int)vertices.size();
  mb.indexCount = (int)indices.size();

  if (mb.indexCount == 0) {
    outBuffers.push_back(mb);
    return;
  }

  // Upload to GPU
  glGenVertexArrays(1, &mb.vao);
  glGenBuffers(1, &mb.vbo);
  glGenBuffers(1, &mb.ebo);

  glBindVertexArray(mb.vao);

  glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mb.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  // Layout: Pos(3) + Norm(3) + UV(2) = 8 floats stride
  GLsizei stride = sizeof(Vertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                        (void *)(sizeof(float) * 3));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                        (void *)(sizeof(float) * 6));

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
      UploadStaticMesh(mesh, texPath, bones, filename, model.meshes);
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
  static void RenderItemUI(const std::string &modelFile, int16_t defIndex,
                           int x, int y, int w, int h, bool hovered = false) {
    LoadedItemModel *model = Get(modelFile);
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
    if (maxDim < 1.0f)
      maxDim = 1.0f;

    // Use Orthographic projection for UI items to fill grid space perfectly
    float aspect = (float)w / (float)h;
    glm::mat4 proj = glm::ortho(-aspect, aspect, -1.0f, 1.0f, -100.0f, 100.0f);

    // Camera looking at origin
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 50.0f), glm::vec3(0, 0, 0),
                                 glm::vec3(0, 1, 0));

    // Model Transformation
    glm::mat4 mod = glm::mat4(1.0f);

    // 1. Orientation to make the item "stand up" vertically in the grid
    if (defIndex != -1) {
      int category = 0;
      auto it = g_itemDefs.find(defIndex);
      if (it != g_itemDefs.end()) {
        category = it->second.category;
      } else {
        category = defIndex / 32;
      }

      // 1. Orientation to make the item "stand up" vertically in the grid
      if (category <= 5) {
        // Weapons and Staffs (0-5): Use smart axis-detection to ensure they are
        // strictly vertical pointing UP.
        if (size.z >= size.x && size.z >= size.y) {
          mod = glm::rotate(mod, glm::radians(-90.0f), glm::vec3(1, 0, 0));
          if (size.x < size.y)
            mod = glm::rotate(mod, glm::radians(90.0f), glm::vec3(0, 1, 0));
        } else if (size.x >= size.y && size.x >= size.z) {
          mod = glm::rotate(mod, glm::radians(90.0f), glm::vec3(0, 0, 1));
          if (size.z < size.y)
            mod = glm::rotate(mod, glm::radians(90.0f), glm::vec3(0, 1, 0));
        } else {
          if (size.x < size.z)
            mod = glm::rotate(mod, glm::radians(90.0f), glm::vec3(0, 1, 0));
        }
      } else {
        // Other items (Shields 6, Armor 7-11, Wings 12, etc):
        // These are typically modeled lying flat. Use standard MU pose (-90 X).
        mod = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                          glm::vec3(1, 0, 0));
      }
    } else {
      // Zen/Default: Use -90 X to make the Zen coins/box stand up
      mod = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                        glm::vec3(1, 0, 0));
    }

    // 2. Consistent 360 spin around the GRID'S vertical axis (Y) on hover
    if (hovered) {
      float spin = (float)glfwGetTime() * 180.0f;
      // Apply spin AFTER orientation so it's always around the screen's Y axis
      mod =
          glm::rotate(glm::mat4(1.0f), glm::radians(spin), glm::vec3(0, 1, 0)) *
          mod;
    }

    // 3. Transformation order: Scale * (Spin * Orientation) * Translation
    // Scale to fit: map maxDim to ~1.8 (leaving small margin in 2.0 range)
    float scale = 1.8f / maxDim;
    mod = glm::scale(glm::mat4(1.0f), glm::vec3(scale)) * mod;

    // Center the model locally before any rotation
    mod = mod * glm::translate(glm::mat4(1.0f), -center);

    shader->setMat4("projection", proj);
    shader->setMat4("view", view);
    shader->setMat4("model", mod);
    // Set ALL lighting uniforms explicitly for UI — don't rely on stale
    // world-pass values
    shader->setVec3("lightPos", glm::vec3(0, 50, 50.0f));
    shader->setVec3("viewPos", glm::vec3(0, 0, 50.0f));
    shader->setVec3("lightColor",
                    glm::vec3(1.0f, 1.0f, 1.0f)); // Pure white light
    shader->setFloat("blendMeshLight", 1.0f);     // No mesh darkening
    shader->setVec3("terrainLight",
                    glm::vec3(1.0f, 1.0f, 1.0f)); // No terrain darkening
    shader->setFloat("luminosity", 1.0f);         // Full brightness
    shader->setInt("numPointLights", 0);          // No point lights in UI
    shader->setBool("useFog", false);             // No fog in UI
    shader->setFloat("objectAlpha", 1.0f);        // Fully opaque

    // Render — disable face culling for double-sided meshes (pet wings etc.)
    glDisable(GL_CULL_FACE);
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
        glDepthMask(GL_FALSE); // Disable depth writes for transparent layers
        if (mb.bright)
          glBlendFunc(GL_ONE, GL_ONE); // Pure additive
        else
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      } else {
        glDisable(GL_BLEND);  // Opaque
        glDepthMask(GL_TRUE); // Enable depth writes
      }

      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      glDepthMask(GL_TRUE); // Restore state after draw
    }
    glEnable(GL_CULL_FACE);
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
                              float scale = 1.0f,
                              glm::vec3 rotation = glm::vec3(0)) {
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

    // Apply resting rotation
    if (rotation.x != 0)
      mod = glm::rotate(mod, glm::radians(rotation.x), glm::vec3(1, 0, 0));
    if (rotation.y != 0)
      mod = glm::rotate(mod, glm::radians(rotation.y), glm::vec3(0, 1, 0));
    if (rotation.z != 0)
      mod = glm::rotate(mod, glm::radians(rotation.z), glm::vec3(0, 0, 1));

    // Spin only if hovering? No, ground items just lie there usually
    // mod = glm::rotate(mod, (float)glfwGetTime() * 2.0f, glm::vec3(0, 1, 0));

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
  int16_t defIndex;
  int x, y, w, h;
  bool hovered;
};
static std::vector<ItemRenderJob> g_renderQueue;

// Deferred tooltip: populated during UI pass, drawn AFTER 3D render queue
// so it always appears on top of 3D items.
struct PendingTooltipLine {
  ImU32 color;
  std::string text;
};
struct PendingTooltip {
  bool active = false;
  ImVec2 pos; // top-left of tooltip box
  float w = 0, h = 0;
  std::vector<PendingTooltipLine> lines;
};
static PendingTooltip g_pendingTooltip;

// Helper: begin a new pending tooltip at mouse position
static void BeginPendingTooltip(float tw, float th) {
  ImVec2 mp = ImGui::GetIO().MousePos;
  ImVec2 tPos(mp.x + 15, mp.y + 15);
  float winW = ImGui::GetIO().DisplaySize.x;
  float winH = ImGui::GetIO().DisplaySize.y;
  if (tPos.x + tw > winW)
    tPos.x = winW - tw - 5;
  if (tPos.y + th > winH)
    tPos.y = winH - th - 5;
  g_pendingTooltip.active = true;
  g_pendingTooltip.pos = tPos;
  g_pendingTooltip.w = tw;
  g_pendingTooltip.h = th;
  g_pendingTooltip.lines.clear();
}
static void AddPendingTooltipLine(ImU32 color, const std::string &text) {
  g_pendingTooltip.lines.push_back({color, text});
}

// Unified tooltip logic for items
static void AddPendingItemTooltip(int16_t defIndex, int itemLevel) {
  auto it = g_itemDefs.find(defIndex);
  const ClientItemDefinition *def = nullptr;
  ClientItemDefinition fallback;

  if (it != g_itemDefs.end()) {
    def = &it->second;
  } else {
    fallback.name = GetDropName(defIndex);
    fallback.category = (uint8_t)(defIndex / 32);
    fallback.width = 1;
    fallback.height = 1;
    def = &fallback;
  }

  // Calculate tooltip height
  float lineH = 18.0f; // Increased for ProggyClean clarity
  float th = 10.0f;    // Padding
  th += lineH;         // name
  const char *catDesc = (def->category < 16) ? kCatNames[def->category] : "";
  if (catDesc[0])
    th += lineH;

  // Hands specification (usually under category)
  if (def->category <= 5 ||
      def->category == 12) // Weapons or Wings maybe not wings, weapons only
    th += lineH;

  if (def->category <= 5 && (def->dmgMin > 0 || def->dmgMax > 0))
    th += lineH;
  // Attack Speed
  if (def->category <= 5 && def->attackSpeed > 0)
    th += lineH;

  if (def->category >= 7 && def->category <= 11 && def->defense > 0)
    th += lineH;

  th += 8; // separator
  if (def->levelReq > 0)
    th += lineH;
  if (def->reqStr > 0)
    th += lineH;
  if (def->reqDex > 0)
    th += lineH;
  if (def->reqVit > 0)
    th += lineH;
  if (def->reqEne > 0)
    th += lineH;

  // Class requirements (e.g. "Dark Wizard  Dark Knight")
  if (def->classFlags > 0 && def->classFlags != 0xFFFFFFFF)
    th += lineH;

  th += 10; // bottom padding

  BeginPendingTooltip(185.0f, th);

  // Name color based on level
  ImU32 nameColor = IM_COL32(255, 255, 255, 255);
  if (itemLevel >= 7)
    nameColor = IM_COL32(255, 215, 0, 255);
  else if (itemLevel >= 4)
    nameColor = IM_COL32(100, 180, 255, 255);

  char nameBuf[64];
  if (itemLevel > 0)
    snprintf(nameBuf, sizeof(nameBuf), "%s +%d", def->name.c_str(), itemLevel);
  else
    snprintf(nameBuf, sizeof(nameBuf), "%s", def->name.c_str());
  AddPendingTooltipLine(nameColor, nameBuf);

  if (catDesc[0])
    AddPendingTooltipLine(IM_COL32(160, 160, 160, 200), catDesc);

  if (def->category <= 5) {
    if (def->twoHanded)
      AddPendingTooltipLine(IM_COL32(200, 200, 200, 255), "Two-Handed Weapon");
    else if (def->category != 4 || def->name == "Arrows" || def->name == "Bolt")
      AddPendingTooltipLine(IM_COL32(200, 200, 200, 255), "One-Handed Weapon");
  }

  if (def->category <= 5 && (def->dmgMin > 0 || def->dmgMax > 0)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Damage: %d~%d", def->dmgMin, def->dmgMax);
    AddPendingTooltipLine(IM_COL32(255, 140, 140, 255), buf);
  }

  if (def->category <= 5 && def->attackSpeed > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Attack Speed: %d", def->attackSpeed);
    AddPendingTooltipLine(IM_COL32(200, 255, 200, 255), buf);
  }

  if (def->category >= 7 && def->category <= 11 && def->defense > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Defense: %d", def->defense);
    AddPendingTooltipLine(IM_COL32(140, 200, 255, 255), buf);
  }

  AddPendingTooltipLine(IM_COL32(80, 80, 120, 0), "---");

  auto addReq = [&](const char *label, int current, int req) {
    char rBuf[48];
    snprintf(rBuf, sizeof(rBuf), "%s: %d", label, req);
    ImU32 rcol = (current >= req) ? IM_COL32(180, 220, 180, 255)
                                  : IM_COL32(255, 80, 80, 255);
    AddPendingTooltipLine(rcol, rBuf);
  };

  if (def->levelReq > 0)
    addReq("Level", g_serverLevel, def->levelReq);
  if (def->reqStr > 0)
    addReq("STR", g_serverStr, def->reqStr);
  if (def->reqDex > 0)
    addReq("DEX", g_serverDex, def->reqDex);
  if (def->reqVit > 0)
    addReq("VIT", g_serverVit, def->reqVit);
  if (def->reqEne > 0)
    addReq("ENE", g_serverEne, def->reqEne);

  if (def->classFlags > 0 && def->classFlags != 0xFFFFFFFF) {
    std::string classes = "";
    if (def->classFlags & 1)
      classes += "DW ";
    if (def->classFlags & 2)
      classes += "DK ";
    if (def->classFlags & 4)
      classes += "FE ";
    if (def->classFlags & 8)
      classes += "MG";
    if (!classes.empty()) {
      uint32_t myFlag = (1 << (g_hero.GetClass() / 16));
      ImU32 col = (def->classFlags & myFlag) ? IM_COL32(160, 160, 255, 255)
                                             : IM_COL32(255, 80, 80, 255);
      AddPendingTooltipLine(col, classes);
    }
  }
}

// Draw the pending tooltip using a raw draw list (called after 3D render)
static void FlushPendingTooltip() {
  if (!g_pendingTooltip.active)
    return;
  g_pendingTooltip.active = false;
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  ImVec2 tPos = g_pendingTooltip.pos;
  float tw = g_pendingTooltip.w, th = g_pendingTooltip.h;
  dl->AddRectFilled(tPos, ImVec2(tPos.x + tw, tPos.y + th),
                    IM_COL32(10, 10, 20, 245), 4.0f);
  dl->AddRect(tPos, ImVec2(tPos.x + tw, tPos.y + th),
              IM_COL32(120, 120, 200, 200), 4.0f);
  float curY = tPos.y + 8;
  if (g_fontDefault)
    ImGui::PushFont(g_fontDefault);

  for (auto &line : g_pendingTooltip.lines) {
    if (line.text == "---") {
      // Horizontal separator
      dl->AddLine(ImVec2(tPos.x + 6, curY + 4),
                  ImVec2(tPos.x + tw - 6, curY + 4),
                  IM_COL32(80, 80, 120, 180));
      curY += 12;
    } else {
      dl->AddText(ImVec2(tPos.x + 10, curY), line.color, line.text.c_str());
      curY += 18.0f; // Matches lineH in AddPendingItemTooltip
    }
  }

  if (g_fontDefault)
    ImGui::PopFont();
}

// Forward declarations for panel rendering
static UICoords g_hudCoords; // File-scope for mouse callback access

// ServerEquipSlot defined in ClientTypes.hpp

// ── Drop Physics & Rotation Logic ──

static void GetItemRestingAngle(int defIndex, glm::vec3 &angle, float &scale) {
  angle = glm::vec3(90.0f, 0.0f, 0.0f); // Default: lay flat on ground
  scale = 1.0f;

  if (defIndex == -1) { // Zen
    angle = glm::vec3(0, 0, 0);
    return;
  }

  int category = 0;
  int index = 0;

  auto it = g_itemDefs.find(defIndex);
  if (it != g_itemDefs.end()) {
    category = it->second.category;
    index = it->second.itemIndex;
  } else {
    category = defIndex / 32;
    index = defIndex % 32;
  }

  // All weapons lay flat (90° X tilt) — vary Y for visual interest
  if (category == 0) { // Swords — diagonal
    angle = glm::vec3(90.0f, 45.0f, 0.0f);
    scale = 1.0f;
    if (index == 19)
      scale = 0.7f;           // Divine Sword
  } else if (category == 1) { // Axes
    angle = glm::vec3(90.0f, 30.0f, 0.0f);
  } else if (category == 2) { // Maces
    angle = glm::vec3(90.0f, 0.0f, 0.0f);
  } else if (category == 3) { // Spears — longer, lay along Y
    angle = glm::vec3(90.0f, 0.0f, 0.0f);
    scale = 0.9f;
  } else if (category == 4) { // Bows/Crossbows
    angle = glm::vec3(90.0f, 90.0f, 0.0f);
    scale = 0.9f;
  } else if (category == 5) { // Staffs
    angle = glm::vec3(90.0f, 0.0f, 0.0f);
  } else if (category == 6) { // Shields — lay face-up
    angle = glm::vec3(90.0f, 0.0f, 0.0f);
    scale = 0.9f;
  } else if (category == 7 || category == 8) { // Helms / Armor
    angle = glm::vec3(90.0f, 0.0f, 0.0f);
  } else if (category == 14) { // Potions — stand upright
    angle = glm::vec3(0.0f, 0.0f, 0.0f);
    scale = 0.6f;
  }
}

static void UpdateGroundItemPhysics(GroundItem &gi, float terrainHeight) {
  if (gi.isResting) {
    gi.position.y = terrainHeight + 0.5f; // Snap to ground
    return;
  }

  // Apply gravity
  gi.position.y += gi.gravity * 0.5f; // Integrate velocity (using Y as UP)
  gi.gravity -= 1.0f;                 // Gravity accel

  // Floor check (bounce)
  if (gi.position.y <= terrainHeight + 0.5f) {
    gi.position.y = terrainHeight + 0.5f;

    // Bounce
    if (abs(gi.gravity) > 2.0f) {
      gi.gravity = -gi.gravity * 0.4f; // Bounce with damping
    } else {
      gi.gravity = 0;
      gi.isResting = true;
    }
  }
}

static void RenderZenPile(int quantity, glm::vec3 pos, glm::vec3 angle,
                          float scale, const glm::mat4 &view,
                          const glm::mat4 &proj) {
  // Procedural pile based on quantity
  int coinCount = (int)sqrtf((float)quantity) / 2;
  if (coinCount < 3)
    coinCount = 3;
  if (coinCount > 20)
    coinCount = 20;

  // Seed rand with quantity to keep pile consistent per frame
  srand(quantity + (int)pos.x);

  for (int i = 0; i < coinCount; ++i) {
    glm::vec3 offset;
    offset.x = (rand() % 40) - 20.0f;
    offset.z =
        (rand() % 40) -
        20.0f; // Z is horizontal in MU-space relative to model center? No, X/Z
               // are ground plane in Physics, but RenderItemWorld takes Pos.
    // Wait, RenderItemWorld takes world pos. In World: X/Z are ground, Y is up.
    offset.y = 0;

    float rotY = (float)(rand() % 360);

    // Simple stacking effect check
    if (i > 5)
      offset.y += 2.0f;
    if (i > 10)
      offset.y += 4.0f;

    // RenderItemWorld handles the transform, but we need to pass a modified
    // pos? Actually, RenderItemWorld centers the model. We want to offset from
    // the "center" of the item. Let's call ItemModelManager::RenderItemWorld
    // multiple times with different positions.

    // We need random offsets in X/Z plane.
    ItemModelManager::RenderItemWorld("Gold01.bmd", pos + offset, view, proj,
                                      scale);
  }
}

// ServerData defined in ClientTypes.hpp

// Delegated to ClientPacketHandler::HandleInitialPacket
// (see src/ClientPacketHandler.cpp)

// Delegated to ClientPacketHandler::HandleGamePacket
// (see src/ClientPacketHandler.cpp)

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
static int rayPickMonster(GLFWwindow *window, double mouseX, double mouseY);
static int rayPickGroundItem(GLFWwindow *window, double mouseX, double mouseY);

void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
  ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
  // Camera rotation disabled — fixed isometric angle like original MU

  // Update NPC, Monster, and Ground Item hover state on cursor move
  if (!ImGui::GetIO().WantCaptureMouse) {
    g_hoveredNpc = rayPickNpc(window, xpos, ypos);
    if (g_hoveredNpc < 0) {
      g_hoveredMonster = rayPickMonster(window, xpos, ypos);
      if (g_hoveredMonster < 0) {
        g_hoveredGroundItem = rayPickGroundItem(window, xpos, ypos);
      } else {
        g_hoveredGroundItem = -1;
      }
    } else {
      g_hoveredMonster = -1;
      g_hoveredGroundItem = -1;
    }
  } else {
    g_hoveredNpc = -1;
    g_hoveredMonster = -1;
    g_hoveredGroundItem = -1;
  }
}

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
  ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
  g_camera.ProcessMouseScroll(yoffset);
}

// Init Inventory Grid
// This code snippet was likely intended for an initialization function.
// Placing it here as a standalone block would cause a syntax error.
// Assuming it should be part of a larger initialization routine,
// but without context, it's difficult to place correctly.
// For now, I'll place the logging statement where it makes sense,
// and comment out the `invX` line as it's incomplete and context-dependent.
// If this was meant to be a new function, it needs a function signature.
// float invX = g_windowWidth - 250.0f; // This line is incomplete without
// context. std::cout << "[Init] Loaded " << g_itemDefs.size() << " item
// definitions." << std::endl;

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
    float r = info.radius * 0.8f; // Slightly tighter for NPCs
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
    float r = info.radius *
              1.2f; // Reduced from 3.5f for more precise hover targeting
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
static bool g_dragFromQuickSlot = false;

// Forward declarations for panel interaction (defined later)
// ═══════════════════════════════════════════════════════════════════════
// Input Handling
// ═══════════════════════════════════════════════════════════════════════

static int rayPickGroundItem(GLFWwindow *window, double mouseX, double mouseY) {
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

  for (int i = 0; i < MAX_GROUND_ITEMS; ++i) {
    if (!g_groundItems[i].active)
      continue;

    float r = 50.0f; // Click radius around item
    glm::vec3 pos = g_groundItems[i].position;

    // Ray-sphere intersection (approximate for item)
    glm::vec3 oc = rayO - pos;
    float b = glm::dot(oc, rayD);
    float c = glm::dot(oc, oc) - r * r;
    float h = b * b - c;
    if (h < 0.0f)
      continue; // No hit

    float t = -b - sqrtf(h);
    if (t > 0 && t < bestT) {
      bestT = t;
      bestIdx = i;
    }
  }
  return bestIdx;
}

static void HandlePickupClick(GLFWwindow *window, double mx, double my) {
  if (g_showInventory || g_showCharInfo)
    return; // UI blocks pickup

  if (g_hoveredGroundItem != -1) {
    int bestIdx = g_hoveredGroundItem;
    float distToHero =
        glm::distance(g_hero.GetPosition(), g_groundItems[bestIdx].position);

    if (distToHero < 150.0f) {
      // Close enough, pick up immediately
      g_server.SendPickup(g_groundItems[bestIdx].dropIndex);
      std::cout << "[Pickup] Sent direct pickup for index "
                << g_groundItems[bestIdx].dropIndex << " (Close range)"
                << std::endl;
      g_hero.ClearPendingPickup();
    } else {
      // Too far, move to it and set pending pickup
      g_hero.MoveTo(g_groundItems[bestIdx].position);
      g_hero.SetPendingPickup(bestIdx);
      std::cout << "[Pickup] Moving to item index "
                << g_groundItems[bestIdx].dropIndex << std::endl;
    }
  }
}

static bool HandlePanelClick(float vx, float vy);
static void HandlePanelMouseUp(GLFWwindow *window, float vx, float vy);

// --- Click-to-move mouse handler ---

void mouse_button_callback(GLFWwindow *window, int button, int action,
                           int mods) {
  ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

  // Click-to-move on left click (NPC click takes priority)
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    if (!ImGui::GetIO().WantCaptureMouse && g_terrainDataPtr) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);

      // Check if click is on a UI panel or HUD first
      float vx = g_hudCoords.ToVirtualX((float)mx);
      float vy = g_hudCoords.ToVirtualY((float)my);
      if (HandlePanelClick(vx, vy))
        return;

      // Highest priority interactions: NPC > Monster > Ground Item > Movement
      int npcHit = rayPickNpc(window, mx, my);
      if (npcHit >= 0) {
        g_selectedNpc = npcHit;
        g_hero.CancelAttack();
        g_hero.ClearPendingPickup(); // Cancel pickup if interacting with NPC
      } else {
        g_selectedNpc = -1;
        if (g_hoveredMonster >= 0) {
          MonsterInfo info = g_monsterManager.GetMonsterInfo(g_hoveredMonster);
          g_hero.AttackMonster(g_hoveredMonster, info.position);
          g_hero.ClearPendingPickup(); // Cancel pickup if attacking monster
        } else if (g_hoveredGroundItem >= 0) {
          HandlePickupClick(window, mx, my);
        } else {
          // Ground click — move to terrain
          if (g_hero.IsAttacking())
            g_hero.CancelAttack();
          g_hero.ClearPendingPickup(); // Cancel pickup if manually moving
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
      HandlePanelMouseUp(window, vx, vy);
    }
  }
}

void key_callback(GLFWwindow *window, int key, int scancode, int action,
                  int mods) {
  ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
  // Note: do NOT check WantCaptureKeyboard here — it blocks game hotkeys
  // when ImGui panels have focus. Only text-input widgets need that guard.

  if (action == GLFW_PRESS) {
    if (key == GLFW_KEY_C)
      g_showCharInfo = !g_showCharInfo;
    if (key == GLFW_KEY_I)
      g_showInventory = !g_showInventory;
    if (key == GLFW_KEY_Q)
      ConsumeQuickSlotItem();
    if (key == GLFW_KEY_ESCAPE) {
      if (g_showCharInfo)
        g_showCharInfo = false;
      else if (g_showInventory)
        g_showInventory = false;
    }
  }
}

// Removed duplicate forward declaration
// void mouse_button_callback(GLFWwindow* window, int button, int action, int
// mods);

void char_callback(GLFWwindow *window, unsigned int c) {
  ImGui_ImplGlfw_CharCallback(window, c);
}

// --- Process input: hero movement + screenshot ---

void processInput(GLFWwindow *window, float deltaTime) {
  bool wasMoving = g_hero.IsMoving();
  g_hero.ProcessMovement(deltaTime);

  // Auto-pickup logic: check if we reached a pending item
  int pendingIdx = g_hero.GetPendingPickup();
  if (pendingIdx != -1) {
    if (pendingIdx >= 0 && pendingIdx < MAX_GROUND_ITEMS &&
        g_groundItems[pendingIdx].active) {
      float dist = glm::distance(g_hero.GetPosition(),
                                 g_groundItems[pendingIdx].position);
      if (dist < 150.0f) {
        g_server.SendPickup(g_groundItems[pendingIdx].dropIndex);
        std::cout << "[Pickup] REACHED: Auto-picking item index "
                  << g_groundItems[pendingIdx].dropIndex << std::endl;
        g_hero.ClearPendingPickup();
      }
    } else {
      g_hero.ClearPendingPickup(); // Item no longer active
    }
  }

  // Hide click effect when hero stops moving
  if (wasMoving && !g_hero.IsMoving())
    g_clickEffect.Hide();

  // Camera follows hero (Continuous)
  g_camera.SetPosition(g_hero.GetPosition());
}

// ═══════════════════════════════════════════════════════════════════════
// Panel rendering: Character Info (C) and Inventory (I)
// ═══════════════════════════════════════════════════════════════════════

static const char *GetEquipSlotName(int slot) {
  static const char *names[] = {"R.Hand", "L.Hand",  "Helm",   "Armor",
                                "Pants",  "Gloves",  "Boots",  "Wings",
                                "Pet",    "Pendant", "Ring 1", "Ring 2"};
  if (slot >= 0 && slot < 12)
    return names[slot];
  return "???";
}

static void InitItemDefinitions() {
  // Matches 0.97d server seeding
  auto addDef = [](int16_t id, uint8_t cat, uint8_t idx, const char *name,
                   const char *mod, uint8_t w, uint8_t h, uint16_t s,
                   uint16_t d, uint16_t v, uint16_t e, uint16_t l, uint32_t cf,
                   uint16_t dmgMin = 0, uint16_t dmgMax = 0,
                   uint16_t defense = 0, uint8_t attackSpeed = 0,
                   bool twoHanded = false) {
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
    cd.dmgMin = dmgMin;
    cd.dmgMax = dmgMax;
    cd.defense = defense;
    cd.attackSpeed = attackSpeed;
    cd.twoHanded = twoHanded;

    // Use Standard ID (Cat*32 + Idx) as key
    // This matches what the server sends for drops and ensures consistency
    int16_t standardId = (int16_t)cat * 32 + idx;
    g_itemDefs[standardId] = cd;
  };

  // IDs are used locally as keys in g_itemDefs.
  // We'll use IDs that won't collide with the server's autoincrement range if
  // possible, but since we sync by (Cat, Idx), the actual ID value here is
  // arbitrary as long as it's unique.

  // Auto-generated from Database.cpp
  // Category 0: Swords
  //                id  cat idx  name              model         w  h  str dex
  //                vit ene lvl cf  dmgMin dmgMax def atkSpd 2H
  // Category 0: Swords (OpenMU 0.95d Weapons.cs)
  addDef(0, 0, 0, "Kris", "Sword01.bmd", 1, 2, 10, 8, 0, 0, 1, 11, 6, 11, 0, 50,
         false);
  addDef(1, 0, 1, "Short Sword", "Sword02.bmd", 1, 3, 20, 0, 0, 0, 1, 7, 3, 7,
         0, 20, false);
  addDef(2, 0, 2, "Rapier", "Sword03.bmd", 1, 3, 50, 40, 0, 0, 9, 6, 9, 15, 0,
         40, false);
  addDef(3, 0, 3, "Katana", "Sword04.bmd", 1, 3, 80, 40, 0, 0, 16, 2, 16, 26, 0,
         35, false);
  addDef(4, 0, 4, "Sword of Assassin", "Sword05.bmd", 1, 3, 60, 40, 0, 0, 12, 2,
         12, 18, 0, 30, false);
  addDef(5, 0, 5, "Blade", "Sword06.bmd", 1, 3, 80, 50, 0, 0, 36, 7, 36, 47, 0,
         30, false);
  addDef(6, 0, 6, "Gladius", "Sword07.bmd", 1, 3, 110, 0, 0, 0, 20, 6, 20, 30,
         0, 20, false);
  addDef(7, 0, 7, "Falchion", "Sword08.bmd", 1, 3, 120, 0, 0, 0, 24, 2, 24, 34,
         0, 25, false);
  addDef(8, 0, 8, "Serpent Sword", "Sword09.bmd", 1, 3, 130, 0, 0, 0, 30, 2, 30,
         40, 0, 20, false);
  addDef(9, 0, 9, "Sword of Salamander", "Sword10.bmd", 2, 3, 103, 0, 0, 0, 32,
         2, 32, 46, 0, 30, true);
  addDef(10, 0, 10, "Light Saber", "Sword11.bmd", 2, 4, 80, 60, 0, 0, 40, 6, 47,
         61, 0, 25, true);
  addDef(11, 0, 11, "Legendary Sword", "Sword12.bmd", 2, 3, 120, 0, 0, 0, 44, 2,
         56, 72, 0, 20, true);
  addDef(12, 0, 12, "Heliacal Sword", "Sword13.bmd", 2, 3, 140, 0, 0, 0, 56, 2,
         73, 98, 0, 25, true);
  addDef(13, 0, 13, "Double Blade", "Sword14.bmd", 1, 3, 70, 70, 0, 0, 48, 6,
         48, 56, 0, 30, false);
  addDef(14, 0, 14, "Lightning Sword", "Sword15.bmd", 1, 3, 90, 50, 0, 0, 59, 6,
         59, 67, 0, 30, false);
  addDef(15, 0, 15, "Giant Sword", "Sword16.bmd", 2, 3, 140, 0, 0, 0, 52, 2, 60,
         85, 0, 20, true);
  addDef(16, 0, 16, "Sword of Destruction", "Sword17.bmd", 1, 4, 160, 60, 0, 0,
         82, 10, 82, 90, 0, 35, false);
  addDef(17, 0, 17, "Dark Breaker", "Sword18.bmd", 2, 4, 180, 50, 0, 0, 104, 2,
         128, 153, 0, 40, true);
  addDef(18, 0, 18, "Thunder Blade", "Sword19.bmd", 2, 3, 180, 50, 0, 0, 105, 8,
         140, 168, 0, 40, true);
  // Category 1: Axes (OpenMU 0.95d Weapons.cs)
  addDef(32, 1, 0, "Small Axe", "Axe01.bmd", 1, 3, 20, 0, 0, 0, 1, 7, 1, 6, 0,
         20, false);
  addDef(33, 1, 1, "Hand Axe", "Axe02.bmd", 1, 3, 70, 0, 0, 0, 4, 7, 4, 9, 0,
         30, false);
  addDef(34, 1, 2, "Double Axe", "Axe03.bmd", 1, 3, 90, 0, 0, 0, 14, 2, 14, 24,
         0, 20, false);
  addDef(35, 1, 3, "Tomahawk", "Axe04.bmd", 1, 3, 100, 0, 0, 0, 18, 2, 18, 28,
         0, 30, false);
  addDef(36, 1, 4, "Elven Axe", "Axe05.bmd", 1, 3, 50, 70, 0, 0, 26, 5, 26, 38,
         0, 40, false);
  addDef(37, 1, 5, "Battle Axe", "Axe06.bmd", 2, 3, 120, 0, 0, 0, 30, 6, 36, 44,
         0, 20, true);
  addDef(38, 1, 6, "Nikkea Axe", "Axe07.bmd", 2, 3, 130, 0, 0, 0, 34, 6, 38, 50,
         0, 30, true);
  addDef(39, 1, 7, "Larkan Axe", "Axe08.bmd", 2, 3, 140, 0, 0, 0, 46, 2, 54, 67,
         0, 25, true);
  addDef(40, 1, 8, "Crescent Axe", "Axe09.bmd", 2, 3, 100, 40, 0, 0, 54, 3, 69,
         89, 0, 30, true);
  // Category 2: Maces (OpenMU 0.95d Weapons.cs)
  addDef(64, 2, 0, "Mace", "Mace01.bmd", 1, 3, 100, 0, 0, 0, 7, 2, 7, 13, 0, 15,
         false);
  addDef(65, 2, 1, "Morning Star", "Mace02.bmd", 1, 3, 100, 0, 0, 0, 13, 2, 13,
         22, 0, 15, false);
  addDef(66, 2, 2, "Flail", "Mace03.bmd", 1, 3, 80, 50, 0, 0, 22, 2, 22, 32, 0,
         15, false);
  addDef(67, 2, 3, "Great Hammer", "Mace04.bmd", 2, 3, 150, 0, 0, 0, 38, 2, 45,
         56, 0, 15, true);
  addDef(68, 2, 4, "Crystal Morning Star", "Mace05.bmd", 2, 3, 130, 0, 0, 0, 66,
         7, 78, 107, 0, 30, true);
  addDef(69, 2, 5, "Crystal Sword", "Mace06.bmd", 2, 4, 130, 70, 0, 0, 72, 7,
         89, 120, 0, 40, true);
  addDef(70, 2, 6, "Chaos Dragon Axe", "Mace07.bmd", 2, 4, 140, 50, 0, 0, 75, 2,
         102, 130, 0, 35, true);
  // Category 3: Spears (OpenMU 0.95d Weapons.cs)
  addDef(96, 3, 0, "Light Spear", "Spear01.bmd", 2, 4, 60, 70, 0, 0, 42, 6, 50,
         63, 0, 25, true);
  addDef(97, 3, 1, "Spear", "Spear02.bmd", 2, 4, 70, 50, 0, 0, 23, 6, 30, 41, 0,
         30, true);
  addDef(98, 3, 2, "Dragon Lance", "Spear03.bmd", 2, 4, 70, 50, 0, 0, 15, 6, 21,
         33, 0, 30, true);
  addDef(99, 3, 3, "Giant Trident", "Spear04.bmd", 2, 4, 90, 30, 0, 0, 29, 6,
         35, 43, 0, 25, true);
  addDef(100, 3, 4, "Serpent Spear", "Spear05.bmd", 2, 4, 90, 30, 0, 0, 46, 6,
         58, 80, 0, 20, true);
  addDef(101, 3, 5, "Double Poleaxe", "Spear06.bmd", 2, 4, 70, 50, 0, 0, 13, 6,
         19, 31, 0, 30, true);
  addDef(102, 3, 6, "Halberd", "Spear07.bmd", 2, 4, 70, 50, 0, 0, 19, 6, 25, 35,
         0, 30, true);
  addDef(103, 3, 7, "Berdysh", "Spear08.bmd", 2, 4, 80, 50, 0, 0, 37, 6, 42, 54,
         0, 30, true);
  addDef(104, 3, 8, "Great Scythe", "Spear09.bmd", 2, 4, 90, 50, 0, 0, 54, 6,
         71, 92, 0, 25, true);
  addDef(105, 3, 9, "Bill of Balrog", "Spear10.bmd", 2, 4, 80, 50, 0, 0, 63, 6,
         76, 102, 0, 25, true);
  // Category 4: Bows & Crossbows (OpenMU 0.95d Weapons.cs)
  addDef(128, 4, 0, "Short Bow", "Bow01.bmd", 2, 3, 20, 80, 0, 0, 2, 4, 3, 5, 0,
         30, true);
  addDef(129, 4, 1, "Bow", "Bow02.bmd", 2, 3, 30, 90, 0, 0, 8, 4, 9, 13, 0, 30,
         true);
  addDef(130, 4, 2, "Elven Bow", "Bow03.bmd", 2, 3, 30, 90, 0, 0, 16, 4, 17, 24,
         0, 30, true);
  addDef(131, 4, 3, "Battle Bow", "Bow04.bmd", 2, 3, 30, 90, 0, 0, 26, 4, 28,
         37, 0, 30, true);
  addDef(132, 4, 4, "Tiger Bow", "Bow05.bmd", 2, 4, 30, 100, 0, 0, 40, 4, 42,
         52, 0, 30, true);
  addDef(133, 4, 5, "Silver Bow", "Bow06.bmd", 2, 4, 30, 100, 0, 0, 56, 4, 59,
         71, 0, 40, true);
  addDef(134, 4, 6, "Chaos Nature Bow", "Bow07.bmd", 2, 4, 40, 150, 0, 0, 75, 4,
         88, 106, 0, 35, true);
  addDef(135, 4, 7, "Bolt", "Bolt01.bmd", 1, 1, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0,
         false);
  addDef(136, 4, 8, "Crossbow", "CrossBow01.bmd", 2, 2, 20, 90, 0, 0, 4, 4, 5,
         8, 0, 40, false);
  addDef(137, 4, 9, "Golden Crossbow", "CrossBow02.bmd", 2, 2, 30, 90, 0, 0, 12,
         4, 13, 19, 0, 40, false);
  addDef(138, 4, 10, "Arquebus", "CrossBow03.bmd", 2, 2, 30, 90, 0, 0, 20, 4,
         22, 30, 0, 40, false);
  addDef(139, 4, 11, "Light Crossbow", "CrossBow04.bmd", 2, 3, 30, 90, 0, 0, 32,
         4, 35, 44, 0, 40, false);
  addDef(140, 4, 12, "Serpent Crossbow", "CrossBow05.bmd", 2, 3, 30, 100, 0, 0,
         48, 4, 50, 61, 0, 40, false);
  addDef(141, 4, 13, "Bluewing Crossbow", "CrossBow06.bmd", 2, 3, 40, 110, 0, 0,
         68, 4, 68, 82, 0, 40, false);
  addDef(142, 4, 14, "Aquagold Crossbow", "CrossBow07.bmd", 2, 3, 50, 130, 0, 0,
         72, 4, 78, 92, 0, 30, false);
  addDef(143, 4, 15, "Arrows", "Arrow01.bmd", 1, 1, 0, 0, 0, 0, 0, 4, 0, 0, 0,
         0, false);
  addDef(144, 4, 16, "Saint Crossbow", "CrossBow08.bmd", 2, 3, 50, 130, 0, 0,
         83, 4, 90, 108, 0, 35, false);
  // Category 5: Staves (OpenMU 0.95d Weapons.cs)
  addDef(160, 5, 0, "Skull Staff", "Staff01.bmd", 1, 3, 40, 0, 0, 0, 6, 1, 3, 4,
         0, 20, false);
  addDef(161, 5, 1, "Angelic Staff", "Staff02.bmd", 2, 3, 50, 0, 0, 0, 18, 1,
         10, 12, 0, 25, false);
  addDef(162, 5, 2, "Serpent Staff", "Staff03.bmd", 2, 3, 50, 0, 0, 0, 30, 1,
         17, 18, 0, 25, false);
  addDef(163, 5, 3, "Thunder Staff", "Staff04.bmd", 2, 4, 40, 10, 0, 0, 42, 1,
         23, 25, 0, 25, false);
  addDef(164, 5, 4, "Gorgon Staff", "Staff05.bmd", 2, 4, 60, 0, 0, 0, 52, 1, 29,
         32, 0, 25, false);
  addDef(165, 5, 5, "Legendary Staff", "Staff06.bmd", 1, 4, 50, 0, 0, 0, 59, 1,
         29, 31, 0, 25, false);
  addDef(166, 5, 6, "Staff of Resurrection", "Staff07.bmd", 1, 4, 60, 10, 0, 0,
         70, 1, 35, 39, 0, 25, false);
  addDef(167, 5, 7, "Chaos Lightning Staff", "Staff08.bmd", 2, 4, 60, 10, 0, 0,
         75, 1, 47, 48, 0, 30, false);
  addDef(168, 5, 8, "Staff of Destruction", "Staff09.bmd", 2, 4, 60, 10, 0, 0,
         90, 9, 55, 60, 0, 35, false);
  // Category 6: Shields (OpenMU v0.75)
  addDef(192, 6, 0, "Small Shield", "Shield01.bmd", 2, 2, 70, 0, 0, 0, 3, 15, 0,
         0, 3, 0, false);
  addDef(193, 6, 1, "Horn Shield", "Shield02.bmd", 2, 2, 100, 0, 0, 0, 9, 2, 0,
         0, 9, 0, false);
  addDef(194, 6, 2, "Kite Shield", "Shield03.bmd", 2, 2, 110, 0, 0, 0, 12, 2, 0,
         0, 12, 0, false);
  addDef(195, 6, 3, "Elven Shield", "Shield04.bmd", 2, 2, 30, 100, 0, 0, 21, 4,
         0, 0, 21, 0, false);
  addDef(196, 6, 4, "Buckler", "Shield05.bmd", 2, 2, 80, 0, 0, 0, 6, 15, 0, 0,
         6, 0, false);
  addDef(197, 6, 5, "Dragon Slayer Shield", "Shield06.bmd", 2, 2, 100, 40, 0, 0,
         35, 2, 0, 0, 36, 0, false);
  addDef(198, 6, 6, "Skull Shield", "Shield07.bmd", 2, 2, 110, 0, 0, 0, 15, 15,
         0, 0, 15, 0, false);
  addDef(199, 6, 7, "Spiked Shield", "Shield08.bmd", 2, 2, 130, 0, 0, 0, 30, 2,
         0, 0, 30, 0, false);
  addDef(200, 6, 8, "Tower Shield", "Shield09.bmd", 2, 2, 130, 0, 0, 0, 40, 11,
         0, 0, 40, 0, false);
  addDef(201, 6, 9, "Plate Shield", "Shield10.bmd", 2, 2, 120, 0, 0, 0, 25, 2,
         0, 0, 25, 0, false);
  addDef(202, 6, 10, "Big Round Shield", "Shield11.bmd", 2, 2, 120, 0, 0, 0, 18,
         2, 0, 0, 18, 0, false);
  addDef(203, 6, 11, "Serpent Shield", "Shield12.bmd", 2, 2, 130, 0, 0, 0, 45,
         11, 0, 0, 45, 0, false);
  addDef(204, 6, 12, "Bronze Shield", "Shield13.bmd", 2, 2, 140, 0, 0, 0, 54, 2,
         0, 0, 54, 0, false);
  addDef(205, 6, 13, "Dragon Shield", "Shield14.bmd", 2, 2, 120, 40, 0, 0, 60,
         2, 0, 0, 60, 0, false);
  addDef(206, 6, 14, "Legendary Shield", "Shield15.bmd", 2, 3, 90, 25, 0, 0, 48,
         5, 0, 0, 48, 0, false);
  // Category 7-11: Armors (OpenMU v0.75 - Pad, Leather, Bronze, etc.)
  // Helmets (7)
  addDef(224, 7, 0, "Bronze Helm", "HelmMale01.bmd", 2, 2, 25, 20, 0, 0, 1, 2,
         0, 0, 34, 0, false);
  addDef(225, 7, 1, "Dragon Helm", "HelmMale02.bmd", 2, 2, 120, 30, 0, 0, 57, 2,
         0, 0, 68, 0, false);
  addDef(226, 7, 2, "Pad Helm", "HelmClass01.bmd", 2, 2, 0, 0, 0, 20, 1, 1, 0,
         0, 28, 0, false);
  addDef(227, 7, 3, "Legendary Helm", "HelmClass02.bmd", 2, 2, 30, 0, 0, 0, 50,
         1, 0, 0, 42, 0, false);
  addDef(228, 7, 4, "Bone Helm", "HelmClass03.bmd", 2, 2, 30, 0, 0, 0, 18, 1, 0,
         0, 30, 0, false);
  addDef(229, 7, 5, "Leather Helm", "HelmMale06.bmd", 2, 2, 20, 0, 0, 0, 1, 2,
         0, 0, 30, 0, false);
  addDef(230, 7, 6, "Scale Helm", "HelmMale07.bmd", 2, 2, 110, 0, 0, 0, 26, 2,
         0, 0, 40, 0, false);
  addDef(231, 7, 7, "Sphinx Mask", "HelmClass04.bmd", 2, 2, 30, 0, 0, 0, 32, 1,
         0, 0, 36, 0, false);
  addDef(232, 7, 8, "Brass Helm", "HelmMale09.bmd", 2, 2, 100, 30, 0, 0, 36, 2,
         0, 0, 44, 0, false);
  addDef(233, 7, 9, "Plate Helm", "HelmMale10.bmd", 2, 2, 130, 0, 0, 0, 46, 2,
         0, 0, 50, 0, false);
  addDef(234, 7, 10, "Vine Helm", "HelmClass05.bmd", 2, 2, 30, 60, 0, 0, 6, 4,
         0, 0, 22, 0, false);
  addDef(235, 7, 11, "Silk Helm", "HelmClass06.bmd", 2, 2, 0, 0, 0, 20, 1, 4, 0,
         0, 26, 0, false);
  addDef(236, 7, 12, "Wind Helm", "HelmClass07.bmd", 2, 2, 30, 80, 0, 0, 28, 4,
         0, 0, 32, 0, false);
  addDef(237, 7, 13, "Spirit Helm", "HelmClass08.bmd", 2, 2, 40, 80, 0, 0, 40,
         4, 0, 0, 38, 0, false);
  addDef(238, 7, 14, "Guardian Helm", "HelmClass09.bmd", 2, 2, 40, 80, 0, 0, 53,
         4, 0, 0, 45, 0, false);
  // Armors (8)
  addDef(256, 8, 0, "Bronze Armor", "ArmorMale01.bmd", 2, 2, 25, 20, 0, 0, 1, 2,
         0, 0, 34, 0, false);
  addDef(257, 8, 1, "Dragon Armor", "ArmorMale02.bmd", 2, 3, 120, 30, 0, 0, 59,
         2, 0, 0, 68, 0, false);
  addDef(258, 8, 2, "Pad Armor", "ArmorClass01.bmd", 2, 2, 0, 0, 0, 20, 1, 1, 0,
         0, 28, 0, false);
  addDef(259, 8, 3, "Legendary Armor", "ArmorClass02.bmd", 2, 2, 40, 0, 0, 0,
         56, 1, 0, 0, 42, 0, false);
  addDef(260, 8, 4, "Bone Armor", "ArmorClass03.bmd", 2, 2, 40, 0, 0, 0, 22, 1,
         0, 0, 30, 0, false);
  addDef(261, 8, 5, "Leather Armor", "ArmorMale06.bmd", 2, 3, 20, 0, 0, 0, 1, 2,
         0, 0, 30, 0, false);
  addDef(262, 8, 6, "Scale Armor", "ArmorMale07.bmd", 2, 2, 110, 0, 0, 0, 28, 2,
         0, 0, 40, 0, false);
  addDef(263, 8, 7, "Sphinx Armor", "ArmorClass04.bmd", 2, 3, 40, 0, 0, 0, 38,
         1, 0, 0, 36, 0, false);
  addDef(264, 8, 8, "Brass Armor", "ArmorMale09.bmd", 2, 2, 100, 30, 0, 0, 38,
         2, 0, 0, 44, 0, false);
  addDef(265, 8, 9, "Plate Armor", "ArmorMale10.bmd", 2, 2, 130, 0, 0, 0, 48, 2,
         0, 0, 50, 0, false);
  addDef(266, 8, 10, "Vine Armor", "ArmorClass05.bmd", 2, 2, 30, 60, 0, 0, 10,
         4, 0, 0, 22, 0, false);
  addDef(267, 8, 11, "Silk Armor", "ArmorClass06.bmd", 2, 2, 0, 0, 0, 20, 1, 4,
         0, 0, 26, 0, false);
  addDef(268, 8, 12, "Wind Armor", "ArmorClass07.bmd", 2, 2, 30, 80, 0, 0, 32,
         4, 0, 0, 32, 0, false);
  addDef(269, 8, 13, "Spirit Armor", "ArmorClass08.bmd", 2, 2, 40, 80, 0, 0, 44,
         4, 0, 0, 38, 0, false);
  addDef(270, 8, 14, "Guardian Armor", "ArmorClass09.bmd", 2, 2, 40, 80, 0, 0,
         57, 4, 0, 0, 45, 0, false);
  // Pants (9)
  addDef(288, 9, 0, "Bronze Pants", "PantMale01.bmd", 2, 2, 25, 20, 0, 0, 1, 2,
         0, 0, 34, 0, false);
  addDef(289, 9, 1, "Dragon Pants", "PantMale02.bmd", 2, 2, 120, 30, 0, 0, 55,
         2, 0, 0, 68, 0, false);
  addDef(290, 9, 2, "Pad Pants", "PantClass01.bmd", 2, 2, 0, 0, 0, 20, 1, 1, 0,
         0, 28, 0, false);
  addDef(291, 9, 3, "Legendary Pants", "PantClass02.bmd", 2, 2, 40, 0, 0, 0, 53,
         1, 0, 0, 42, 0, false);
  addDef(292, 9, 4, "Bone Pants", "PantClass03.bmd", 2, 2, 40, 0, 0, 0, 20, 1,
         0, 0, 30, 0, false);
  addDef(293, 9, 5, "Leather Pants", "PantMale06.bmd", 2, 2, 20, 0, 0, 0, 1, 2,
         0, 0, 30, 0, false);
  addDef(294, 9, 6, "Scale Pants", "PantMale07.bmd", 2, 2, 110, 0, 0, 0, 25, 2,
         0, 0, 40, 0, false);
  addDef(295, 9, 7, "Sphinx Pants", "PantClass04.bmd", 2, 2, 40, 0, 0, 0, 34, 1,
         0, 0, 36, 0, false);
  addDef(296, 9, 8, "Brass Pants", "PantMale09.bmd", 2, 2, 100, 30, 0, 0, 35, 2,
         0, 0, 44, 0, false);
  addDef(297, 9, 9, "Plate Pants", "PantMale10.bmd", 2, 2, 130, 0, 0, 0, 45, 2,
         0, 0, 50, 0, false);
  addDef(298, 9, 10, "Vine Pants", "PantClass05.bmd", 2, 2, 30, 60, 0, 0, 8, 4,
         0, 0, 22, 0, false);
  addDef(299, 9, 11, "Silk Pants", "PantClass06.bmd", 2, 2, 0, 0, 0, 20, 1, 4,
         0, 0, 26, 0, false);
  addDef(300, 9, 12, "Wind Pants", "PantClass07.bmd", 2, 2, 30, 80, 0, 0, 30, 4,
         0, 0, 32, 0, false);
  addDef(301, 9, 13, "Spirit Pants", "PantClass08.bmd", 2, 2, 40, 80, 0, 0, 42,
         4, 0, 0, 38, 0, false);
  addDef(302, 9, 14, "Guardian Pants", "PantClass09.bmd", 2, 2, 40, 80, 0, 0,
         54, 4, 0, 0, 45, 0, false);
  // Gloves (10)
  addDef(320, 10, 0, "Bronze Gloves", "GloveMale01.bmd", 2, 2, 25, 20, 0, 0, 1,
         2, 0, 0, 34, 0, false);
  addDef(321, 10, 1, "Dragon Gloves", "GloveMale02.bmd", 2, 2, 120, 30, 0, 0,
         52, 2, 0, 0, 68, 0, false);
  addDef(322, 10, 2, "Pad Gloves", "GloveClass01.bmd", 2, 2, 0, 0, 0, 20, 1, 1,
         0, 0, 28, 0, false);
  addDef(323, 10, 3, "Legendary Gloves", "GloveClass02.bmd", 2, 2, 20, 0, 0, 0,
         44, 1, 0, 0, 42, 0, false);
  addDef(324, 10, 4, "Bone Gloves", "GloveClass03.bmd", 2, 2, 20, 0, 0, 0, 14,
         1, 0, 0, 30, 0, false);
  addDef(325, 10, 5, "Leather Gloves", "GloveMale06.bmd", 2, 2, 20, 0, 0, 0, 1,
         2, 0, 0, 30, 0, false);
  addDef(326, 10, 6, "Scale Gloves", "GloveMale07.bmd", 2, 2, 110, 0, 0, 0, 22,
         2, 0, 0, 40, 0, false);
  addDef(327, 10, 7, "Sphinx Gloves", "GloveClass04.bmd", 2, 2, 20, 0, 0, 0, 28,
         1, 0, 0, 36, 0, false);
  addDef(328, 10, 8, "Brass Gloves", "GloveMale09.bmd", 2, 2, 100, 30, 0, 0, 32,
         2, 0, 0, 44, 0, false);
  addDef(329, 10, 9, "Plate Gloves", "GloveMale10.bmd", 2, 2, 130, 0, 0, 0, 42,
         2, 0, 0, 50, 0, false);
  addDef(330, 10, 10, "Vine Gloves", "GloveClass05.bmd", 2, 2, 30, 60, 0, 0, 4,
         4, 0, 0, 22, 0, false);
  addDef(331, 10, 11, "Silk Gloves", "GloveClass06.bmd", 2, 2, 0, 0, 0, 20, 1,
         4, 0, 0, 26, 0, false);
  addDef(332, 10, 12, "Wind Gloves", "GloveClass07.bmd", 2, 2, 30, 80, 0, 0, 26,
         4, 0, 0, 32, 0, false);
  addDef(333, 10, 13, "Spirit Gloves", "GloveClass08.bmd", 2, 2, 40, 80, 0, 0,
         38, 4, 0, 0, 38, 0, false);
  addDef(334, 10, 14, "Guardian Gloves", "GloveClass09.bmd", 2, 2, 40, 80, 0, 0,
         50, 4, 0, 0, 45, 0, false);
  // Boots (11)
  addDef(352, 11, 0, "Bronze Boots", "BootMale01.bmd", 2, 2, 25, 20, 0, 0, 1, 2,
         0, 0, 34, 0, false);
  addDef(353, 11, 1, "Dragon Boots", "BootMale02.bmd", 2, 2, 120, 30, 0, 0, 54,
         2, 0, 0, 68, 0, false);
  addDef(354, 11, 2, "Pad Boots", "BootClass01.bmd", 2, 2, 0, 0, 0, 20, 1, 1, 0,
         0, 28, 0, false);
  addDef(355, 11, 3, "Legendary Boots", "BootClass02.bmd", 2, 2, 30, 0, 0, 0,
         46, 1, 0, 0, 42, 0, false);
  addDef(356, 11, 4, "Bone Boots", "BootClass03.bmd", 2, 2, 30, 0, 0, 0, 16, 1,
         0, 0, 30, 0, false);
  addDef(357, 11, 5, "Leather Boots", "BootMale06.bmd", 2, 2, 20, 0, 0, 0, 1, 2,
         0, 0, 30, 0, false);
  addDef(358, 11, 6, "Scale Boots", "BootMale07.bmd", 2, 2, 110, 0, 0, 0, 22, 2,
         0, 0, 40, 0, false);
  addDef(359, 11, 7, "Sphinx Boots", "BootClass04.bmd", 2, 2, 30, 0, 0, 0, 30,
         1, 0, 0, 36, 0, false);
  addDef(360, 11, 8, "Brass Boots", "BootMale09.bmd", 2, 2, 100, 30, 0, 0, 32,
         2, 0, 0, 44, 0, false);
  addDef(361, 11, 9, "Plate Boots", "BootMale10.bmd", 2, 2, 130, 0, 0, 0, 42, 2,
         0, 0, 50, 0, false);
  addDef(362, 11, 10, "Vine Boots", "BootClass05.bmd", 2, 2, 30, 60, 0, 0, 5, 4,
         0, 0, 22, 0, false);
  addDef(363, 11, 11, "Silk Boots", "BootClass06.bmd", 2, 2, 0, 0, 0, 20, 1, 4,
         0, 0, 26, 0, false);
  addDef(364, 11, 12, "Wind Boots", "BootClass07.bmd", 2, 2, 30, 80, 0, 0, 27,
         4, 0, 0, 32, 0, false);
  addDef(365, 11, 13, "Spirit Boots", "BootClass08.bmd", 2, 2, 40, 80, 0, 0, 40,
         4, 0, 0, 38, 0, false);
  addDef(366, 11, 14, "Guardian Boots", "BootClass09.bmd", 2, 2, 40, 80, 0, 0,
         52, 4, 0, 0, 45, 0, false);

  // Category 12: Wings (IDs 700+)
  addDef(700, 12, 0, "Wings of Elf", "Wing01.bmd", 3, 2, 0, 0, 0, 0, 100, 4);
  addDef(701, 12, 1, "Wings of Heaven", "Wing02.bmd", 3, 2, 0, 0, 0, 0, 100, 1);
  addDef(702, 12, 2, "Wings of Satan", "Wing03.bmd", 3, 2, 0, 0, 0, 0, 100, 2);
  addDef(703, 12, 3, "Wings of Spirits", "Wing04.bmd", 4, 3, 0, 0, 0, 0, 150,
         4);
  addDef(704, 12, 4, "Wings of Soul", "Wing05.bmd", 4, 3, 0, 0, 0, 0, 150, 1);
  addDef(705, 12, 5, "Wings of Dragon", "Wing06.bmd", 4, 3, 0, 0, 0, 0, 150, 2);
  addDef(706, 12, 6, "Wings of Darkness", "Wing07.bmd", 4, 3, 0, 0, 0, 0, 150,
         8);

  // Category 12: Orbs (IDs 750+)
  addDef(757, 12, 7, "Orb of Twisting Slash", "Gem01.bmd", 1, 1, 0, 0, 0, 0, 47,
         2);
  addDef(758, 12, 8, "Orb of Healing", "Gem02.bmd", 1, 1, 0, 0, 0, 100, 8, 4);
  addDef(759, 12, 9, "Orb of Greater Defense", "Gem03.bmd", 1, 1, 0, 0, 0, 100,
         13, 4);
  addDef(760, 12, 10, "Orb of Greater Damage", "Gem04.bmd", 1, 1, 0, 0, 0, 100,
         18, 4);
  addDef(761, 12, 11, "Orb of Summoning", "Gem05.bmd", 1, 1, 0, 0, 0, 0, 3, 4);
  addDef(762, 12, 12, "Orb of Rageful Blow", "Gem06.bmd", 1, 1, 170, 0, 0, 0,
         78, 2);
  addDef(763, 12, 13, "Orb of Impale", "Gem07.bmd", 1, 1, 28, 0, 0, 0, 20, 2);
  addDef(764, 12, 14, "Orb of Greater Fortitude", "Gem08.bmd", 1, 1, 120, 0, 0,
         0, 60, 2);
  addDef(766, 12, 16, "Orb of Fire Slash", "Gem10.bmd", 1, 1, 320, 0, 0, 0, 60,
         8);
  addDef(767, 12, 17, "Orb of Penetration", "Gem11.bmd", 1, 1, 130, 0, 0, 0, 64,
         4);
  addDef(768, 12, 18, "Orb of Ice Arrow", "Gem12.bmd", 1, 1, 0, 258, 0, 0, 81,
         4);
  addDef(769, 12, 19, "Orb of Death Stab", "Gem13.bmd", 1, 1, 160, 0, 0, 0, 72,
         2);

  // Category 12 (Jewels mix) & Category 13 (Jewelry/Pets) (IDs 800+)
  addDef(815, 12, 15, "Jewel of Chaos", "Jewel15.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(800, 13, 0, "Guardian Angel", "Helper01.bmd", 1, 1, 0, 0, 0, 0, 23,
         15);
  addDef(801, 13, 1, "Imp", "Helper02.bmd", 1, 1, 0, 0, 0, 0, 28, 15);
  addDef(802, 13, 2, "Horn of Uniria", "Helper03.bmd", 1, 1, 0, 0, 0, 0, 25,
         15);
  addDef(803, 13, 3, "Horn of Dinorant", "Pet04.bmd", 1, 1, 0, 0, 0, 0, 110,
         15);
  addDef(808, 13, 8, "Ring of Ice", "Ring01.bmd", 1, 1, 0, 0, 0, 0, 20, 15);
  addDef(809, 13, 9, "Ring of Poison", "Ring02.bmd", 1, 1, 0, 0, 0, 0, 17, 15);
  addDef(810, 13, 10, "Transformation Ring", "Ring01.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(812, 13, 12, "Pendant of Lighting", "Necklace01.bmd", 1, 1, 0, 0, 0, 0,
         21, 15);
  addDef(813, 13, 13, "Pendant of Fire", "Necklace02.bmd", 1, 1, 0, 0, 0, 0, 13,
         15);

  // Category 14: Consumables (IDs 850+)
  addDef(850, 14, 0, "Apple", "Potion01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(851, 14, 1, "Small HP Potion", "Potion02.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(852, 14, 2, "Medium HP Potion", "Potion03.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(853, 14, 3, "Large HP Potion", "Potion04.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(854, 14, 4, "Small Mana Potion", "Potion05.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(855, 14, 5, "Medium Mana Potion", "Potion06.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(856, 14, 6, "Large Mana Potion", "Potion07.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);
  addDef(858, 14, 8, "Antidote", "Antidote01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(859, 14, 9, "Ale", "Potion09.bmd", 1, 2, 0, 0, 0, 0, 0, 15);
  addDef(860, 14, 10, "Town Portal", "Scroll01.bmd", 1, 2, 0, 0, 0, 0, 0, 15);
  addDef(863, 14, 13, "Jewel of Bless", "Jewel01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(864, 14, 14, "Jewel of Soul", "Jewel02.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(866, 14, 16, "Jewel of Life", "Jewel03.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(872, 14, 22, "Jewel of Creation", "Gem01.bmd", 1, 1, 0, 0, 0, 0, 0,
         15);

  // Category 15: Scrolls (IDs 900+)
  addDef(900, 15, 0, "Scroll of Poison", "Book01.bmd", 1, 2, 0, 0, 0, 0, 0, 1);
  addDef(901, 15, 1, "Scroll of Meteorite", "Book02.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(902, 15, 2, "Scroll of Lightning", "Book03.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(903, 15, 3, "Scroll of Fire Ball", "Book04.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(904, 15, 4, "Scroll of Flame", "Book05.bmd", 1, 2, 0, 0, 0, 0, 0, 1);
  addDef(905, 15, 5, "Scroll of Teleport", "Book06.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(906, 15, 6, "Scroll of Ice", "Book07.bmd", 1, 2, 0, 0, 0, 0, 0, 1);
  addDef(907, 15, 7, "Scroll of Twister", "Book08.bmd", 1, 2, 0, 0, 0, 0, 0, 1);
  addDef(908, 15, 8, "Scroll of Evil Spirit", "Book09.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(909, 15, 9, "Scroll of Hellfire", "Book10.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(910, 15, 10, "Scroll of Power Wave", "Book11.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(911, 15, 11, "Scroll of Aqua Beam", "Book12.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(912, 15, 12, "Scroll of Cometfall", "Book13.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);
  addDef(913, 15, 13, "Scroll of Inferno", "Book14.bmd", 1, 2, 0, 0, 0, 0, 0,
         1);

  // ── Additional 0.97d items (Main 5.2 deep dive) ──

  // Missing Swords (0)
  addDef(0, 0, 19, "Sword of Destruction", "Sword20.bmd", 1, 4, 124, 44, 0, 0,
         76, 8, 68, 93);
  addDef(0, 0, 20, "Spirit Sword", "Sword21.bmd", 1, 4, 140, 48, 0, 0, 88, 2,
         92, 112);
  addDef(0, 0, 21, "Dark Master Sword", "Sword22.bmd", 1, 4, 154, 50, 0, 0, 98,
         8, 108, 132);

  // Missing Maces (2)
  addDef(0, 2, 7, "Battle Scepter", "Mace08.bmd", 2, 4, 132, 32, 0, 0, 80, 2,
         85, 110);
  addDef(0, 2, 8, "Master Scepter", "Mace09.bmd", 2, 4, 142, 38, 0, 0, 86, 2,
         92, 126);
  addDef(0, 2, 9, "Great Scepter", "Mace10.bmd", 2, 4, 152, 42, 0, 0, 92, 2,
         105, 140);
  addDef(0, 2, 10, "Lord Scepter", "Mace11.bmd", 2, 4, 158, 44, 0, 0, 96, 2,
         110, 148);
  addDef(0, 2, 11, "Great Lord Scepter", "Mace12.bmd", 2, 4, 164, 48, 0, 0, 100,
         2, 118, 156);
  addDef(0, 2, 12, "Divine Scepter", "Mace13.bmd", 2, 4, 170, 50, 0, 0, 104, 2,
         125, 168);
  addDef(0, 2, 13, "Saint Scepter", "Saint.bmd", 1, 3, 72, 18, 0, 0, 96, 1, 106,
         144);

  // Missing Spears (3)
  addDef(0, 3, 10, "Dragon Spear", "Spear11.bmd", 2, 4, 170, 60, 0, 0, 92, 2,
         112, 140);

  // Missing Bows (4)
  addDef(0, 4, 17, "Celestial Bow", "Bow18.bmd", 2, 4, 54, 198, 0, 0, 92, 4,
         127, 155);
  addDef(0, 4, 18, "Divine CB of Archangel", "CrossBow17.bmd", 2, 3, 40, 110, 0,
         0, 100, 4, 144, 166);

  // Missing Staffs (5)
  addDef(0, 5, 9, "Dragon Soul Staff", "Staff10.bmd", 1, 4, 52, 16, 0, 0, 100,
         1, 46, 48);
  addDef(0, 5, 10, "Staff of Imperial", "Staff11.bmd", 2, 4, 36, 4, 0, 0, 104,
         1, 50, 53);
  addDef(0, 5, 11, "Divine Staff of Archangel", "Staff12.bmd", 2, 4, 36, 4, 0,
         0, 104, 1, 53, 55);

  // Missing Shields (6)
  addDef(0, 6, 15, "Grand Soul Shield", "Shield16.bmd", 2, 3, 70, 23, 0, 0, 74,
         1, 0, 0, 55);
  addDef(0, 6, 16, "Elemental Shield", "Shield17.bmd", 2, 3, 50, 110, 0, 0, 78,
         4, 0, 0, 58);

  // Missing Helms (7) — indices 15-21
  addDef(0, 7, 15, "Storm Crow Helm", "HelmMale11.bmd", 2, 2, 150, 70, 0, 0, 72,
         8, 0, 0, 50);
  addDef(0, 7, 16, "Black Dragon Helm", "HelmMale12.bmd", 2, 2, 170, 60, 0, 0,
         82, 2, 0, 0, 55);
  addDef(0, 7, 17, "Dark Phoenix Helm", "HelmMale13.bmd", 2, 2, 205, 62, 0, 0,
         92, 10, 0, 0, 60);
  addDef(0, 7, 18, "Grand Soul Helm", "HelmClass10.bmd", 2, 2, 59, 20, 0, 0, 81,
         1, 0, 0, 48);
  addDef(0, 7, 19, "Divine Helm", "HelmClass11.bmd", 2, 2, 50, 110, 0, 0, 85, 4,
         0, 0, 52);
  addDef(0, 7, 20, "Thunder Hawk Helm", "HelmMale14.bmd", 2, 2, 150, 70, 0, 0,
         88, 8, 0, 0, 54);
  addDef(0, 7, 21, "Great Dragon Helm", "HelmMale15.bmd", 2, 2, 200, 58, 0, 0,
         104, 10, 0, 0, 66);

  // Missing Armors (8) — indices 15-21
  addDef(0, 8, 15, "Storm Crow Armor", "ArmorMale11.bmd", 2, 3, 150, 70, 0, 0,
         80, 8, 0, 0, 58);
  addDef(0, 8, 16, "Black Dragon Armor", "ArmorMale12.bmd", 2, 3, 170, 60, 0, 0,
         90, 2, 0, 0, 63);
  addDef(0, 8, 17, "Dark Phoenix Armor", "ArmorMale13.bmd", 2, 3, 214, 65, 0, 0,
         100, 10, 0, 0, 70);
  addDef(0, 8, 18, "Grand Soul Armor", "ArmorClass10.bmd", 2, 3, 59, 20, 0, 0,
         91, 1, 0, 0, 52);
  addDef(0, 8, 19, "Divine Armor", "ArmorClass11.bmd", 2, 2, 50, 110, 0, 0, 92,
         4, 0, 0, 56);
  addDef(0, 8, 20, "Thunder Hawk Armor", "ArmorMale14.bmd", 2, 3, 170, 70, 0, 0,
         107, 8, 0, 0, 68);
  addDef(0, 8, 21, "Great Dragon Armor", "ArmorMale15.bmd", 2, 3, 200, 58, 0, 0,
         126, 10, 0, 0, 80);

  // Missing Pants (9) — indices 15-21
  addDef(0, 9, 15, "Storm Crow Pants", "PantMale11.bmd", 2, 2, 150, 70, 0, 0,
         74, 8, 0, 0, 50);
  addDef(0, 9, 16, "Black Dragon Pants", "PantMale12.bmd", 2, 2, 170, 60, 0, 0,
         84, 2, 0, 0, 55);
  addDef(0, 9, 17, "Dark Phoenix Pants", "PantMale13.bmd", 2, 2, 207, 63, 0, 0,
         96, 10, 0, 0, 62);
  addDef(0, 9, 18, "Grand Soul Pants", "PantClass10.bmd", 2, 2, 59, 20, 0, 0,
         86, 1, 0, 0, 48);
  addDef(0, 9, 19, "Divine Pants", "PantClass11.bmd", 2, 2, 50, 110, 0, 0, 88,
         4, 0, 0, 52);
  addDef(0, 9, 20, "Thunder Hawk Pants", "PantMale14.bmd", 2, 2, 150, 70, 0, 0,
         99, 8, 0, 0, 60);
  addDef(0, 9, 21, "Great Dragon Pants", "PantMale15.bmd", 2, 2, 200, 58, 0, 0,
         113, 10, 0, 0, 72);

  // Missing Gloves (10) — indices 15-21
  addDef(0, 10, 15, "Storm Crow Gloves", "GloveMale11.bmd", 2, 2, 150, 70, 0, 0,
         70, 8, 0, 0, 46);
  addDef(0, 10, 16, "Black Dragon Gloves", "GloveMale12.bmd", 2, 2, 170, 60, 0,
         0, 76, 2, 0, 0, 50);
  addDef(0, 10, 17, "Dark Phoenix Gloves", "GloveMale13.bmd", 2, 2, 205, 63, 0,
         0, 86, 10, 0, 0, 56);
  addDef(0, 10, 18, "Grand Soul Gloves", "GloveClass10.bmd", 2, 2, 49, 10, 0, 0,
         70, 1, 0, 0, 44);
  addDef(0, 10, 19, "Divine Gloves", "GloveClass11.bmd", 2, 2, 50, 110, 0, 0,
         72, 4, 0, 0, 48);
  addDef(0, 10, 20, "Thunder Hawk Gloves", "GloveMale14.bmd", 2, 2, 150, 70, 0,
         0, 88, 8, 0, 0, 54);
  addDef(0, 10, 21, "Great Dragon Gloves", "GloveMale15.bmd", 2, 2, 200, 58, 0,
         0, 94, 10, 0, 0, 64);

  // Missing Boots (11) — indices 15-21
  addDef(0, 11, 15, "Storm Crow Boots", "BootMale11.bmd", 2, 2, 150, 70, 0, 0,
         72, 8, 0, 0, 48);
  addDef(0, 11, 16, "Black Dragon Boots", "BootMale12.bmd", 2, 2, 170, 60, 0, 0,
         78, 2, 0, 0, 52);
  addDef(0, 11, 17, "Dark Phoenix Boots", "BootMale13.bmd", 2, 2, 198, 60, 0, 0,
         93, 10, 0, 0, 58);
  addDef(0, 11, 18, "Grand Soul Boots", "BootClass10.bmd", 2, 2, 59, 10, 0, 0,
         76, 1, 0, 0, 44);
  addDef(0, 11, 19, "Divine Boots", "BootClass11.bmd", 2, 2, 50, 110, 0, 0, 81,
         4, 0, 0, 50);
  addDef(0, 11, 20, "Thunder Hawk Boots", "BootMale14.bmd", 2, 2, 150, 70, 0, 0,
         92, 8, 0, 0, 56);
  addDef(0, 11, 21, "Great Dragon Boots", "BootMale15.bmd", 2, 2, 200, 58, 0, 0,
         98, 10, 0, 0, 68);

  // Missing Helpers/Jewelry (13)
  addDef(0, 13, 4, "Dark Horse Horn", "DarkHorseHorn.bmd", 1, 1, 0, 0, 0, 0,
         110, 15);
  addDef(0, 13, 5, "Spirit Bill", "SpiritBill.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 13, 7, "Covenant", "Covenant.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 13, 11, "Summon Book", "SummonBook.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 13, 21, "Fire Ring", "FireRing.bmd", 1, 1, 0, 0, 0, 0, 68, 15);
  addDef(0, 13, 22, "Ground Ring", "GroundRing.bmd", 1, 1, 0, 0, 0, 0, 76, 15);
  addDef(0, 13, 23, "Wind Ring", "WindRing.bmd", 1, 1, 0, 0, 0, 0, 84, 15);
  addDef(0, 13, 24, "Mana Ring", "ManaRing.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 13, 25, "Ice Necklace", "IceNecklace.bmd", 1, 1, 0, 0, 0, 0, 68,
         15);
  addDef(0, 13, 26, "Wind Necklace", "WindNecklace.bmd", 1, 1, 0, 0, 0, 0, 76,
         15);
  addDef(0, 13, 27, "Water Necklace", "WaterNecklace.bmd", 1, 1, 0, 0, 0, 0, 84,
         15);
  addDef(0, 13, 28, "AG Necklace", "AgNecklace.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 13, 29, "Chaos Castle Invitation", "EventChaosCastle.bmd", 1, 1, 0,
         0, 0, 0, 0, 15);

  // Missing Potions/Consumables (14)
  addDef(0, 14, 7, "Special Healing Potion", "SpecialPotion.bmd", 1, 1, 0, 0, 0,
         0, 0, 15);
  addDef(0, 14, 11, "Box of Luck", "MagicBox01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 14, 12, "Heart of Love", "Event01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 14, 15, "Zen", "Gold01.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 14, 17, "Devil Square Key (Bronze)", "Devil00.bmd", 1, 1, 0, 0, 0,
         0, 0, 15);
  addDef(0, 14, 18, "Devil Square Key (Silver)", "Devil01.bmd", 1, 1, 0, 0, 0,
         0, 0, 15);
  addDef(0, 14, 19, "Devil Square Key (Gold)", "Devil02.bmd", 1, 1, 0, 0, 0, 0,
         0, 15);
  addDef(0, 14, 20, "Remedy of Love", "Drink00.bmd", 1, 1, 0, 0, 0, 0, 0, 15);
  addDef(0, 14, 31, "Guardian Angel Scroll", "Suho.bmd", 1, 2, 0, 0, 0, 0, 0,
         15);
}

static const char *GetItemNameByDef(int16_t defIndex) {
  auto it = g_itemDefs.find(defIndex);
  if (it != g_itemDefs.end())
    return it->second.name.c_str();
  return "Item";
}

// Panel layout constants (virtual coords 1280x720)
static constexpr float BASE_PANEL_W = 190.0f;
static constexpr float BASE_PANEL_H = 429.0f;
static constexpr float PANEL_W = BASE_PANEL_W * g_uiPanelScale;
static constexpr float PANEL_H = BASE_PANEL_H * g_uiPanelScale;
static constexpr float PANEL_Y = 20.0f;
static constexpr float PANEL_X_RIGHT = 1270.0f - PANEL_W; // Snug to right

static float GetCharInfoPanelX() { return PANEL_X_RIGHT; }
static float GetInventoryPanelX() {
  return g_showCharInfo ? PANEL_X_RIGHT - PANEL_W - 5.0f : PANEL_X_RIGHT;
}

// Check if virtual point is inside a panel
static bool IsPointInPanel(float vx, float vy, float panelX) {
  return vx >= panelX && vx < panelX + PANEL_W && vy >= PANEL_Y &&
         vy < PANEL_Y + PANEL_H;
}

// Helper: draw textured quad at virtual coords (handles scaling + OZT V-flip)
static void DrawPanelImage(ImDrawList *dl, const UICoords &c,
                           const UITexture &tex, float px, float py, float relX,
                           float relY, float vw, float vh) {
  if (tex.id == 0)
    return;
  float vx = px + relX * g_uiPanelScale;
  float vy = py + relY * g_uiPanelScale;
  float sw = vw * g_uiPanelScale;
  float sh = vh * g_uiPanelScale;

  ImVec2 pMin(c.ToScreenX(vx), c.ToScreenY(vy));
  ImVec2 pMax(c.ToScreenX(vx + sw), c.ToScreenY(vy + sh));
  ImVec2 uvMin(0, 0), uvMax(1, 1);
  if (tex.isOZT) {
    uvMin.y = 1.0f;
    uvMax.y = 0.0f;
  } // V-flip for OZT
  dl->AddImage((ImTextureID)(uintptr_t)tex.id, pMin, pMax, uvMin, uvMax);
}

// Helper: draw centered text with shadow (handles scaling)
static void DrawPanelText(ImDrawList *dl, const UICoords &c, float px, float py,
                          float relX, float relY, const char *text, ImU32 color,
                          ImFont *font = nullptr) {
  float vx = px + relX * g_uiPanelScale;
  float vy = py + relY * g_uiPanelScale;
  float sx = c.ToScreenX(vx), sy = c.ToScreenY(vy);
  float fs = (font ? font->LegacySize : 13.0f);

  if (font) {
    dl->AddText(font, fs, ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 180), text);
    dl->AddText(font, fs, ImVec2(sx, sy), color, text);
  } else {
    dl->AddText(ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 180), text);
    dl->AddText(ImVec2(sx, sy), color, text);
  }
}

// Helper: draw right-aligned text (handles scaling)
static void DrawPanelTextRight(ImDrawList *dl, const UICoords &c, float px,
                               float py, float relX, float relY, float width,
                               const char *text, ImU32 color) {
  float vx = px + relX * g_uiPanelScale;
  float vy = py + relY * g_uiPanelScale;
  float sw = width * g_uiPanelScale;
  ImVec2 sz = ImGui::CalcTextSize(text);
  float sx = c.ToScreenX(vx + sw) - sz.x;
  float sy = c.ToScreenY(vy);
  dl->AddText(ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 180), text);
  dl->AddText(ImVec2(sx, sy), color, text);
}

// Helper: draw centered text horizontally (handles scaling)
static void DrawPanelTextCentered(ImDrawList *dl, const UICoords &c, float px,
                                  float py, float relX, float relY, float width,
                                  const char *text, ImU32 color,
                                  ImFont *font = nullptr) {
  float vx = px + relX * g_uiPanelScale;
  float vy = py + relY * g_uiPanelScale;
  float sw = width * g_uiPanelScale;
  float fs = (font ? font->LegacySize : 13.0f);

  ImVec2 sz;
  if (font) {
    sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
  } else {
    sz = ImGui::CalcTextSize(text);
  }

  // vx + sw*0.5 is the virtual center of the area
  float sx = c.ToScreenX(vx + sw * 0.5f) - sz.x * 0.5f;
  float sy = c.ToScreenY(vy);

  if (font) {
    dl->AddText(font, fs, ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 180), text);
    dl->AddText(font, fs, ImVec2(sx, sy), color, text);
  } else {
    dl->AddText(ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 180), text);
    dl->AddText(ImVec2(sx, sy), color, text);
  }
}

// RenderTableDecorations removed

static void RenderCharInfoPanel(ImDrawList *dl, const UICoords &c) {
  float px = GetCharInfoPanelX(), py = PANEL_Y;
  float pw = PANEL_W,
        ph = PANEL_H +
             25.0f * g_uiPanelScale; // Extend by 25 pixels to fit combat stats

  // Colors
  const ImU32 colBg = IM_COL32(15, 15, 25, 240);
  const ImU32 colBr = IM_COL32(60, 65, 90, 200);
  const ImU32 colTitle = IM_COL32(255, 210, 80, 255);
  const ImU32 colLabel = IM_COL32(170, 170, 190, 255);
  const ImU32 colValue = IM_COL32(255, 255, 255, 255);
  const ImU32 colGreen = IM_COL32(100, 255, 100, 255);
  char buf[256];

  // Simple Rect Background
  dl->AddRectFilled(ImVec2(c.ToScreenX(px), c.ToScreenY(py)),
                    ImVec2(c.ToScreenX(px + pw), c.ToScreenY(py + ph)), colBg,
                    5.0f);
  dl->AddRect(ImVec2(c.ToScreenX(px), c.ToScreenY(py)),
              ImVec2(c.ToScreenX(px + pw), c.ToScreenY(py + ph)), colBr, 5.0f,
              0, 1.5f);

  // Title
  DrawPanelTextCentered(dl, c, px, py, 0, 11, BASE_PANEL_W, "Character Info",
                        colTitle, g_fontDefault);

  // Close button visual
  {
    float bx = BASE_PANEL_W - 24;
    float by = 6;
    float bw = 16, bh = 14;
    ImVec2 bMin(c.ToScreenX(px + bx * g_uiPanelScale),
                c.ToScreenY(py + by * g_uiPanelScale));
    ImVec2 bMax(c.ToScreenX(px + (bx + bw) * g_uiPanelScale),
                c.ToScreenY(py + (by + bh) * g_uiPanelScale));
    dl->AddRectFilled(bMin, bMax, IM_COL32(100, 20, 20, 200), 2.0f);

    ImVec2 xSize = ImGui::CalcTextSize("X");
    ImVec2 xPos(bMin.x + (bMax.x - bMin.x) * 0.5f - xSize.x * 0.5f,
                bMin.y + (bMax.y - bMin.y) * 0.5f - xSize.y * 0.5f);
    dl->AddText(xPos, colValue, "X");
  }

  // Basic Info - Stacked for better readability
  DrawPanelText(dl, c, px, py, 20, 45, "Name", colLabel);
  DrawPanelTextRight(dl, c, px, py, 20, 45, 145, "TestDK", colValue);

  DrawPanelText(dl, c, px, py, 20, 65, "Class", colLabel);
  DrawPanelTextRight(dl, c, px, py, 20, 65, 145, "Dark Knight", colValue);

  DrawPanelText(dl, c, px, py, 20, 85, "Level", colLabel);
  snprintf(buf, sizeof(buf), "%d", g_serverLevel);
  DrawPanelTextRight(dl, c, px, py, 20, 85, 145, buf, colGreen);

  // XP bar (Simplified)
  float xpFrac = 0.0f;
  uint64_t nextXp = g_hero.GetNextExperience();
  uint64_t curXp = (uint64_t)g_serverXP;
  uint64_t prevXp = g_hero.CalcXPForLevel(g_serverLevel);
  if (nextXp > prevXp)
    xpFrac = (float)(curXp - prevXp) / (float)(nextXp - prevXp);
  xpFrac = std::clamp(xpFrac, 0.0f, 1.0f);

  float barX = 15, barY = 115, barW = 160, barH = 5;
  dl->AddRectFilled(ImVec2(c.ToScreenX(px + barX * g_uiPanelScale),
                           c.ToScreenY(py + barY * g_uiPanelScale)),
                    ImVec2(c.ToScreenX(px + (barX + barW) * g_uiPanelScale),
                           c.ToScreenY(py + (barY + barH) * g_uiPanelScale)),
                    IM_COL32(20, 20, 30, 255));
  if (xpFrac > 0.0f) {
    dl->AddRectFilled(
        ImVec2(c.ToScreenX(px + barX * g_uiPanelScale),
               c.ToScreenY(py + barY * g_uiPanelScale)),
        ImVec2(c.ToScreenX(px + (barX + barW * xpFrac) * g_uiPanelScale),
               c.ToScreenY(py + (barY + barH) * g_uiPanelScale)),
        IM_COL32(40, 180, 80, 255));
  }

  // Stats (Simplified, no custom textboxes)
  const char *statLabels[] = {"Strength", "Agility", "Vitality", "Energy"};
  int statValues[] = {g_serverStr, g_serverDex, g_serverVit, g_serverEne};
  float statYOffsets[] = {150, 182, 214, 246};

  for (int i = 0; i < 4; i++) {
    float ry = statYOffsets[i];
    dl->AddRectFilled(ImVec2(c.ToScreenX(px + 15 * g_uiPanelScale),
                             c.ToScreenY(py + ry * g_uiPanelScale)),
                      ImVec2(c.ToScreenX(px + 175 * g_uiPanelScale),
                             c.ToScreenY(py + (ry + 22) * g_uiPanelScale)),
                      IM_COL32(30, 35, 50, 255), 2.0f);

    DrawPanelText(dl, c, px, py, 25, ry + 4, statLabels[i], colLabel);
    snprintf(buf, sizeof(buf), "%d", statValues[i]);
    DrawPanelTextRight(dl, c, px, py, 25, ry + 4, 120, buf, colValue);

    if (g_serverLevelUpPoints > 0) {
      dl->AddRectFilled(ImVec2(c.ToScreenX(px + 155 * g_uiPanelScale),
                               c.ToScreenY(py + (ry + 2) * g_uiPanelScale)),
                        ImVec2(c.ToScreenX(px + 173 * g_uiPanelScale),
                               c.ToScreenY(py + (ry + 20) * g_uiPanelScale)),
                        IM_COL32(50, 150, 50, 255), 2.0f);
      DrawPanelText(dl, c, px, py, 158, ry + 3, "+", colValue);
    }
  }

  if (g_serverLevelUpPoints > 0) {
    snprintf(buf, sizeof(buf), "Points: %d", g_serverLevelUpPoints);
    DrawPanelText(dl, c, px, py, 15, 272, buf, colGreen);
  }

  // Combat Info
  int dMin = g_serverStr / 8 + g_hero.GetWeaponBonusMin();
  int dMax = g_serverStr / 4 + g_hero.GetWeaponBonusMax();

  // Draw Damage
  snprintf(buf, sizeof(buf), "Damage: %d - %d", dMin, dMax);
  DrawPanelText(dl, c, px, py, 15, 300, buf, colValue);

  // Draw Defense is handled below to include additional defense

  // Draw Attack Speed
  snprintf(buf, sizeof(buf), "Atk Speed: %d", g_serverAttackSpeed);
  DrawPanelText(dl, c, px, py, 15, 330, buf, colValue);

  // Draw Magic Speed (if class uses magic)
  snprintf(buf, sizeof(buf), "Mag Speed: %d", g_serverMagicSpeed);
  DrawPanelText(dl, c, px, py, 15, 345, buf, colValue);

  // Draw Additional Combat Stats
  int critRate = std::min((int)g_serverDex / 5, 20); // capped 20%
  int excRate = std::min((int)g_serverDex / 10, 10); // capped 10%

  snprintf(buf, sizeof(buf), "Crit: %d%%", critRate);
  DrawPanelText(dl, c, px, py, 15, 360, buf,
                IM_COL32(100, 200, 255, 255)); // Blue

  snprintf(buf, sizeof(buf), "Exc: %d%%", excRate);
  DrawPanelText(dl, c, px, py, 100, 360, buf,
                IM_COL32(100, 255, 100, 255)); // Green

  int baseDef = g_serverDefense - g_hero.GetDefenseBonus();
  int addDef = g_hero.GetDefenseBonus();
  if (addDef > 0) {
    snprintf(buf, sizeof(buf), "Defense: %d + %d", baseDef, addDef);
    DrawPanelText(dl, c, px, py, 15, 315, buf, colValue);
  } else {
    snprintf(buf, sizeof(buf), "Defense: %d", g_serverDefense);
    DrawPanelText(dl, c, px, py, 15, 315, buf, colValue);
  }

  // HP / MP Bars (Authoritative from g_hero)
  int curHP = g_hero.GetHP();
  int maxHP = g_hero.GetMaxHP();
  int curMP = g_hero.GetMana();
  int maxMP = g_hero.GetMaxMana();

  float hpFrac = (maxHP > 0) ? (float)curHP / maxHP : 0.0f;
  float mpFrac = (maxMP > 0) ? (float)curMP / maxMP : 0.0f;

  float hbarX = 50, hbarY = 385, hbarW = 100, hbarH = 8;
  DrawPanelText(dl, c, px, py, 15, hbarY - 2, "HP", colLabel);
  dl->AddRectFilled(ImVec2(c.ToScreenX(px + hbarX * g_uiPanelScale),
                           c.ToScreenY(py + hbarY * g_uiPanelScale)),
                    ImVec2(c.ToScreenX(px + (hbarX + hbarW) * g_uiPanelScale),
                           c.ToScreenY(py + (hbarY + hbarH) * g_uiPanelScale)),
                    IM_COL32(50, 20, 20, 255));
  if (hpFrac > 0.0f) {
    dl->AddRectFilled(
        ImVec2(c.ToScreenX(px + hbarX * g_uiPanelScale),
               c.ToScreenY(py + hbarY * g_uiPanelScale)),
        ImVec2(c.ToScreenX(px + (hbarX + hbarW * hpFrac) * g_uiPanelScale),
               c.ToScreenY(py + (hbarY + hbarH) * g_uiPanelScale)),
        IM_COL32(200, 30, 30, 255));
  }
  snprintf(buf, sizeof(buf), "%d / %d", curHP, maxHP);
  DrawPanelTextRight(dl, c, px, py, hbarX, hbarY - 3, hbarW, buf, colValue);

  float mbarY = 405;
  DrawPanelText(dl, c, px, py, 15, mbarY - 2, "MP", colLabel);
  dl->AddRectFilled(ImVec2(c.ToScreenX(px + hbarX * g_uiPanelScale),
                           c.ToScreenY(py + mbarY * g_uiPanelScale)),
                    ImVec2(c.ToScreenX(px + (hbarX + hbarW) * g_uiPanelScale),
                           c.ToScreenY(py + (mbarY + hbarH) * g_uiPanelScale)),
                    IM_COL32(20, 20, 80, 255));
  if (mpFrac > 0.0f) {
    dl->AddRectFilled(
        ImVec2(c.ToScreenX(px + hbarX * g_uiPanelScale),
               c.ToScreenY(py + mbarY * g_uiPanelScale)),
        ImVec2(c.ToScreenX(px + (hbarX + hbarW * mpFrac) * g_uiPanelScale),
               c.ToScreenY(py + (mbarY + hbarH) * g_uiPanelScale)),
        IM_COL32(40, 40, 220, 255));
  }
  snprintf(buf, sizeof(buf), "%d / %d", curMP, maxMP);
  DrawPanelTextRight(dl, c, px, py, hbarX, mbarY - 3, hbarW, buf, colValue);
}

static void RenderInventoryPanel(ImDrawList *dl, const UICoords &c) {
  float px = GetInventoryPanelX(), py = PANEL_Y;
  float pw = PANEL_W, ph = PANEL_H;
  ImVec2 mp = ImGui::GetIO().MousePos;

  // Colors
  const ImU32 colBg = IM_COL32(15, 15, 25, 240);
  const ImU32 colBr = IM_COL32(60, 65, 90, 200);
  const ImU32 colTitle = IM_COL32(255, 210, 80, 255);
  const ImU32 colHeader = IM_COL32(200, 180, 120, 255);
  const ImU32 colSlotBg = IM_COL32(0, 0, 0, 150);
  const ImU32 colSlotBr = IM_COL32(80, 75, 60, 255);
  const ImU32 colGold = IM_COL32(255, 215, 0, 255);
  const ImU32 colValue = IM_COL32(255, 255, 255, 255);
  const ImU32 colDragHi = IM_COL32(255, 255, 0, 100);
  char buf[256];

  // Simple Rect Background
  dl->AddRectFilled(ImVec2(c.ToScreenX(px), c.ToScreenY(py)),
                    ImVec2(c.ToScreenX(px + pw), c.ToScreenY(py + ph)), colBg,
                    5.0f);
  dl->AddRect(ImVec2(c.ToScreenX(px), c.ToScreenY(py)),
              ImVec2(c.ToScreenX(px + pw), c.ToScreenY(py + ph)), colBr, 5.0f,
              0, 1.5f);

  // Title (reduced width to avoid close button overlap)
  DrawPanelTextCentered(dl, c, px, py, 0, 11, BASE_PANEL_W, "Inventory",
                        colTitle, g_fontDefault);

  // Close button visual
  {
    float bx = BASE_PANEL_W - 24;
    float by = 6;
    float bw = 16, bh = 14;
    ImVec2 bMin(c.ToScreenX(px + bx * g_uiPanelScale),
                c.ToScreenY(py + by * g_uiPanelScale));
    ImVec2 bMax(c.ToScreenX(px + (bx + bw) * g_uiPanelScale),
                c.ToScreenY(py + (by + bh) * g_uiPanelScale));
    dl->AddRectFilled(bMin, bMax, IM_COL32(100, 20, 20, 200), 2.0f);

    // Centered "X" inside the box
    ImVec2 xSize = ImGui::CalcTextSize("X");
    ImVec2 xPos(bMin.x + (bMax.x - bMin.x) * 0.5f - xSize.x * 0.5f,
                bMin.y + (bMax.y - bMin.y) * 0.5f - xSize.y * 0.5f);
    dl->AddText(xPos, colValue, "X");
  }

  // Equipment Layout
  for (auto &ep : g_equipLayoutRects) {
    float vx = px + ep.rx * g_uiPanelScale;
    float vy = py + ep.ry * g_uiPanelScale;
    float sw = ep.rw * g_uiPanelScale;
    float sh = ep.rh * g_uiPanelScale;

    ImVec2 sMin(c.ToScreenX(vx), c.ToScreenY(vy));
    ImVec2 sMax(c.ToScreenX(vx + sw), c.ToScreenY(vy + sh));

    bool hoverSlot =
        mp.x >= sMin.x && mp.x < sMax.x && mp.y >= sMin.y && mp.y < sMax.y;

    dl->AddRectFilled(sMin, sMax, colSlotBg, 3.0f);

    // Draw Background Placeholder if empty
    if (!g_equipSlots[ep.slot].equipped && g_slotBackgrounds[ep.slot] != 0) {
      dl->AddImage((ImTextureID)(intptr_t)g_slotBackgrounds[ep.slot], sMin,
                   sMax);
    }

    dl->AddRect(sMin, sMax, hoverSlot && g_isDragging ? colDragHi : colSlotBr,
                3.0f);

    bool isBeingDragged = (g_isDragging && g_dragFromEquipSlot == ep.slot);

    if (g_equipSlots[ep.slot].equipped && !isBeingDragged) {
      std::string modelName = g_equipSlots[ep.slot].modelFile;
      if (!modelName.empty()) {
        int winH = (int)ImGui::GetIO().DisplaySize.y;
        int16_t defIdx = GetDefIndexFromCategory(
            g_equipSlots[ep.slot].category, g_equipSlots[ep.slot].itemIndex);
        g_renderQueue.push_back({modelName, defIdx, (int)sMin.x,
                                 winH - (int)sMax.y, (int)(sMax.x - sMin.x),
                                 (int)(sMax.y - sMin.y), hoverSlot});
      }
      if (hoverSlot) {
        AddPendingItemTooltip(
            GetDefIndexFromCategory(g_equipSlots[ep.slot].category,
                                    g_equipSlots[ep.slot].itemIndex),
            g_equipSlots[ep.slot].itemLevel);
      }
      // Always show +Level overlay if > 0
      if (g_equipSlots[ep.slot].itemLevel > 0) {
        char lvlBuf[8];
        snprintf(lvlBuf, sizeof(lvlBuf), "+%d",
                 g_equipSlots[ep.slot].itemLevel);
        dl->AddText(ImVec2(sMin.x + 2, sMin.y + 2), IM_COL32(255, 200, 80, 255),
                    lvlBuf);
      }
    }
  }

  // Bag Grid
  DrawPanelText(dl, c, px, py, 15, 198, "Bag", colHeader);
  float gridRX = 15.0f, gridRY = 208.0f;
  float cellW = 20.0f, cellH = 20.0f;
  float gap = 0.0f;

  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      float rX = gridRX + col * (cellW + gap);
      float rY = gridRY + row * (cellH + gap);
      float vX = px + rX * g_uiPanelScale;
      float vY = py + rY * g_uiPanelScale;
      float sW = cellW * g_uiPanelScale;
      float sH = cellH * g_uiPanelScale;

      ImVec2 sMin(c.ToScreenX(vX), c.ToScreenY(vY));
      ImVec2 sMax(c.ToScreenX(vX + sW), c.ToScreenY(vY + sH));
      dl->AddRectFilled(sMin, sMax, colSlotBg, 1.0f);
      dl->AddRect(sMin, sMax, colSlotBr, 1.0f);
    }
  }

  // Bag Items
  bool processed[INVENTORY_SLOTS] = {false};
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int slot = row * 8 + col;
      if (processed[slot])
        continue;

      bool isThisBeingDragged = (g_isDragging && g_dragFromSlot == slot);

      if (g_inventory[slot].occupied) {
        auto it = g_itemDefs.find(g_inventory[slot].defIndex);
        if (it != g_itemDefs.end()) {
          const auto &def = it->second;
          // Mark ENTIRE footprint as processed regardless of whether we render
          // it
          for (int hh = 0; hh < def.height; hh++)
            for (int ww = 0; ww < def.width; ww++)
              if (slot + hh * 8 + ww < INVENTORY_SLOTS)
                processed[slot + hh * 8 + ww] = true;

          if (isThisBeingDragged)
            continue; // Skip rendering visual of the item AT source slot

          float rX = gridRX + col * (cellW + gap);
          float rY = gridRY + row * (cellH + gap);
          float vX = px + rX * g_uiPanelScale;
          float vY = py + rY * g_uiPanelScale;
          ImVec2 iMin(c.ToScreenX(vX), c.ToScreenY(vY));
          ImVec2 iMax(c.ToScreenX(vX + def.width * cellW * g_uiPanelScale),
                      c.ToScreenY(vY + def.height * cellH * g_uiPanelScale));
          bool hoverItem = mp.x >= iMin.x && mp.x < iMax.x && mp.y >= iMin.y &&
                           mp.y < iMax.y;

          if (hoverItem)
            dl->AddRectFilled(iMin, iMax, IM_COL32(255, 255, 255, 30), 2.0f);
          else
            dl->AddRectFilled(iMin, iMax, IM_COL32(0, 0, 0, 40), 2.0f);

          const char *model = def.modelFile.empty()
                                  ? GetDropModelName(g_inventory[slot].defIndex)
                                  : def.modelFile.c_str();
          if (model && model[0]) {
            int winH = (int)ImGui::GetIO().DisplaySize.y;
            g_renderQueue.push_back({model, g_inventory[slot].defIndex,
                                     (int)iMin.x, winH - (int)iMax.y,
                                     (int)(iMax.x - iMin.x),
                                     (int)(iMax.y - iMin.y), hoverItem});
          }
          if (hoverItem && !g_isDragging)
            AddPendingItemTooltip(g_inventory[slot].defIndex,
                                  g_inventory[slot].itemLevel);

          // Level overlay
          if (g_inventory[slot].itemLevel > 0) {
            char lvlBuf[8];
            snprintf(lvlBuf, sizeof(lvlBuf), "+%d",
                     g_inventory[slot].itemLevel);
            dl->AddText(ImVec2(iMin.x + 2, iMin.y + 2),
                        IM_COL32(255, 200, 80, 255), lvlBuf);
          }
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
      float gridVX = px + gridRX * g_uiPanelScale;
      float gridVY = py + gridRY * g_uiPanelScale;
      float gridVW = 8 * cellW * g_uiPanelScale;
      float gridVH = 8 * cellH * g_uiPanelScale;

      // Check if mouse is over the bag grid
      if (mp.x >= c.ToScreenX(gridVX) && mp.x < c.ToScreenX(gridVX + gridVW) &&
          mp.y >= c.ToScreenY(gridVY) && mp.y < c.ToScreenY(gridVY + gridVH)) {
        // Compute which cell the mouse is over
        float localX = (mp.x - c.ToScreenX(gridVX)) /
                       (c.ToScreenX(gridVX + cellW * g_uiPanelScale) -
                        c.ToScreenX(gridVX));
        float localY = (mp.y - c.ToScreenY(gridVY)) /
                       (c.ToScreenY(gridVY + cellH * g_uiPanelScale) -
                        c.ToScreenY(gridVY));
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
          float ox = px + (gridRX + hCol * cellW) * g_uiPanelScale;
          float oy = py + (gridRY + hRow * cellH) * g_uiPanelScale;
          float ow = iw * cellW * g_uiPanelScale;
          float oh = ih * cellH * g_uiPanelScale;
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
      g_renderQueue.push_back({def.modelFile, g_dragDefIndex, (int)iMin.x,
                               winH - (int)iMax.y, (int)dw, (int)dh, false});

      if (g_dragItemLevel > 0)
        snprintf(buf, sizeof(buf), "%s +%d", def.name.c_str(), g_dragItemLevel);
      else
        snprintf(buf, sizeof(buf), "%s", def.name.c_str());
      dl->AddText(ImVec2(iMin.x, iMax.y + 2), colGold, buf);
    }
  }

  // Tooltip on hover (bag items) — use GetForegroundDrawList for top z-order
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int slot = row * 8 + col;
      if (!g_inventory[slot].occupied || !g_inventory[slot].primary)
        continue;

      auto it = g_itemDefs.find(g_inventory[slot].defIndex);
      int dw = 1, dh = 1;
      if (it != g_itemDefs.end()) {
        dw = it->second.width;
        dh = it->second.height;
      }
      float rX = gridRX + col * cellW;
      float rY = gridRY + row * cellH;
      float vX = px + rX * g_uiPanelScale;
      float vY = py + rY * g_uiPanelScale;
      ImVec2 iMin(c.ToScreenX(vX), c.ToScreenY(vY));
      ImVec2 iMax(c.ToScreenX(vX + dw * cellW * g_uiPanelScale),
                  c.ToScreenY(vY + dh * cellH * g_uiPanelScale));

      if (mp.x >= iMin.x && mp.x < iMax.x && mp.y >= iMin.y && mp.y < iMax.y &&
          !g_isDragging) {
        AddPendingItemTooltip(g_inventory[slot].defIndex,
                              g_inventory[slot].itemLevel);
      }
    }
  }

  // Zen display at the bottom
  {
    dl->AddRectFilled(ImVec2(c.ToScreenX(px + 10 * g_uiPanelScale),
                             c.ToScreenY(py + 400 * g_uiPanelScale)),
                      ImVec2(c.ToScreenX(px + 180 * g_uiPanelScale),
                             c.ToScreenY(py + 424 * g_uiPanelScale)),
                      IM_COL32(20, 25, 40, 255), 3.0f);
    char zenBuf[64];
    std::string s = std::to_string(g_zen);
    int n = s.length() - 3;
    while (n > 0) {
      s.insert(n, ",");
      n -= 3;
    }
    snprintf(zenBuf, sizeof(zenBuf), "%s Zen", s.c_str());
    DrawPanelTextRight(dl, c, px, py, 10, 405, 160, zenBuf, colGold);
  }
}
// End of RenderInventoryPanel

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

  // Class check: bit_mask = 1 << (char_class >> 4)
  // Mapping: 0(DW) -> bit 0, 16(DK) -> bit 1, 32(Elf) -> bit 2
  int bitIndex = g_hero.GetClass() >> 4;
  if (!(def.classFlags & (1 << bitIndex))) {
    std::cout << "[UI] This item cannot be equipped by your class! (Class:"
              << (int)g_hero.GetClass() << " Bit:" << bitIndex << " Flags:0x"
              << std::hex << def.classFlags << std::dec << ")" << std::endl;
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
        // it), we need to be careful. But we usually clear the old slots
        // before checking fit for a new move.
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
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;

    // Close button (relative: 190 - 24, 6, size 16, 12)
    if (relX >= 190 - 24 && relX < 190 - 8 && relY >= 6 && relY < 18) {
      g_showCharInfo = false;
      return true;
    }

    // Stat "+" buttons
    float statRowYOffsets[] = {150, 182, 214, 246};
    if (g_serverLevelUpPoints > 0) {
      for (int i = 0; i < 4; i++) {
        float btnX = 155, btnY = statRowYOffsets[i] + 2;
        if (relX >= btnX && relX < btnX + 18 && relY >= btnY &&
            relY < btnY + 18) {
          g_server.SendStatAlloc(static_cast<uint8_t>(i));
          return true;
        }
      }
    }
    return true; // Click consumed by panel
  }

  // Inventory panel
  if (g_showInventory && IsPointInPanel(vx, vy, GetInventoryPanelX())) {
    float px = GetInventoryPanelX(), py = PANEL_Y;
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;

    // Close button
    if (relX >= 190 - 24 && relX < 190 - 8 && relY >= 6 && relY < 18) {
      g_showInventory = false;
      return true;
    }

    // Inventory Action Buttons removed

    // Equipment slots: start drag
    // Equipment slots: start drag
    for (const auto &ep : g_equipLayoutRects) {
      if (relX >= ep.rx && relX < ep.rx + ep.rw && relY >= ep.ry &&
          relY < ep.ry + ep.rh) {
        if (g_equipSlots[ep.slot].equipped) {
          g_dragFromSlot = -1; // Flag for equipping vs inventory
          g_dragFromEquipSlot = ep.slot;

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
    float gridRX = 15.0f, gridRY = 208.0f;
    float cellW = 20.0f, cellH = 20.0f, gap = 0.0f;
    // (relClickX/Y already declared above)

    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        int slot = row * 8 + col;
        float cx = gridRX + col * (cellW + gap);
        float cy = gridRY + row * (cellH + gap);
        if (relX >= cx && relX < cx + cellW && relY >= cy &&
            relY < cy + cellH) {
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

  // Quick Slot (HUD area) - bottom center
  int winW, winH;
  glfwGetWindowSize(glfwGetCurrentContext(), &winW, &winH);
  if (vy >= g_hudCoords.ToVirtualY((float)winH - 60)) {
    // Center is 640 in virtual space (1280 wide)
    if (vx >= 615 && vx <= 665) {
      if (g_quickSlotDefIndex != -1) {
        g_isDragging = true;
        g_dragFromQuickSlot = true;
        g_dragDefIndex = g_quickSlotDefIndex;
        g_dragFromSlot = -1;
        g_dragFromEquipSlot = -1;
        std::cout << "[QuickSlot] Started dragging from Q" << std::endl;
        return true;
      }
    }
  }

  return false;
}

// Handle drag release (mouse up)
static void HandlePanelMouseUp(GLFWwindow *window, float vx, float vy) {
  if (!g_isDragging)
    return;
  bool wasDragging = g_isDragging;
  g_isDragging = false;

  int winW, winH;
  glfwGetWindowSize(window, &winW, &winH);
  bool droppedOnHUD = (vy >= g_hudCoords.ToVirtualY((float)winH - 60));

  if (g_dragFromQuickSlot) {
    if (!droppedOnHUD) {
      g_quickSlotDefIndex = -1;
      std::cout << "[QuickSlot] Cleared assignment (dragged out)" << std::endl;
    }
    g_dragFromQuickSlot = false;
    return;
  }

  if (g_showInventory) {
    float px = GetInventoryPanelX(), py = PANEL_Y;
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;

    // 1. Check drop on Equipment slots
    for (const auto &ep : g_equipLayoutRects) {
      if (relX >= ep.rx && relX < ep.rx + ep.rw && relY >= ep.ry &&
          relY < ep.ry + ep.rh) {
        // Dragging FROM Inventory TO Equipment
        if (g_dragFromSlot >= 0) {
          if (!CanEquipItem(g_dragDefIndex)) {
            g_isDragging = false;
            g_dragFromSlot = -1;
            return;
          }

          uint8_t cat, idx;
          GetItemCategoryAndIndex(g_dragDefIndex, cat, idx);

          // Enforce Strict Slot Category Compatibility (Main 5.2 logic)
          bool validSlot = false;
          switch (ep.slot) {
          case 0: // R.Hand: Sword(0), Axe(1), Mace(2), Spear(3), Staff(5),
                  // Bow(4)
            validSlot = (cat <= 5);
            break;
          case 1: // L.Hand: Shield(6), Bow(4), Staff(5), Sword(0), Axe(1),
                  // Mace(2), Spear(3)
            // (Note: Shields are cat 6. Dual wielding allowed if cat <= 3)
            validSlot = (cat <= 6);
            break;
          case 2:
            validSlot = (cat == 7);
            break; // Helm
          case 3:
            validSlot = (cat == 8);
            break; // Armor
          case 4:
            validSlot = (cat == 9);
            break; // Pants
          case 5:
            validSlot = (cat == 10);
            break; // Gloves
          case 6:
            validSlot = (cat == 11);
            break; // Boots
          case 7:
            validSlot = (cat == 12 && idx <= 6);
            break; // Wings
          case 8:
            validSlot = (cat == 13 && (idx == 0 || idx == 1 || idx == 2 ||
                                       idx == 3)); // Guardian/Pet
            break;
          case 9:
            validSlot = (cat == 13 && idx >= 8 && idx <= 13);
            break; // Pendant
          case 10:
          case 11:
            validSlot = (cat == 13 && idx >= 20 && idx <= 25);
            break; // Rings
          }

          if (!validSlot) {
            std::cout << "[UI] Cannot equip category " << (int)cat
                      << " in slot " << ep.slot << std::endl;
            g_isDragging = false;
            g_dragFromSlot = -1;
            return;
          }

          // Enforce Hand Compatibility
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
          if (g_syncDone) {
            g_server.SendEquip(1, static_cast<uint8_t>(ep.slot), cat, idx,
                               g_dragItemLevel);
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

    // 2. Check drop on Quick Slot area (bottom bar)
    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);
    if (vy >= g_hudCoords.ToVirtualY((float)winH - 60)) {
      auto it = g_itemDefs.find(g_dragDefIndex);
      if (it != g_itemDefs.end() && it->second.category == 14) {
        g_quickSlotDefIndex = g_dragDefIndex;
        std::cout << "[QuickSlot] Assigned " << it->second.name << " to Q"
                  << std::endl;
        return;
      }
    }

    // 3. Check drop on Bag grid
    float gridRX = 15.0f, gridRY = 208.0f;
    float cellW = 20.0f, cellH = 20.0f;

    if (relX >= gridRX && relX < gridRX + 8 * cellW && relY >= gridRY &&
        relY < gridRY + 8 * cellH) {
      int col = (int)((relX - gridRX) / cellW);
      int row = (int)((relY - gridRY) / cellH);
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
            if (g_syncDone)
              g_server.SendUnequip(1, static_cast<uint8_t>(g_dragFromEquipSlot));

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
            if (g_syncDone)
              g_server.SendInventoryMove(static_cast<uint8_t>(g_dragFromSlot),
                                         static_cast<uint8_t>(targetSlot));

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

  struct StreamRedirector {
    std::streambuf *origCout, *origCerr;
    TeeStreambuf *coutTee, *cerrTee;
    StreamRedirector(std::streambuf *oc, std::streambuf *oce, TeeStreambuf *ct,
                     TeeStreambuf *cet)
        : origCout(oc), origCerr(oce), coutTee(ct), cerrTee(cet) {}
    ~StreamRedirector() {
      if (origCout)
        std::cout.rdbuf(origCout);
      if (origCerr)
        std::cerr.rdbuf(origCerr);
      delete coutTee;
      delete cerrTee;
    }
  } redirector(origCout, origCerr, coutTee, cerrTee);

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
      1366, 768, "Mu Online Remaster (Native macOS C++)", nullptr, nullptr);
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
  glfwSetKeyCallback(window,
                     key_callback); // Register keyboard hotkeys (I, C, Escape)
  glfwSetCharCallback(window, char_callback);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Load fonts for high-fidelity UI
  float contentScale = 1.0f;
  {
    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    contentScale = xscale;
  }
  {
    ImFontConfig cfg;
    // ProggyClean is sharpest at 13px multiples
    g_fontDefault = io.Fonts->AddFontFromFileTTF(
        "external/imgui/misc/fonts/ProggyClean.ttf", 13.0f * contentScale);
    if (!g_fontDefault)
      g_fontDefault = io.Fonts->AddFontDefault(&cfg);

    g_fontBold = io.Fonts->AddFontFromFileTTF(
        "external/imgui/misc/fonts/ProggyClean.ttf", 15.0f * contentScale);
    if (!g_fontBold)
      g_fontBold = g_fontDefault;

    io.Fonts->Build();
  }

  // Initialize modern HUD (centered at 70% scale)
  g_hudCoords.window = window;
  g_hudCoords.SetCenteredScale(0.7f);

  std::string hudAssetPath = "../lab-studio/modern-ui/assets";
  // --- Main Render Loop ---

  // Main Loop logic continues...

  MockData hudData = MockData::CreateDK50();

  // Load Terrain for testing
  std::string data_path = g_dataPath;
  TerrainData terrainData = TerrainParser::LoadWorld(1, data_path);

  // Reconstruct TW_NOGROUND for bridge cells.
  // The .att file for this data version lacks these flags (verified: 0
  // cells). Original engine reads them from .att (ZzzLodTerrain.cpp:1665). We
  // reconstruct from bridge objects (type 80) with orientation awareness.
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

  // Starting character initialization: empty inventory for realistic testing
  // Initial stats for Level 1 DK
  g_hero.LoadStats(1, 28, 20, 25, 10, 0, 0, 110, 110, 20, 20, 1);
  g_hero.SetTerrainLightmap(terrainData.lightmap);
  g_hero.SetPointLights(g_pointLights);
  g_hero.SnapToTerrain();

  g_clickEffect.Init();
  g_texInventoryBg = UITexture::Load("Data/Interface/mu_inventory_bg.png");

  // Load Equipment Slot Backgrounds from Main 5.2 assets
  g_slotBackgrounds[0] =
      TextureLoader::Resolve("Data/Interface", "newui_item_weapon(R).OZT");
  g_slotBackgrounds[1] =
      TextureLoader::Resolve("Data/Interface", "newui_item_weapon(L).OZT");
  g_slotBackgrounds[2] =
      TextureLoader::Resolve("Data/Interface", "newui_item_cap.OZT");
  g_slotBackgrounds[3] =
      TextureLoader::Resolve("Data/Interface", "newui_item_upper.OZT");
  g_slotBackgrounds[4] =
      TextureLoader::Resolve("Data/Interface", "newui_item_lower.OZT");
  g_slotBackgrounds[5] =
      TextureLoader::Resolve("Data/Interface", "newui_item_gloves.OZT");
  g_slotBackgrounds[6] =
      TextureLoader::Resolve("Data/Interface", "newui_item_boots.OZT");
  g_slotBackgrounds[7] =
      TextureLoader::Resolve("Data/Interface", "newui_item_wing.OZT");
  g_slotBackgrounds[8] =
      TextureLoader::Resolve("Data/Interface", "newui_item_fairy.OZT");
  g_slotBackgrounds[9] =
      TextureLoader::Resolve("Data/Interface", "newui_item_necklace.OZT");
  g_slotBackgrounds[10] =
      TextureLoader::Resolve("Data/Interface", "newui_item_ring.OZT");
  g_slotBackgrounds[11] =
      TextureLoader::Resolve("Data/Interface", "newui_item_ring.OZT");

  g_clickEffect.LoadAssets(data_path);
  g_clickEffect.SetTerrainData(&terrainData);
  checkGLError("hero init");

  // Connect to server via persistent ServerConnection
  g_npcManager.SetTerrainData(&terrainData);
  ServerData serverData;

  // Initialize ClientPacketHandler with game state context
  {
    static ClientGameState gameState;
    gameState.hero = &g_hero;
    gameState.monsterManager = &g_monsterManager;
    gameState.vfxManager = &g_vfxManager;
    gameState.terrain = &g_terrain;
    gameState.inventory = g_inventory;
    gameState.equipSlots = g_equipSlots;
    gameState.groundItems = g_groundItems;
    gameState.itemDefs = &g_itemDefs;
    gameState.zen = &g_zen;
    gameState.syncDone = &g_syncDone;
    gameState.serverLevel = &g_serverLevel;
    gameState.serverHP = &g_serverHP;
    gameState.serverMaxHP = &g_serverMaxHP;
    gameState.serverMP = &g_serverMP;
    gameState.serverMaxMP = &g_serverMaxMP;
    gameState.serverStr = &g_serverStr;
    gameState.serverDex = &g_serverDex;
    gameState.serverVit = &g_serverVit;
    gameState.serverEne = &g_serverEne;
    gameState.serverLevelUpPoints = &g_serverLevelUpPoints;
    gameState.serverXP = &g_serverXP;
    gameState.serverDefense = &g_serverDefense;
    gameState.serverAttackSpeed = &g_serverAttackSpeed;
    gameState.serverMagicSpeed = &g_serverMagicSpeed;
    gameState.quickSlotDefIndex = &g_quickSlotDefIndex;
    gameState.spawnDamageNumber = SpawnDamageNumber;
    gameState.getBodyPartIndex = GetBodyPartIndex;
    gameState.getBodyPartModelFile = GetBodyPartModelFile;
    gameState.getItemRestingAngle = [](int16_t defIdx, glm::vec3 &angle,
                                       float &scale) {
      GetItemRestingAngle(defIdx, angle, scale);
    };
    ClientPacketHandler::Init(&gameState);
  }

  // Set up packet handler BEFORE connecting so no packets are lost
  g_server.onPacket = [&serverData](const uint8_t *pkt, int size) {
    if (size >= 3) {
      std::cout << "[Net:Initial] Received packet type=0x" << std::hex
                << (int)pkt[0] << std::dec << " size=" << size << std::endl;
    }
    ClientPacketHandler::HandleInitialPacket(pkt, size, serverData);
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
    if (g_server.Connect("127.0.0.1", 44405)) {
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
  int packetsReceived = 0;
  for (int attempt = 0; attempt < 100; attempt++) {
    g_server.Poll();
    usleep(20000); // 20ms
  }

  if (serverData.npcs.empty() && !autoScreenshot && !autoDiag) {
    // If we didn't get initial sync data (e.g. timeout), it's probably a
    // stale connection
    std::cerr << "[Net] FATAL: Server connected but failed to sync initial "
                 "game state."
              << std::endl;
    return 1;
  }

  // Switch to ongoing packet handler for game loop
  g_server.onPacket = [](const uint8_t *pkt, int size) {
    ClientPacketHandler::HandleGamePacket(pkt, size);
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

  // Fix: if hero spawned on a non-walkable tile, move to a known safe
  // position
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
    // Diag mode no longer forces top-down view to respect user's "only default"
    // request
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
      // Disabled to force "only default" view
      g_hero.SetPosition(objPos);
      g_hero.SnapToTerrain();
      g_camera.SetPosition(g_hero.GetPosition());
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
    g_server.Poll();
    g_server.Flush();

    // Send player position to server periodically (~4Hz)
    {
      // Tick potion cooldown
      if (g_potionCooldown > 0.0f)
        g_potionCooldown = std::max(0.0f, g_potionCooldown - deltaTime);

      static float posTimer = 0.0f;
      static int lastGridX = -1, lastGridY = -1;
      posTimer += deltaTime;
      if (posTimer >= 0.25f) {
        posTimer = 0.0f;
        glm::vec3 hp = g_hero.GetPosition();
        g_server.SendPrecisePosition(hp.x, hp.z);

        // Also send grid move when grid cell changes (for DB persistence)
        int gx = (int)(hp.z / 100.0f);
        int gy = (int)(hp.x / 100.0f);
        if (gx != lastGridX || gy != lastGridY) {
          g_server.SendGridMove((uint8_t)gx, (uint8_t)gy);
          lastGridX = gx;
          lastGridY = gy;
        }
      }
    }

    // Update monster manager (state machines, animation)
    g_monsterManager.SetPlayerPosition(g_hero.GetPosition());
    g_monsterManager.SetPlayerDead(g_hero.IsDead());
    g_monsterManager.Update(deltaTime);

    // Hero combat: update attack state machine, send attack packet on hit
    // Block all combat in safe zone — but don't stop movement
    {
      bool nowInSafe = g_hero.IsInSafeZone();
      static bool wasInSafe = false;
      if (nowInSafe) {
        // On transition INTO safe zone: cancel any active attack once
        if (!wasInSafe &&
            (g_hero.GetAttackTarget() >= 0 || g_hero.IsAttacking())) {
          g_hero.CancelAttack();
        }
        // Don't update attack/state while in safe zone
      } else {
        g_hero.UpdateAttack(deltaTime);
        g_hero.UpdateState(deltaTime);
        if (g_hero.CheckAttackHit()) {
          int targetIdx = g_hero.GetAttackTarget();
          if (targetIdx >= 0 &&
              targetIdx < g_monsterManager.GetMonsterCount()) {
            uint16_t serverIdx = g_monsterManager.GetServerIndex(targetIdx);
            g_server.SendAttack(serverIdx);
          }
        }
        // Auto-attack: re-engage after cooldown if target still alive
        if (g_hero.GetAttackState() == AttackState::NONE &&
            g_hero.GetAttackTarget() >= 0) {
          int targetIdx = g_hero.GetAttackTarget();
          if (targetIdx < g_monsterManager.GetMonsterCount()) {
            MonsterInfo mi = g_monsterManager.GetMonsterInfo(targetIdx);
            if (mi.state != MonsterState::DYING &&
                mi.state != MonsterState::DEAD && mi.hp > 0) {
              g_hero.AttackMonster(targetIdx, mi.position);
            }
          }
        }
      }
      wasInSafe = nowInSafe;
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
      g_server.SendCharSave(1, (uint16_t)g_serverLevel, (uint16_t)g_serverStr,
                            (uint16_t)g_serverDex, (uint16_t)g_serverVit,
                            (uint16_t)g_serverEne, (uint16_t)g_serverMaxHP,
                            (uint16_t)g_serverMaxHP,
                            (uint16_t)g_serverLevelUpPoints,
                            (uint64_t)g_serverXP, g_quickSlotDefIndex);
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
            gi.position.y = h + 0.5f;
          }
        }
        float dist = glm::length(
            glm::vec3(heroPos.x - gi.position.x, 0, heroPos.z - gi.position.z));
        // Auto-pickup Zen only (items require explicit click)
        if (gi.defIndex == -1 && dist < 120.0f && !g_hero.IsDead()) {
          g_server.SendPickup(gi.dropIndex);
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
      g_hero.SetInSafeZone((heroAttr & 0x01) != 0 || (heroAttr & 0x08) != 0);
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
    // if (g_hoveredNpc >= 0)
    //   g_npcManager.RenderOutline(g_hoveredNpc, view, projection);
    // Simplified hover UI: handled by RenderLabels background highlight

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

    // Auto-screenshot flag (capture happens after ImGui render to include
    // HUD)
    bool captureScreenshot = (autoScreenshot && diagFrame == 60);

    // Start the Dear ImGui frame
    g_renderQueue.clear();
    g_pendingTooltip.active = false; // Reset deferred tooltip each frame
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

        // HP bar
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
        {
          int curHP = g_hero.GetHP();
          int maxHP = g_hero.GetMaxHP();
          float hpFrac = maxHP > 0 ? (float)curHP / (float)maxHP : 0.0f;
          hpFrac = std::clamp(hpFrac, 0.0f, 1.0f);
          char hpLabel[32];
          snprintf(hpLabel, sizeof(hpLabel), "HP %d/%d", curHP, maxHP);
          ImGui::ProgressBar(hpFrac, ImVec2(180, 20), hpLabel);
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // MP bar
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              ImVec4(0.2f, 0.3f, 0.9f, 1.0f));
        {
          int curMP = g_hero.GetMana();
          int maxMP = g_hero.GetMaxMana();
          float mpFrac = maxMP > 0 ? (float)curMP / (float)maxMP : 0.0f;
          mpFrac = std::clamp(mpFrac, 0.0f, 1.0f);
          char mpLabel[32];
          snprintf(mpLabel, sizeof(mpLabel), "MP %d/%d", curMP, maxMP);
          ImGui::ProgressBar(mpFrac, ImVec2(120, 20), mpLabel);
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // Level
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Lv.%d",
                           g_serverLevel);
        ImGui::SameLine();

        // XP Bar (consistent source of truth from g_hero)
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              ImVec4(0.2f, 0.7f, 0.9f, 1.0f));
        {
          uint64_t curXp = g_hero.GetExperience();
          int curLv = g_hero.GetLevel();
          uint64_t nextXp = g_hero.GetNextExperience();
          uint64_t prevXp = g_hero.CalcXPForLevel(curLv);

          float xpFrac = 0.0f;
          if (nextXp > prevXp)
            xpFrac = (float)(curXp - prevXp) / (float)(nextXp - prevXp);
          xpFrac = std::clamp(xpFrac, 0.0f, 1.0f);

          char xpLabel[64];
          snprintf(xpLabel, sizeof(xpLabel), "XP %llu/%llu (%.1f%%)",
                   (unsigned long long)(curXp - prevXp),
                   (unsigned long long)(nextXp - prevXp), xpFrac * 100.0f);
          ImGui::ProgressBar(xpFrac, ImVec2(220, 20), xpLabel);
        }
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

        // Quick Slot (Q)
        ImGui::SameLine(vp->Size.x * 0.5f - 25);
        ImGui::BeginGroup();
        {
          ImVec2 qPos = ImGui::GetCursorScreenPos();
          ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 20, 30, 200));
          ImGui::BeginChild("QuickSlotQ", ImVec2(50, 50), true,
                            ImGuiWindowFlags_NoScrollbar);

          int itemCount = 0;
          if (g_quickSlotDefIndex != -1) {
            for (int i = 0; i < INVENTORY_SLOTS; i++) {
              if (g_inventory[i].occupied && g_inventory[i].primary &&
                  g_inventory[i].defIndex == g_quickSlotDefIndex) {
                itemCount += g_inventory[i].quantity;
              }
            }

            // Queue 3D render for quick slot icon
            auto it = g_itemDefs.find(g_quickSlotDefIndex);
            if (it != g_itemDefs.end() && itemCount > 0) {
              g_quickSlotPos = qPos; // Capture for post-3D overlay
              int qwinH = (int)ImGui::GetIO().DisplaySize.y;
              g_renderQueue.push_back(
                  {it->second.modelFile, g_quickSlotDefIndex, (int)qPos.x + 5,
                   qwinH - (int)qPos.y - 45, 40, 40, false});

              // --- Cooldown Overlay ---
              if (false && g_potionCooldown > 0.0f) {
                ImVec2 p0 = qPos;
                ImVec2 p1 = ImVec2(qPos.x + 50, qPos.y + 50);

                // Dark semi-transparent overlay
                ImGui::GetForegroundDrawList()->AddRectFilled(
                    p0, p1, IM_COL32(20, 20, 20, 180));

                // Countdown text (bright white) - Show integer seconds
                char cdBuf[16];
                snprintf(cdBuf, sizeof(cdBuf), "%d",
                         (int)ceil(g_potionCooldown));
                ImVec2 txtSize = ImGui::CalcTextSize(cdBuf);
                ImGui::GetForegroundDrawList()->AddText(
                    ImVec2(p0.x + (50 - txtSize.x) * 0.5f,
                           p0.y + (50 - txtSize.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), cdBuf);
              }
            }
          }

          ImGui::EndChild();
          ImGui::PopStyleColor();

          // Overlay "Q" and count
          ImDrawList *foreground = ImGui::GetForegroundDrawList();
          foreground->AddText(ImVec2(qPos.x + 3, qPos.y + 2),
                              IM_COL32(255, 255, 255, 200), "Q");
          if (g_quickSlotDefIndex != -1 && itemCount > 0) {
            char cbuf[16];
            snprintf(cbuf, sizeof(cbuf), "%d", itemCount);
            ImVec2 tsz = ImGui::CalcTextSize(cbuf);
            foreground->AddText(ImVec2(qPos.x + 47 - tsz.x, qPos.y + 32),
                                IM_COL32(255, 210, 80, 255), cbuf);
          }
        }
        ImGui::EndGroup();
      }
      ImGui::End();

      ImDrawList *dl = ImGui::GetForegroundDrawList();

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
            col = IM_COL32(250, 250, 250,
                           (int)(alpha * 255)); // Brighter for visibility
            text = "MISS";
          } else if (d.type == 9) {
            // XP gained (purple-gold)
            snprintf(buf, sizeof(buf), "+%d XP", d.damage);
            text = buf;
            col = IM_COL32(220, 180, 255, (int)(alpha * 255));
          } else if (d.type == 10) {
            // Heal (bright green)
            snprintf(buf, sizeof(buf), "+%d", d.damage);
            text = buf;
            col = IM_COL32(60, 255, 60, (int)(alpha * 255));
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
          dl->AddText(g_fontDefault, fontSize, ImVec2(tpos.x + 1, tpos.y + 1),
                      IM_COL32(0, 0, 0, (int)(alpha * 200)), text);
          // Main text
          dl->AddText(g_fontDefault, fontSize, tpos, col, text);
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
          ImVec2 textSize =
              g_fontDefault->CalcTextSizeA(14.0f, FLT_MAX, 0, nameText);
          float tx = sx - textSize.x * 0.5f;
          float ty = sy - textSize.y;

          // Highlight background if hovered (replaces mesh outline)
          if (i == g_hoveredMonster) {
            float pad = 4.0f;
            dl->AddRectFilled(
                ImVec2(tx - pad, ty - pad),
                ImVec2(tx + textSize.x + pad, ty + textSize.y + pad),
                IM_COL32(255, 255, 255, (int)(alpha * 60)), 3.0f);
            dl->AddRect(ImVec2(tx - pad, ty - pad),
                        ImVec2(tx + textSize.x + pad, ty + textSize.y + pad),
                        IM_COL32(255, 255, 255, (int)(alpha * 120)), 3.0f);
          }

          // Name color: white normally, red if attacking hero
          ImU32 nameCol = (mi.state == MonsterState::ATTACKING ||
                           mi.state == MonsterState::CHASING)
                              ? IM_COL32(255, 100, 100, (int)(alpha * 255))
                              : IM_COL32(255, 255, 255, (int)(alpha * 220));

          // Shadow + text
          dl->AddText(g_fontDefault, 14.0f, ImVec2(tx + 1, ty + 1),
                      IM_COL32(0, 0, 0, (int)(alpha * 180)), nameText);
          dl->AddText(g_fontDefault, 14.0f, ImVec2(tx, ty), nameCol, nameText);

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

          // Update Physics
          float terrainH = g_terrain.GetHeight(gi.position.x, gi.position.z);
          UpdateGroundItemPhysics(gi, terrainH);

          const char *modelFile =
              GetDropModelName(gi.defIndex); // Use helper mapping

          if (modelFile) {
            ItemModelManager::RenderItemWorld(modelFile, gi.position, view,
                                              projection, gi.scale, gi.angle);
          } else if (gi.defIndex == -1) {
            // Zen model
            RenderZenPile(gi.quantity, gi.position, gi.angle, gi.scale, view,
                          projection);
          }

          // Float label logic (static, no bobbing)
          // float bob = sinf(gi.timer * 3.0f) * 5.0f;

          glm::vec3 labelPos = gi.position + glm::vec3(0, 15.0f, 0);
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

          ImVec2 ts = g_fontDefault->CalcTextSizeA(13.0f, FLT_MAX, 0, label);
          float tx = sx - ts.x * 0.5f, ty = sy - ts.y * 0.5f;

          // Highlight if hovered
          int giIndex = (int)(&gi - g_groundItems);
          bool isHovered = (giIndex == g_hoveredGroundItem);

          // Yellow for items, gold for Zen
          ImU32 col = gi.defIndex == -1 ? IM_COL32(255, 215, 0, 220)
                                        : IM_COL32(180, 255, 180, 220);

          if (isHovered) {
            // Bright white highlight for hovered items
            col = IM_COL32(255, 255, 255, 255);
            // Thicker shadow/outline
            dl->AddText(g_fontDefault, 13.0f, ImVec2(tx + 2, ty + 1),
                        IM_COL32(0, 0, 0, 200), label);
            dl->AddText(g_fontDefault, 13.0f, ImVec2(tx - 1, ty - 1),
                        IM_COL32(0, 0, 0, 200), label);
          }

          dl->AddText(g_fontDefault, 13.0f, ImVec2(tx + 1, ty + 1),
                      IM_COL32(0, 0, 0, 160), label);
          dl->AddText(g_fontDefault, 13.0f, ImVec2(tx, ty), col, label);

          // Hover tooltip: show item details when mouse is near the label
          ImVec2 mousePos = ImGui::GetIO().MousePos;
          float hoverRadius = std::max(ts.x * 0.5f + 10.0f, 20.0f);
          if (std::abs(mousePos.x - sx) < hoverRadius &&
              std::abs(mousePos.y - sy) < 20.0f) {
            ImVec2 tPos(mousePos.x + 15, mousePos.y + 10);
            // Clamp to screen
            if (tPos.x + 180 > winW)
              tPos.x = winW - 185;
            if (tPos.y + 80 > winH)
              tPos.y = winH - 85;
            dl->AddRectFilled(tPos, ImVec2(tPos.x + 180, tPos.y + 80),
                              IM_COL32(0, 0, 0, 240), 4.0f);
            dl->AddRect(tPos, ImVec2(tPos.x + 180, tPos.y + 80),
                        IM_COL32(150, 150, 255, 200), 4.0f);
            float curY = tPos.y + 8;
            // Name (gold)
            dl->AddText(ImVec2(tPos.x + 8, curY), IM_COL32(255, 215, 80, 255),
                        label);
            curY += 18;
            if (gi.defIndex != -1) {
              auto dit = g_itemDefs.find(gi.defIndex);
              if (dit != g_itemDefs.end()) {
                const auto &dd = dit->second;
                if (dd.reqStr > 0) {
                  char rb[32];
                  snprintf(rb, sizeof(rb), "STR: %d", dd.reqStr);
                  dl->AddText(ImVec2(tPos.x + 8, curY),
                              IM_COL32(200, 200, 200, 255), rb);
                  curY += 14;
                }
                if (dd.reqDex > 0) {
                  char rb[32];
                  snprintf(rb, sizeof(rb), "DEX: %d", dd.reqDex);
                  dl->AddText(ImVec2(tPos.x + 8, curY),
                              IM_COL32(200, 200, 200, 255), rb);
                  curY += 14;
                }
                if (dd.levelReq > 0) {
                  char rb[32];
                  snprintf(rb, sizeof(rb), "Lv: %d", dd.levelReq);
                  dl->AddText(ImVec2(tPos.x + 8, curY),
                              IM_COL32(200, 200, 200, 255), rb);
                  curY += 14;
                }
              }
            } else {
              dl->AddText(ImVec2(tPos.x + 8, curY), IM_COL32(255, 215, 0, 200),
                          "Click to pick up");
            }
          }
        }
      }
    }

    // Add some debug info
    ImGui::Begin("Terrain Debug");
    ImGui::Text("Camera Pos: %.1f, %.1f, %.1f", camPos.x, camPos.y, camPos.z);
    ImGui::Text("Camera Zoom: %.1f (Default: 800.0)", g_camera.GetZoom());
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
            hovered ? IM_COL32(20, 40, 20, 200) : IM_COL32(10, 10, 10, 150);
        ImU32 borderCol =
            hovered ? IM_COL32(100, 255, 100, 200) : IM_COL32(80, 80, 80, 150);
        ImU32 textCol = hovered ? IM_COL32(150, 255, 150, 255)
                                : IM_COL32(200, 200, 200, 255);

        // Fill
        drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), bgCol, 2.0f);
        // Border
        drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), borderCol, 2.0f, 0,
                          1.0f);

        // Shadow
        drawList->AddText(
            ImVec2(sx - textSize.x / 2 + 1, sy - textSize.y / 2 + 1),
            IM_COL32(0, 0, 0, 180), info.name.c_str());
        // Text
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

    // ── Character Info and Inventory panels ──
    ImDrawList *panelDl = ImGui::GetForegroundDrawList();
    if (g_showCharInfo)
      RenderCharInfoPanel(panelDl, g_hudCoords);
    if (g_showInventory)
      RenderInventoryPanel(panelDl, g_hudCoords);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Flatten render queue (items on top of UI)
    // Scale logical pixel coords to physical framebuffer pixels (HiDPI/Retina
    // fix)
    {
      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      float scaleX = (float)fbW / ImGui::GetIO().DisplaySize.x;
      float scaleY = (float)fbH / ImGui::GetIO().DisplaySize.y;
      for (const auto &job : g_renderQueue) {
        int px = (int)(job.x * scaleX);
        int py = (int)(job.y * scaleY);
        int pw = (int)(job.w * scaleX);
        int ph = (int)(job.h * scaleY);
        ItemModelManager::RenderItemUI(job.modelFile, job.defIndex, px, py, pw,
                                       ph, job.hovered);
      }
    }

    // Second ImGui pass: draw deferred tooltip and HUD overlays ON TOP of 3D
    // items
    if (g_pendingTooltip.active || g_potionCooldown > 0.0f) {
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      if (g_potionCooldown > 0.0f && g_quickSlotPos.x > 0) {
        ImVec2 p0 = g_quickSlotPos;
        ImVec2 p1 = ImVec2(p0.x + 50, p0.y + 50);

        // Dark semi-transparent overlay
        ImGui::GetForegroundDrawList()->AddRectFilled(
            p0, p1, IM_COL32(20, 20, 20, 180));

        // Countdown text (bright white)
        char cdBuf[16];
        snprintf(cdBuf, sizeof(cdBuf), "%d", (int)ceil(g_potionCooldown));
        ImVec2 txtSize = ImGui::CalcTextSize(cdBuf);
        ImGui::GetForegroundDrawList()->AddText(
            ImVec2(p0.x + (50 - txtSize.x) * 0.5f,
                   p0.y + (50 - txtSize.y) * 0.5f),
            IM_COL32(255, 255, 255, 255), cdBuf);
      }

      if (g_pendingTooltip.active) {
        FlushPendingTooltip();
      }

      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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

  // Save hero position via camera state for next launch (unless in diag mode)
  if (!autoDiag) {
    g_camera.SetPosition(g_hero.GetPosition());
    g_camera.SaveState("camera_save.txt");
  }

  // Save character stats to server before disconnecting
  g_server.SendCharSave(1, (uint16_t)g_serverLevel, (uint16_t)g_serverStr,
                        (uint16_t)g_serverDex, (uint16_t)g_serverVit,
                        (uint16_t)g_serverEne, (uint16_t)g_serverHP,
                        (uint16_t)g_serverMaxHP,
                        (uint16_t)g_serverLevelUpPoints, (uint64_t)g_serverXP,
                        g_quickSlotDefIndex);
  g_server.Flush();

  // Disconnect from server
  g_server.Disconnect();
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

  // StreamRedirector handles restoration and deletion

  return 0;
}
