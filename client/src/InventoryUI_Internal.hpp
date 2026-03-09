#pragma once
// Internal header shared between InventoryUI.cpp, InventoryUITooltip.cpp,
// and InventoryUISkills.cpp.  NOT a public API — lives in src/, not include/.

#include "InventoryUI.hpp"
#include "ItemDatabase.hpp"
#include "imgui.h"
#include <GL/glew.h>
#include <string>
#include <vector>

// ─── Shared constants (inline constexpr — safe in headers) ──────────────────

inline constexpr float g_uiPanelScale = 1.5f;
inline constexpr float BASE_PANEL_W = 190.0f;
inline constexpr float BASE_PANEL_H = 429.0f;
inline constexpr float PANEL_W = BASE_PANEL_W * g_uiPanelScale;
inline constexpr float PANEL_H = BASE_PANEL_H * g_uiPanelScale;
inline constexpr float PANEL_Y = 20.0f;
inline constexpr float PANEL_X_RIGHT = 1270.0f - PANEL_W;

inline constexpr int SKILL_ICON_COLS = 25;
inline constexpr float SKILL_ICON_W = 20.0f;
inline constexpr float SKILL_ICON_H = 28.0f;
inline constexpr float SKILL_TEX_SIZE = 512.0f;

inline constexpr int NUM_DK_SKILLS = 8;
inline constexpr int NUM_DW_SPELLS = 14;

// ─── Shared structs ─────────────────────────────────────────────────────────

struct SkillDef {
  uint8_t skillId;
  const char *name;
  int resourceCost; // AG for DK, Mana for DW
  int levelReq;
  int damageBonus;
  const char *desc;
  int energyReq; // Energy stat required to learn
};

struct PendingTooltipLine {
  ImU32 color;
  std::string text;
  uint8_t flags = 0; // 1=center, 2=separator, 4=small gap after
};

struct PendingTooltip {
  bool active = false;
  ImVec2 pos;
  float w = 0, h = 0;
  std::vector<PendingTooltipLine> lines;
  ImU32 borderColor = IM_COL32(120, 120, 200, 200);
};

struct DeferredCooldown {
  float x, y, w, h; // screen-space rect
  char text[8];
};

// ─── Shared state (defined in InventoryUI.cpp) ─────────────────────────────

extern const InventoryUIContext *s_ctx;
extern PendingTooltip g_pendingTooltip;
extern GLuint g_texSkillIcons;
extern bool g_isDragging;
extern int16_t g_dragDefIndex;
extern int g_dragFromSlot;
extern int g_dragFromEquipSlot;
extern int g_dragFromSkillSlot;
extern int g_dragFromPotionSlot;
extern bool g_dragFromRmcSlot;
extern std::vector<DeferredCooldown> g_deferredCooldowns;

extern const SkillDef g_dkSkills[];
extern const SkillDef g_dwSpells[];

// ─── Shared helper functions (defined in InventoryUI.cpp) ───────────────────

const SkillDef *GetClassSkills(uint8_t classCode, int &outCount);

// These live inside namespace InventoryUI in the .cpp files
namespace InventoryUI {
int GetEquipSlotForCategory(uint8_t category, bool leftHand = false);
const DropDef *GetEquippedDropDef(int equipSlot);
}

void BeginPendingTooltip(float tw, float th);
void AddPendingTooltipLine(ImU32 color, const std::string &text,
                           uint8_t flags = 0);
void AddTooltipSeparator();

void DrawStyledPanel(ImDrawList *dl, float x0, float y0, float x1, float y1,
                     float rounding = 5.0f);
void DrawStyledSlot(ImDrawList *dl, ImVec2 p0, ImVec2 p1,
                    bool hovered = false, float rounding = 3.0f);
void DrawStyledBar(ImDrawList *dl, float x, float y, float w, float h,
                   float frac, ImU32 topColor, ImU32 botColor,
                   const char *label);
void DrawShadowText(ImDrawList *dl, ImVec2 pos, ImU32 color,
                    const char *text, int shadowOffset = 1);
