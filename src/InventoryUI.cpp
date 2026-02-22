#include "InventoryUI.hpp"
#include "HeroCharacter.hpp"
#include "ItemDatabase.hpp"
#include "ServerConnection.hpp"
#include "TextureLoader.hpp"
#include "UITexture.hpp"
#include "imgui.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

// ─── File-scope statics (internal-only) ─────────────────────────────────────

static const InventoryUIContext *s_ctx = nullptr;

// Potion cooldown constant
static constexpr float POTION_COOLDOWN_TIME = 30.0f;

// UI scale and panel layout
static constexpr float g_uiPanelScale = 1.5f;
static constexpr float BASE_PANEL_W = 190.0f;
static constexpr float BASE_PANEL_H = 429.0f;
static constexpr float PANEL_W = BASE_PANEL_W * g_uiPanelScale;
static constexpr float PANEL_H = BASE_PANEL_H * g_uiPanelScale;
static constexpr float PANEL_Y = 20.0f;
static constexpr float PANEL_X_RIGHT = 1270.0f - PANEL_W;

// Equipment layout rects (virtual coords)
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

// Slot background textures
static GLuint g_slotBackgrounds[12] = {0};
static UITexture g_texInventoryBg;
static GLuint g_texSkillIcons = 0; // Skill.OZJ sprite sheet

// Render queue for deferred 3D item rendering
static std::vector<ItemRenderJob> g_renderQueue;

// Deferred text overlays (rendered AFTER 3D items in second ImGui pass)
struct DeferredOverlay {
  float x, y; // screen coords
  ImU32 color;
  char text[8];
};
static std::vector<DeferredOverlay> g_deferredOverlays;

// Drag state
static int g_dragFromSlot = -1;
static int g_dragFromEquipSlot = -1;
static int16_t g_dragDefIndex = -2;
static uint8_t g_dragQuantity = 0;
static uint8_t g_dragItemLevel = 0;
static bool g_isDragging = false;
static bool g_dragFromQuickSlot = false;

// Deferred tooltip
struct PendingTooltipLine {
  ImU32 color;
  std::string text;
};
struct PendingTooltip {
  bool active = false;
  ImVec2 pos;
  float w = 0, h = 0;
  std::vector<PendingTooltipLine> lines;
};
static PendingTooltip g_pendingTooltip;

// Shop grid (mirrors inventory bag grid for NPC shop display)
static constexpr int SHOP_GRID_COLS = 8;
static constexpr int SHOP_GRID_MAX_ROWS = 15;
static constexpr int SHOP_GRID_MAX_SLOTS = SHOP_GRID_COLS * SHOP_GRID_MAX_ROWS;

struct ShopGridSlot {
  int16_t defIndex = -2;
  uint8_t itemLevel = 0;
  uint32_t buyPrice = 0;
  bool occupied = false;
  bool primary = false;
};

static ShopGridSlot s_shopGrid[SHOP_GRID_MAX_SLOTS];
static int s_shopGridRows = 0;
static bool s_shopGridDirty = true;
static size_t s_lastShopItemCount = 0; // Detect new SHOP_LIST arrivals

// Screen notification (center of screen, fades out)
static std::string s_notifyText;
static float s_notifyTimer = 0.0f;
static constexpr float NOTIFY_DURATION = 2.0f;

// ─── Internal helpers (unnamed namespace) ───────────────────────────────────

namespace {

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

// Close button with hover highlight — used by all panels
static void DrawCloseButton(ImDrawList *dl, const UICoords &c, float px,
                            float py) {
  float bx = BASE_PANEL_W - 24;
  float by = 6;
  float bw = 16, bh = 14;
  ImVec2 bMin(c.ToScreenX(px + bx * g_uiPanelScale),
              c.ToScreenY(py + by * g_uiPanelScale));
  ImVec2 bMax(c.ToScreenX(px + (bx + bw) * g_uiPanelScale),
              c.ToScreenY(py + (by + bh) * g_uiPanelScale));
  ImVec2 mp = ImGui::GetIO().MousePos;
  bool hovered =
      mp.x >= bMin.x && mp.x < bMax.x && mp.y >= bMin.y && mp.y < bMax.y;
  ImU32 bgCol =
      hovered ? IM_COL32(200, 40, 40, 255) : IM_COL32(100, 20, 20, 200);
  dl->AddRectFilled(bMin, bMax, bgCol, 2.0f);
  if (hovered)
    dl->AddRect(bMin, bMax, IM_COL32(255, 100, 100, 255), 2.0f);
  ImVec2 xSize = ImGui::CalcTextSize("X");
  ImVec2 xPos(bMin.x + (bMax.x - bMin.x) * 0.5f - xSize.x * 0.5f,
              bMin.y + (bMax.y - bMin.y) * 0.5f - xSize.y * 0.5f);
  dl->AddText(xPos, IM_COL32(255, 255, 255, 255), "X");
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

// Helper: draw text with shadow (handles scaling)
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

static void RebuildShopGrid() {
  for (int i = 0; i < SHOP_GRID_MAX_SLOTS; i++)
    s_shopGrid[i] = {};

  auto &defs = ItemDatabase::GetItemDefs();
  int usedRows = 0;

  for (size_t i = 0; i < s_ctx->shopItems->size(); i++) {
    auto &si = (*s_ctx->shopItems)[i];
    auto it = defs.find(si.defIndex);
    if (it == defs.end())
      continue;

    int iw = it->second.width;
    int ih = it->second.height;
    bool placed = false;

    for (int row = 0; row <= SHOP_GRID_MAX_ROWS - ih && !placed; row++) {
      for (int col = 0; col <= SHOP_GRID_COLS - iw && !placed; col++) {
        bool fits = true;
        for (int dy = 0; dy < ih && fits; dy++)
          for (int dx = 0; dx < iw && fits; dx++)
            if (s_shopGrid[(row + dy) * SHOP_GRID_COLS + (col + dx)].occupied)
              fits = false;
        if (!fits)
          continue;

        for (int dy = 0; dy < ih; dy++) {
          for (int dx = 0; dx < iw; dx++) {
            int slot = (row + dy) * SHOP_GRID_COLS + (col + dx);
            s_shopGrid[slot].occupied = true;
            s_shopGrid[slot].primary = (dy == 0 && dx == 0);
            s_shopGrid[slot].defIndex = si.defIndex;
            s_shopGrid[slot].itemLevel = si.itemLevel;
            s_shopGrid[slot].buyPrice = si.buyPrice;
          }
        }
        if (row + ih > usedRows)
          usedRows = row + ih;
        placed = true;
      }
    }
  }

  s_shopGridRows = usedRows;
  s_shopGridDirty = false;
}

static bool CanEquipItem(int16_t defIdx) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
  auto it = g_itemDefs.find(defIdx);
  if (it == g_itemDefs.end())
    return false;
  const auto &def = it->second;

  if (*s_ctx->serverLevel < def.levelReq) {
    std::cout << "[UI] Level requirement not met (" << *s_ctx->serverLevel
              << "/" << def.levelReq << ")" << std::endl;
    return false;
  }
  if (*s_ctx->serverStr < def.reqStr) {
    std::cout << "[UI] Strength requirement not met (" << *s_ctx->serverStr
              << "/" << def.reqStr << ")" << std::endl;
    return false;
  }
  if (*s_ctx->serverDex < def.reqDex) {
    std::cout << "[UI] Dexterity requirement not met (" << *s_ctx->serverDex
              << "/" << def.reqDex << ")" << std::endl;
    return false;
  }
  if (*s_ctx->serverVit < def.reqVit) {
    std::cout << "[UI] Vitality requirement not met (" << *s_ctx->serverVit
              << "/" << def.reqVit << ")" << std::endl;
    return false;
  }
  if (*s_ctx->serverEne < def.reqEne) {
    std::cout << "[UI] Energy requirement not met (" << *s_ctx->serverEne << "/"
              << def.reqEne << ")" << std::endl;
    return false;
  }

  // Class check: bit_mask = 1 << (char_class >> 4)
  // Mapping: 0(DW) -> bit 0, 16(DK) -> bit 1, 32(Elf) -> bit 2
  int bitIndex = s_ctx->hero->GetClass() >> 4;
  if (!(def.classFlags & (1 << bitIndex))) {
    std::cout << "[UI] This item cannot be equipped by your class! (Class:"
              << (int)s_ctx->hero->GetClass() << " Bit:" << bitIndex
              << " Flags:0x" << std::hex << def.classFlags << std::dec << ")"
              << std::endl;
    return false;
  }

  return true;
}

static bool CheckBagFit(int16_t defIdx, int targetSlot, int ignoreSlot = -1) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
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
      if (s_ctx->inventory[s].occupied) {
        return false;
      }
    }
  }
  return true;
}

} // anonymous namespace

// ─── InventoryUI namespace implementation ───────────────────────────────────

namespace InventoryUI {

void Init(const InventoryUIContext &ctx) {
  static InventoryUIContext stored;
  stored = ctx;
  s_ctx = &stored;
}

void ClearBagItem(int slot) {
  if (slot < 0 || slot >= INVENTORY_SLOTS)
    return;
  if (!s_ctx->inventory[slot].occupied)
    return;

  int primarySlot = slot;
  if (!s_ctx->inventory[slot].primary) {
    // Search backward or use stored defIndex to find root
  }

  int16_t defIdx = s_ctx->inventory[primarySlot].defIndex;
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
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
          s_ctx->inventory[s] = {};
        }
      }
    }
  } else {
    s_ctx->inventory[primarySlot] = {};
  }
}

void ConsumeQuickSlotItem() {
  if (*s_ctx->quickSlotDefIndex == -1)
    return;

  // Cooldown check
  if (*s_ctx->potionCooldown > 0.0f) {
    std::cout << "[QuickSlot] Cooldown active (" << *s_ctx->potionCooldown
              << "s remaining)" << std::endl;
    return;
  }

  // Search for the first instance of this item in inventory
  int foundSlot = -1;
  for (int i = 0; i < INVENTORY_SLOTS; i++) {
    if (s_ctx->inventory[i].occupied && s_ctx->inventory[i].primary &&
        s_ctx->inventory[i].defIndex == *s_ctx->quickSlotDefIndex) {
      foundSlot = i;
      break;
    }
  }

  if (foundSlot != -1) {
    // Determine healing amount
    int healAmount = 0;
    auto &g_itemDefs = ItemDatabase::GetItemDefs();
    auto it = g_itemDefs.find(*s_ctx->quickSlotDefIndex);
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
      // Full HP check — don't consume if already at max
      if (*s_ctx->serverHP >= *s_ctx->serverMaxHP) {
        ShowNotification("HP is full!");
        return;
      }

      // Send use request to server
      s_ctx->server->SendItemUse((uint8_t)foundSlot);

      // Start local cooldown for UI feedback
      *s_ctx->potionCooldown = POTION_COOLDOWN_TIME;

      std::cout << "[QuickSlot] Requested to use "
                << ItemDatabase::GetItemNameByDef(*s_ctx->quickSlotDefIndex)
                << " from slot " << foundSlot << std::endl;
    }
  } else {
    std::cout << "[QuickSlot] No "
              << ItemDatabase::GetItemNameByDef(*s_ctx->quickSlotDefIndex)
              << " found in inventory!" << std::endl;
  }
}

void SetBagItem(int slot, int16_t defIdx, uint8_t qty, uint8_t lvl) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
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
      if (s >= INVENTORY_SLOTS || s_ctx->inventory[s].occupied)
        return;
    }
  }

  // Pass 2: mark slots
  for (int hh = 0; hh < h; hh++) {
    for (int ww = 0; ww < w; ww++) {
      int s = (r + hh) * 8 + (c + ww);
      s_ctx->inventory[s].occupied = true;
      s_ctx->inventory[s].primary = (hh == 0 && ww == 0);
      s_ctx->inventory[s].defIndex = defIdx;
      if (s_ctx->inventory[s].primary) {
        s_ctx->inventory[s].quantity = qty;
        s_ctx->inventory[s].itemLevel = lvl;
      }
    }
  }
}

void RecalcEquipmentStats() {
  int totalDmgMin = 0, totalDmgMax = 0, totalDef = 0;
  for (int s = 0; s < 12; ++s) {
    if (!s_ctx->equipSlots[s].equipped)
      continue;
    int16_t defIdx = ItemDatabase::GetDefIndexFromCategory(
        s_ctx->equipSlots[s].category, s_ctx->equipSlots[s].itemIndex);
    auto *info = ItemDatabase::GetDropInfo(defIdx);
    if (info) {
      totalDmgMin += info->dmgMin;
      totalDmgMax += info->dmgMax;
      totalDef += info->defense;
    }
  }
  s_ctx->hero->SetWeaponBonus(totalDmgMin, totalDmgMax);
  s_ctx->hero->SetDefenseBonus(totalDef);
}

// Get the equip slot index for a given item category (-1 if none)
static int GetEquipSlotForCategory(uint8_t category,
                                   bool isAlternativeHand = false) {
  switch (category) {
  case 0:
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
    return isAlternativeHand ? 1
                             : 0; // Weapons → R.Hand or L.Hand if alternative
  case 6:
    return 1; // Shield → L.Hand
  case 7:
    return 2; // Helm
  case 8:
    return 3; // Armor
  case 9:
    return 4; // Pants
  case 10:
    return 5; // Gloves
  case 11:
    return 6; // Boots
  case 12:
    return 7; // Wings
  default:
    return -1;
  }
}

// Get the equipped item's DropDef for comparison (nullptr if slot empty)
static const DropDef *GetEquippedDropDef(int equipSlot) {
  if (equipSlot < 0 || equipSlot >= 12)
    return nullptr;
  if (!s_ctx->equipSlots[equipSlot].equipped)
    return nullptr;
  int16_t di = ItemDatabase::GetDefIndexFromCategory(
      s_ctx->equipSlots[equipSlot].category,
      s_ctx->equipSlots[equipSlot].itemIndex);
  return ItemDatabase::GetDropInfo(di);
}

void AddPendingItemTooltip(int16_t defIndex, int itemLevel) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
  auto it = g_itemDefs.find(defIndex);
  const ClientItemDefinition *def = nullptr;
  ClientItemDefinition fallback;

  if (it != g_itemDefs.end()) {
    def = &it->second;
  } else {
    fallback.name = ItemDatabase::GetDropName(defIndex);
    fallback.category = (uint8_t)(defIndex / 32);
    fallback.width = 1;
    fallback.height = 1;
    def = &fallback;
  }

  // Find equipped item for comparison
  int equipSlot = GetEquipSlotForCategory(def->category);
  // Special handling for weapons to compare with left hand if right hand is
  // empty or weapon doesn't fit right hand. We'll just compare against primary
  // recommended slot (right hand for weapons) for tooltips.
  const DropDef *equippedDef = GetEquippedDropDef(equipSlot);
  // Don't compare against itself (same defIndex in the same slot)
  if (equippedDef && equipSlot >= 0 && s_ctx->equipSlots[equipSlot].equipped) {
    int16_t eqDi = ItemDatabase::GetDefIndexFromCategory(
        s_ctx->equipSlots[equipSlot].category,
        s_ctx->equipSlots[equipSlot].itemIndex);
    if (eqDi == defIndex &&
        s_ctx->equipSlots[equipSlot].itemLevel == (uint8_t)itemLevel)
      equippedDef = nullptr; // Same item, no comparison
  }

  // Calculate tooltip height
  float lineH = 18.0f;
  float th = 10.0f; // Padding
  th += lineH;      // name
  const char *catDesc = ItemDatabase::GetCategoryName(def->category);
  if (catDesc[0])
    th += lineH;

  // Potion healing info
  int potionHeal = 0;
  if (def->category == 14) {
    if ((defIndex % 32) == 0)
      potionHeal = 10;
    else if ((defIndex % 32) == 1)
      potionHeal = 20;
    else if ((defIndex % 32) == 2)
      potionHeal = 50;
    else if ((defIndex % 32) == 3)
      potionHeal = 100;
    if (potionHeal > 0)
      th += lineH;
  }

  // Hands specification
  if (def->category <= 5 || def->category == 12)
    th += lineH;

  if (def->category <= 5 && (def->dmgMin > 0 || def->dmgMax > 0))
    th += lineH;
  // Attack Speed
  if (def->category <= 5 && def->attackSpeed > 0)
    th += lineH;

  if (def->category >= 7 && def->category <= 11 && def->defense > 0)
    th += lineH;

  // Comparison lines height
  if (equippedDef) {
    if (def->category <= 5 && (def->dmgMin > 0 || def->dmgMax > 0))
      th += lineH; // damage diff
    if (def->category >= 7 && def->category <= 11 && def->defense > 0)
      th += lineH; // defense diff
  }

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

  // Class requirements
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

  if (potionHeal > 0) {
    char healBuf[32];
    snprintf(healBuf, sizeof(healBuf), "Restores %d HP", potionHeal);
    AddPendingTooltipLine(IM_COL32(80, 255, 80, 255), healBuf);
  }

  if (def->category <= 5) {
    if (def->twoHanded)
      AddPendingTooltipLine(IM_COL32(200, 200, 200, 255), "Two-Handed Weapon");
    else if (def->category != 4 || def->name == "Arrows" || def->name == "Bolt")
      AddPendingTooltipLine(IM_COL32(200, 200, 200, 255), "One-Handed Weapon");
  }

  // Weapon stats + comparison
  if (def->category <= 5 && (def->dmgMin > 0 || def->dmgMax > 0)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Damage: %d~%d", def->dmgMin, def->dmgMax);
    AddPendingTooltipLine(IM_COL32(255, 140, 140, 255), buf);

    if (equippedDef) {
      int avgNew = (def->dmgMin + def->dmgMax) / 2;
      int avgOld = (equippedDef->dmgMin + equippedDef->dmgMax) / 2;
      int diff = avgNew - avgOld;
      char cmpBuf[48];
      if (diff > 0) {
        snprintf(cmpBuf, sizeof(cmpBuf), "  +%d avg damage", diff);
        AddPendingTooltipLine(IM_COL32(80, 255, 80, 255), cmpBuf);
      } else if (diff < 0) {
        snprintf(cmpBuf, sizeof(cmpBuf), "  %d avg damage", diff);
        AddPendingTooltipLine(IM_COL32(255, 80, 80, 255), cmpBuf);
      } else {
        AddPendingTooltipLine(IM_COL32(180, 180, 180, 255), "  same damage");
      }
    }
  }

  if (def->category <= 5 && def->attackSpeed > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Attack Speed: %d", def->attackSpeed);
    AddPendingTooltipLine(IM_COL32(200, 255, 200, 255), buf);
  }

  // Armor defense + comparison
  if (def->category >= 7 && def->category <= 11 && def->defense > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Defense: %d", def->defense);
    AddPendingTooltipLine(IM_COL32(140, 200, 255, 255), buf);

    if (equippedDef) {
      int diff = (int)def->defense - (int)equippedDef->defense;
      char cmpBuf[48];
      if (diff > 0) {
        snprintf(cmpBuf, sizeof(cmpBuf), "  +%d defense", diff);
        AddPendingTooltipLine(IM_COL32(80, 255, 80, 255), cmpBuf);
      } else if (diff < 0) {
        snprintf(cmpBuf, sizeof(cmpBuf), "  %d defense", diff);
        AddPendingTooltipLine(IM_COL32(255, 80, 80, 255), cmpBuf);
      } else {
        AddPendingTooltipLine(IM_COL32(180, 180, 180, 255), "  same defense");
      }
    }
  }

  if (def->buyPrice > 0) {
    char buf[64];
    std::string bStr = std::to_string(def->buyPrice);
    std::string sStr = std::to_string(def->buyPrice / 3);
    int n = bStr.length() - 3;
    while (n > 0) {
      bStr.insert(n, ",");
      n -= 3;
    }
    n = sStr.length() - 3;
    while (n > 0) {
      sStr.insert(n, ",");
      n -= 3;
    }
    snprintf(buf, sizeof(buf), "Buy: %s / Sell: %s Zen", bStr.c_str(),
             sStr.c_str());
    AddPendingTooltipLine(IM_COL32(255, 215, 0, 255), buf);
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
    addReq("Level", *s_ctx->serverLevel, def->levelReq);
  if (def->reqStr > 0)
    addReq("STR", *s_ctx->serverStr, def->reqStr);
  if (def->reqDex > 0)
    addReq("DEX", *s_ctx->serverDex, def->reqDex);
  if (def->reqVit > 0)
    addReq("VIT", *s_ctx->serverVit, def->reqVit);
  if (def->reqEne > 0)
    addReq("ENE", *s_ctx->serverEne, def->reqEne);

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
      uint32_t myFlag = (1 << (s_ctx->hero->GetClass() / 16));
      ImU32 col = (def->classFlags & myFlag) ? IM_COL32(160, 160, 255, 255)
                                             : IM_COL32(255, 80, 80, 255);
      AddPendingTooltipLine(col, classes);
    }
  }
}

void FlushPendingTooltip() {
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
  if (s_ctx->fontDefault)
    ImGui::PushFont(s_ctx->fontDefault);

  for (auto &line : g_pendingTooltip.lines) {
    if (line.text == "---") {
      // Horizontal separator
      dl->AddLine(ImVec2(tPos.x + 6, curY + 4),
                  ImVec2(tPos.x + tw - 6, curY + 4),
                  IM_COL32(80, 80, 120, 180));
      curY += 12;
    } else {
      dl->AddText(ImVec2(tPos.x + 10, curY), line.color, line.text.c_str());
      curY += 18.0f;
    }
  }

  if (s_ctx->fontDefault)
    ImGui::PopFont();
}

void UpdateAndRenderNotification(float deltaTime) {
  if (s_notifyTimer <= 0.0f)
    return;
  s_notifyTimer -= deltaTime;
  if (s_notifyTimer <= 0.0f) {
    s_notifyTimer = 0.0f;
    return;
  }

  float alpha = (s_notifyTimer < 0.5f) ? (s_notifyTimer / 0.5f) : 1.0f;
  uint8_t a = (uint8_t)(alpha * 255);

  ImDrawList *dl = ImGui::GetForegroundDrawList();
  ImVec2 displaySize = ImGui::GetIO().DisplaySize;

  if (s_ctx->fontDefault)
    ImGui::PushFont(s_ctx->fontDefault);
  ImVec2 textSize = ImGui::CalcTextSize(s_notifyText.c_str());
  float px = (displaySize.x - textSize.x) * 0.5f;
  float py = displaySize.y * 0.35f;

  // Background box
  float pad = 8.0f;
  dl->AddRectFilled(ImVec2(px - pad, py - pad),
                    ImVec2(px + textSize.x + pad, py + textSize.y + pad),
                    IM_COL32(10, 10, 20, (uint8_t)(a * 0.8f)), 4.0f);
  dl->AddText(ImVec2(px, py), IM_COL32(255, 80, 80, a), s_notifyText.c_str());
  if (s_ctx->fontDefault)
    ImGui::PopFont();
}

void ShowNotification(const char *msg) {
  s_notifyText = msg;
  s_notifyTimer = NOTIFY_DURATION;
}

void RenderCharInfoPanel(ImDrawList *dl, const UICoords &c) {
  float px = GetCharInfoPanelX(), py = PANEL_Y;
  float pw = PANEL_W,
        ph = PANEL_H +
             60.0f * g_uiPanelScale; // Extend to fit combat stats + atk speed

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
                        colTitle, s_ctx->fontDefault);

  DrawCloseButton(dl, c, px, py);

  // Basic Info
  DrawPanelText(dl, c, px, py, 20, 45, "Name", colLabel);
  DrawPanelTextRight(dl, c, px, py, 20, 45, 145, "TestDK", colValue);

  DrawPanelText(dl, c, px, py, 20, 65, "Class", colLabel);
  DrawPanelTextRight(dl, c, px, py, 20, 65, 145, "Dark Knight", colValue);

  DrawPanelText(dl, c, px, py, 20, 85, "Level", colLabel);
  snprintf(buf, sizeof(buf), "%d", *s_ctx->serverLevel);
  DrawPanelTextRight(dl, c, px, py, 20, 85, 145, buf, colGreen);

  // XP bar
  float xpFrac = 0.0f;
  uint64_t nextXp = s_ctx->hero->GetNextExperience();
  uint64_t curXp = (uint64_t)*s_ctx->serverXP;
  uint64_t prevXp = s_ctx->hero->CalcXPForLevel(*s_ctx->serverLevel);
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

  // Stats
  const char *statLabels[] = {"Strength", "Agility", "Vitality", "Energy"};
  int statValues[] = {*s_ctx->serverStr, *s_ctx->serverDex, *s_ctx->serverVit,
                      *s_ctx->serverEne};
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

    if (*s_ctx->serverLevelUpPoints > 0) {
      dl->AddRectFilled(ImVec2(c.ToScreenX(px + 155 * g_uiPanelScale),
                               c.ToScreenY(py + (ry + 2) * g_uiPanelScale)),
                        ImVec2(c.ToScreenX(px + 173 * g_uiPanelScale),
                               c.ToScreenY(py + (ry + 20) * g_uiPanelScale)),
                        IM_COL32(50, 150, 50, 255), 2.0f);
      DrawPanelText(dl, c, px, py, 158, ry + 3, "+", colValue);
    }
  }

  if (*s_ctx->serverLevelUpPoints > 0) {
    snprintf(buf, sizeof(buf), "Points: %d", *s_ctx->serverLevelUpPoints);
    DrawPanelText(dl, c, px, py, 15, 272, buf, colGreen);
  }

  // Combat Info (OpenMU DK formulas: STR/6 min, STR/4 max)
  int dMin = *s_ctx->serverStr / 6 + s_ctx->hero->GetWeaponBonusMin();
  int dMax = *s_ctx->serverStr / 4 + s_ctx->hero->GetWeaponBonusMax();

  snprintf(buf, sizeof(buf), "Damage: %d - %d", dMin, dMax);
  DrawPanelText(dl, c, px, py, 15, 300, buf, colValue);

  // Defense: base (DEX/3) + equipment
  int baseDef = *s_ctx->serverDex / 3;
  int addDef = s_ctx->hero->GetDefenseBonus();
  if (addDef > 0) {
    snprintf(buf, sizeof(buf), "Defense: %d + %d", baseDef, addDef);
  } else {
    snprintf(buf, sizeof(buf), "Defense: %d", baseDef);
  }
  DrawPanelText(dl, c, px, py, 15, 315, buf, colValue);

  // Defense Rate (OpenMU: DEX/3 for DK)
  int defRate = *s_ctx->serverDex / 3;
  snprintf(buf, sizeof(buf), "Def Rate: %d", defRate);
  DrawPanelText(dl, c, px, py, 15, 330, buf, colValue);

  // Crit/Excellent rates (OpenMU: flat 5% crit, 1% excellent)
  snprintf(buf, sizeof(buf), "Crit: 5%%");
  DrawPanelText(dl, c, px, py, 15, 345, buf, IM_COL32(100, 200, 255, 255));

  snprintf(buf, sizeof(buf), "Exc: 1%%");
  DrawPanelText(dl, c, px, py, 100, 345, buf, IM_COL32(100, 255, 100, 255));

  // Attack Speed (DEX/15 for DK)
  snprintf(buf, sizeof(buf), "Atk Speed: %d", *s_ctx->serverAttackSpeed);
  DrawPanelText(dl, c, px, py, 15, 360, buf, colValue);

  // Attack Rate / Hit Chance (Level*5 + DEX*3/2 + STR/4)
  int atkRate = *s_ctx->serverLevel * 5 + (*s_ctx->serverDex * 3) / 2 +
                *s_ctx->serverStr / 4;
  snprintf(buf, sizeof(buf), "Atk Rate: %d", atkRate);
  DrawPanelText(dl, c, px, py, 15, 375, buf, colValue);

  // HP / MP Bars
  int curHP = s_ctx->hero->GetHP();
  int maxHP = s_ctx->hero->GetMaxHP();
  int curMP = s_ctx->hero->GetMana();
  int maxMP = s_ctx->hero->GetMaxMana();

  float hpFrac = (maxHP > 0) ? (float)curHP / maxHP : 0.0f;
  float mpFrac = (maxMP > 0) ? (float)curMP / maxMP : 0.0f;

  float hbarX = 50, hbarY = 415, hbarW = 100, hbarH = 8;
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

  float mbarY = 435;
  // DK uses AG (Ability Gauge) instead of MP
  const char *manaLabel = (s_ctx->hero->GetClass() == 16) ? "AG" : "MP";
  DrawPanelText(dl, c, px, py, 15, mbarY - 2, manaLabel, colLabel);
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

void RenderInventoryPanel(ImDrawList *dl, const UICoords &c) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
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

  // Title
  DrawPanelTextCentered(dl, c, px, py, 0, 11, BASE_PANEL_W, "Inventory",
                        colTitle, s_ctx->fontDefault);

  DrawCloseButton(dl, c, px, py);

  // Equipment Layout
  for (auto &ep : g_equipLayoutRects) {
    float vx = px + ep.x * g_uiPanelScale;
    float vy = py + ep.y * g_uiPanelScale;
    float sw = ep.w * g_uiPanelScale;
    float sh = ep.h * g_uiPanelScale;

    ImVec2 sMin(c.ToScreenX(vx), c.ToScreenY(vy));
    ImVec2 sMax(c.ToScreenX(vx + sw), c.ToScreenY(vy + sh));

    bool hoverSlot =
        mp.x >= sMin.x && mp.x < sMax.x && mp.y >= sMin.y && mp.y < sMax.y;

    dl->AddRectFilled(sMin, sMax, colSlotBg, 3.0f);

    // Draw Background Placeholder if empty
    if (!s_ctx->equipSlots[ep.slot].equipped &&
        g_slotBackgrounds[ep.slot] != 0) {
      dl->AddImage((ImTextureID)(intptr_t)g_slotBackgrounds[ep.slot], sMin,
                   sMax);
    }

    dl->AddRect(sMin, sMax, hoverSlot && g_isDragging ? colDragHi : colSlotBr,
                3.0f);

    bool isBeingDragged = (g_isDragging && g_dragFromEquipSlot == ep.slot);

    if (s_ctx->equipSlots[ep.slot].equipped && !isBeingDragged) {
      std::string modelName = s_ctx->equipSlots[ep.slot].modelFile;
      if (!modelName.empty()) {
        int winH = (int)ImGui::GetIO().DisplaySize.y;
        int16_t defIdx = ItemDatabase::GetDefIndexFromCategory(
            s_ctx->equipSlots[ep.slot].category,
            s_ctx->equipSlots[ep.slot].itemIndex);
        g_renderQueue.push_back({modelName, defIdx, (int)sMin.x,
                                 winH - (int)sMax.y, (int)(sMax.x - sMin.x),
                                 (int)(sMax.y - sMin.y), hoverSlot});
      }
      if (hoverSlot) {
        AddPendingItemTooltip(ItemDatabase::GetDefIndexFromCategory(
                                  s_ctx->equipSlots[ep.slot].category,
                                  s_ctx->equipSlots[ep.slot].itemIndex),
                              s_ctx->equipSlots[ep.slot].itemLevel);
      }
      // Always show +Level overlay if > 0
      if (s_ctx->equipSlots[ep.slot].itemLevel > 0) {
        char lvlBuf[8];
        snprintf(lvlBuf, sizeof(lvlBuf), "+%d",
                 s_ctx->equipSlots[ep.slot].itemLevel);
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

      if (s_ctx->inventory[slot].occupied) {
        auto it = g_itemDefs.find(s_ctx->inventory[slot].defIndex);
        if (it != g_itemDefs.end()) {
          const auto &def = it->second;
          // Mark ENTIRE footprint as processed
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
                                  ? ItemDatabase::GetDropModelName(
                                        s_ctx->inventory[slot].defIndex)
                                  : def.modelFile.c_str();
          if (model && model[0]) {
            int winH = (int)ImGui::GetIO().DisplaySize.y;
            g_renderQueue.push_back({model, s_ctx->inventory[slot].defIndex,
                                     (int)iMin.x, winH - (int)iMax.y,
                                     (int)(iMax.x - iMin.x),
                                     (int)(iMax.y - iMin.y), hoverItem});
          }
          if (hoverItem && !g_isDragging)
            AddPendingItemTooltip(s_ctx->inventory[slot].defIndex,
                                  s_ctx->inventory[slot].itemLevel);

          // Level overlay
          if (s_ctx->inventory[slot].itemLevel > 0) {
            char lvlBuf[8];
            snprintf(lvlBuf, sizeof(lvlBuf), "+%d",
                     s_ctx->inventory[slot].itemLevel);
            dl->AddText(ImVec2(iMin.x + 2, iMin.y + 2),
                        IM_COL32(255, 200, 80, 255), lvlBuf);
          }

          // Quantity overlay (deferred — drawn after 3D item models)
          if (s_ctx->inventory[slot].quantity > 1) {
            DeferredOverlay ov;
            snprintf(ov.text, sizeof(ov.text), "%d",
                     s_ctx->inventory[slot].quantity);
            ImVec2 qSize = ImGui::CalcTextSize(ov.text);
            ov.x = iMax.x - qSize.x - 2;
            ov.y = iMax.y - qSize.y - 1;
            ov.color = IM_COL32(255, 255, 255, 255);
            g_deferredOverlays.push_back(ov);
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
                if (s_ctx->inventory[s].occupied) {
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

  // Tooltip on hover (bag items)
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int slot = row * 8 + col;
      if (!s_ctx->inventory[slot].occupied || !s_ctx->inventory[slot].primary)
        continue;

      auto it = g_itemDefs.find(s_ctx->inventory[slot].defIndex);
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
        AddPendingItemTooltip(s_ctx->inventory[slot].defIndex,
                              s_ctx->inventory[slot].itemLevel);
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
    std::string s = std::to_string(*s_ctx->zen);
    int n = s.length() - 3;
    while (n > 0) {
      s.insert(n, ",");
      n -= 3;
    }
    snprintf(zenBuf, sizeof(zenBuf), "%s Zen", s.c_str());
    DrawPanelTextRight(dl, c, px, py, 10, 405, 160, zenBuf, colGold);
  }
}

void RenderShopPanel(ImDrawList *dl, const UICoords &c) {
  if (!*s_ctx->shopOpen)
    return;
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
  float px = GetShopPanelX(), py = PANEL_Y;
  float pw = PANEL_W, ph = PANEL_H;
  ImVec2 mp = ImGui::GetIO().MousePos;

  const ImU32 colBg = IM_COL32(15, 15, 25, 240);
  const ImU32 colBr = IM_COL32(60, 65, 90, 200);
  const ImU32 colTitle = IM_COL32(255, 210, 80, 255);
  const ImU32 colValue = IM_COL32(255, 255, 255, 255);

  dl->AddRectFilled(ImVec2(c.ToScreenX(px), c.ToScreenY(py)),
                    ImVec2(c.ToScreenX(px + pw), c.ToScreenY(py + ph)), colBg,
                    5.0f);
  dl->AddRect(ImVec2(c.ToScreenX(px), c.ToScreenY(py)),
              ImVec2(c.ToScreenX(px + pw), c.ToScreenY(py + ph)), colBr, 5.0f,
              0, 1.5f);

  DrawPanelTextCentered(dl, c, px, py, 0, 11, BASE_PANEL_W, "Shop", colTitle,
                        s_ctx->fontDefault);

  DrawCloseButton(dl, c, px, py);

  // Detect new shop list from server (items changed)
  size_t curCount = s_ctx->shopItems->size();
  if (curCount != s_lastShopItemCount) {
    s_shopGridDirty = true;
    s_lastShopItemCount = curCount;
  }
  if (s_shopGridDirty)
    RebuildShopGrid();

  // Grid constants (same cell size as inventory bag grid)
  float gridRX = 15.0f, gridRY = 35.0f;
  float cellW = 20.0f, cellH = 20.0f;

  const ImU32 colSlotBg = IM_COL32(0, 0, 0, 150);
  const ImU32 colSlotBr = IM_COL32(80, 75, 60, 255);

  // Draw empty grid cells
  for (int row = 0; row < s_shopGridRows; row++) {
    for (int col = 0; col < SHOP_GRID_COLS; col++) {
      float rX = gridRX + col * cellW;
      float rY = gridRY + row * cellH;
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

  // Draw shop items in grid
  bool processed[SHOP_GRID_MAX_SLOTS] = {false};
  for (int row = 0; row < s_shopGridRows; row++) {
    for (int col = 0; col < SHOP_GRID_COLS; col++) {
      int slot = row * SHOP_GRID_COLS + col;
      if (processed[slot])
        continue;
      if (!s_shopGrid[slot].occupied || !s_shopGrid[slot].primary)
        continue;

      auto it = g_itemDefs.find(s_shopGrid[slot].defIndex);
      if (it == g_itemDefs.end())
        continue;
      const auto &def = it->second;

      // Mark entire footprint as processed
      for (int dy = 0; dy < def.height; dy++)
        for (int dx = 0; dx < def.width; dx++) {
          int s = (row + dy) * SHOP_GRID_COLS + (col + dx);
          if (s < SHOP_GRID_MAX_SLOTS)
            processed[s] = true;
        }

      float rX = gridRX + col * cellW;
      float rY = gridRY + row * cellH;
      float vX = px + rX * g_uiPanelScale;
      float vY = py + rY * g_uiPanelScale;
      ImVec2 iMin(c.ToScreenX(vX), c.ToScreenY(vY));
      ImVec2 iMax(c.ToScreenX(vX + def.width * cellW * g_uiPanelScale),
                  c.ToScreenY(vY + def.height * cellH * g_uiPanelScale));

      bool hoverItem =
          mp.x >= iMin.x && mp.x < iMax.x && mp.y >= iMin.y && mp.y < iMax.y;

      dl->AddRectFilled(iMin, iMax,
                        hoverItem ? IM_COL32(255, 255, 255, 30)
                                  : IM_COL32(0, 0, 0, 40),
                        2.0f);

      const char *model =
          def.modelFile.empty()
              ? ItemDatabase::GetDropModelName(s_shopGrid[slot].defIndex)
              : def.modelFile.c_str();
      if (model && model[0]) {
        int winH = (int)ImGui::GetIO().DisplaySize.y;
        g_renderQueue.push_back({model, s_shopGrid[slot].defIndex, (int)iMin.x,
                                 winH - (int)iMax.y, (int)(iMax.x - iMin.x),
                                 (int)(iMax.y - iMin.y), hoverItem});
      }

      if (hoverItem)
        AddPendingItemTooltip(s_shopGrid[slot].defIndex,
                              s_shopGrid[slot].itemLevel);

      if (s_shopGrid[slot].itemLevel > 0) {
        char lvlBuf[8];
        snprintf(lvlBuf, sizeof(lvlBuf), "+%d", s_shopGrid[slot].itemLevel);
        dl->AddText(ImVec2(iMin.x + 2, iMin.y + 2), IM_COL32(255, 200, 80, 255),
                    lvlBuf);
      }
    }
  }
}

bool HandlePanelClick(float vx, float vy) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();

  // Shop Panel
  if (*s_ctx->shopOpen && IsPointInPanel(vx, vy, GetShopPanelX())) {
    float px = GetShopPanelX(), py = PANEL_Y;
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;

    // Close button
    if (relX >= 190 - 24 && relX < 190 - 8 && relY >= 6 && relY < 18) {
      *s_ctx->shopOpen = false;
      s_shopGridDirty = true;
      return true;
    }

    // Grid hit test
    float gridRX = 15.0f, gridRY = 35.0f;
    float cellW = 20.0f, cellH = 20.0f;

    if (relX >= gridRX && relX < gridRX + SHOP_GRID_COLS * cellW &&
        relY >= gridRY && relY < gridRY + s_shopGridRows * cellH) {
      int col = (int)((relX - gridRX) / cellW);
      int row = (int)((relY - gridRY) / cellH);
      int slot = row * SHOP_GRID_COLS + col;

      if (slot >= 0 && slot < SHOP_GRID_MAX_SLOTS &&
          s_shopGrid[slot].occupied) {
        // Find primary slot
        int primarySlot = slot;
        if (!s_shopGrid[slot].primary) {
          int16_t di = s_shopGrid[slot].defIndex;
          for (int r = 0; r <= row; r++) {
            for (int c2 = 0; c2 <= col; c2++) {
              int s = r * SHOP_GRID_COLS + c2;
              if (s_shopGrid[s].occupied && s_shopGrid[s].primary &&
                  s_shopGrid[s].defIndex == di) {
                auto it2 = g_itemDefs.find(di);
                if (it2 != g_itemDefs.end()) {
                  if (r + it2->second.height > row &&
                      c2 + it2->second.width > col) {
                    primarySlot = s;
                  }
                }
              }
            }
          }
        }

        // Buy item on left click — check zen first
        if (s_shopGrid[primarySlot].buyPrice > *s_ctx->zen) {
          ShowNotification("Not enough Zen!");
          return true;
        }
        s_ctx->server->SendShopBuy(s_shopGrid[primarySlot].defIndex,
                                   s_shopGrid[primarySlot].itemLevel, 1);
        return true;
      }
    }

    return true;
  }

  // Character Info panel
  if (*s_ctx->showCharInfo && IsPointInPanel(vx, vy, GetCharInfoPanelX())) {
    float px = GetCharInfoPanelX(), py = PANEL_Y;
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;

    // Close button (relative: 190 - 24, 6, size 16, 12)
    if (relX >= 190 - 24 && relX < 190 - 8 && relY >= 6 && relY < 18) {
      *s_ctx->showCharInfo = false;
      return true;
    }

    // Stat "+" buttons
    float statRowYOffsets[] = {150, 182, 214, 246};
    if (*s_ctx->serverLevelUpPoints > 0) {
      for (int i = 0; i < 4; i++) {
        float btnX = 155, btnY = statRowYOffsets[i] + 2;
        if (relX >= btnX && relX < btnX + 18 && relY >= btnY &&
            relY < btnY + 18) {
          s_ctx->server->SendStatAlloc(static_cast<uint8_t>(i));
          return true;
        }
      }
    }
    return true; // Click consumed by panel
  }

  // Inventory panel (also visible when shop is open)
  if ((*s_ctx->showInventory || *s_ctx->shopOpen) &&
      IsPointInPanel(vx, vy, GetInventoryPanelX())) {
    float px = GetInventoryPanelX(), py = PANEL_Y;
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;

    // Close button
    if (relX >= 190 - 24 && relX < 190 - 8 && relY >= 6 && relY < 18) {
      *s_ctx->showInventory = false;
      return true;
    }

    // Equipment slots: start drag
    for (const auto &ep : g_equipLayoutRects) {
      if (relX >= ep.x && relX < ep.x + ep.w && relY >= ep.y &&
          relY < ep.y + ep.h) {
        if (s_ctx->equipSlots[ep.slot].equipped) {
          g_dragFromSlot = -1;
          g_dragFromEquipSlot = ep.slot;

          g_dragDefIndex = ItemDatabase::GetDefIndexFromCategory(
              s_ctx->equipSlots[ep.slot].category,
              s_ctx->equipSlots[ep.slot].itemIndex);
          if (g_dragDefIndex == -1)
            g_dragDefIndex = 0; // Fallback

          g_dragQuantity = 1;
          g_dragItemLevel = s_ctx->equipSlots[ep.slot].itemLevel;
          g_isDragging = true;
        }
        return true;
      }
    }

    // Bag grid: start drag
    float gridRX = 15.0f, gridRY = 208.0f;
    float cellW = 20.0f, cellH = 20.0f, gap = 0.0f;

    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        int slot = row * 8 + col;
        float cx = gridRX + col * (cellW + gap);
        float cy = gridRY + row * (cellH + gap);
        if (relX >= cx && relX < cx + cellW && relY >= cy &&
            relY < cy + cellH) {
          if (s_ctx->inventory[slot].occupied) {
            // Find primary slot if this is a secondary one
            int primarySlot = slot;
            if (!s_ctx->inventory[slot].primary) {
              int16_t di = s_ctx->inventory[slot].defIndex;
              bool found = false;
              for (int r = 0; r <= row && !found; r++) {
                for (int c = 0; c <= col && !found; c++) {
                  int s = r * 8 + c;
                  if (s_ctx->inventory[s].occupied &&
                      s_ctx->inventory[s].primary &&
                      s_ctx->inventory[s].defIndex == di) {
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
            g_dragDefIndex = s_ctx->inventory[primarySlot].defIndex;
            g_dragQuantity = s_ctx->inventory[primarySlot].quantity;
            g_dragItemLevel = s_ctx->inventory[primarySlot].itemLevel;
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
  if (vy >= s_ctx->hudCoords->ToVirtualY((float)winH - 60)) {
    // Center is 640 in virtual space (1280 wide)
    if (vx >= 615 && vx <= 665) {
      if (*s_ctx->quickSlotDefIndex != -1) {
        g_isDragging = true;
        g_dragFromQuickSlot = true;
        g_dragDefIndex = *s_ctx->quickSlotDefIndex;
        g_dragFromSlot = -1;
        g_dragFromEquipSlot = -1;
        std::cout << "[QuickSlot] Started dragging from Q" << std::endl;
        return true;
      }
    }
  }

  return false;
}

bool HandlePanelRightClick(float vx, float vy) {
  if (!*s_ctx->shopOpen)
    return false;
  auto &g_itemDefs = ItemDatabase::GetItemDefs();

  // Check showInventory OR shopOpen — when shop is open, inventory is
  // force-shown during rendering but the flag itself may be false
  if ((*s_ctx->showInventory || *s_ctx->shopOpen) &&
      IsPointInPanel(vx, vy, GetInventoryPanelX())) {
    float px = GetInventoryPanelX(), py = PANEL_Y;
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;
    float gridRX = 15.0f, gridRY = 208.0f, cellW = 20.0f, cellH = 20.0f;
    if (relX >= gridRX && relX < gridRX + 8 * cellW && relY >= gridRY &&
        relY < gridRY + 8 * cellH) {
      int col = (int)((relX - gridRX) / cellW);
      int row = (int)((relY - gridRY) / cellH);
      int targetSlot = row * 8 + col;
      if (s_ctx->inventory[targetSlot].occupied) {
        int primarySlot = targetSlot;
        if (!s_ctx->inventory[targetSlot].primary) {
          int16_t di = s_ctx->inventory[targetSlot].defIndex;
          for (int r = 0; r <= row; ++r) {
            for (int c = 0; c <= col; ++c) {
              int s = r * 8 + c;
              if (s_ctx->inventory[s].occupied && s_ctx->inventory[s].primary &&
                  s_ctx->inventory[s].defIndex == di) {
                primarySlot = s;
              }
            }
          }
        }
        s_ctx->server->SendShopSell(primarySlot);
      }
      return true;
    }
  }

  return false;
}

void HandlePanelMouseUp(GLFWwindow *window, float vx, float vy) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();

  if (!g_isDragging)
    return;
  bool wasDragging = g_isDragging;
  g_isDragging = false;

  int winW, winH;
  glfwGetWindowSize(window, &winW, &winH);
  bool droppedOnHUD = (vy >= s_ctx->hudCoords->ToVirtualY((float)winH - 60));

  if (g_dragFromQuickSlot) {
    if (!droppedOnHUD) {
      *s_ctx->quickSlotDefIndex = -1;
      std::cout << "[QuickSlot] Cleared assignment (dragged out)" << std::endl;
    }
    g_dragFromQuickSlot = false;
    return;
  }

  if (*s_ctx->showInventory || *s_ctx->shopOpen) {
    float px = GetInventoryPanelX(), py = PANEL_Y;
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;

    // 1. Check drop on Equipment slots
    for (const auto &ep : g_equipLayoutRects) {
      if (relX >= ep.x && relX < ep.x + ep.w && relY >= ep.y &&
          relY < ep.y + ep.h) {
        // Dragging FROM Inventory TO Equipment
        if (g_dragFromSlot >= 0) {
          if (!CanEquipItem(g_dragDefIndex)) {
            g_isDragging = false;
            g_dragFromSlot = -1;
            return;
          }

          uint8_t cat, idx;
          ItemDatabase::GetItemCategoryAndIndex(g_dragDefIndex, cat, idx);

          // Enforce Strict Slot Category Compatibility (Main 5.2 logic)
          bool validSlot = false;
          switch (ep.slot) {
          case 0: // R.Hand
            validSlot = (cat <= 5);
            break;
          case 1: // L.Hand
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

          // Enforce Hand Compatibility (Main 5.2 rules)
          if (ep.slot == 0 && cat == 6) {
            std::cout << "[UI] Cannot equip Shield in Right Hand!" << std::endl;
            g_isDragging = false;
            g_dragFromSlot = -1;
            return;
          }
          // L.Hand: weapons (cat 0-5) only for DK dual-wield, one-handed only
          if (ep.slot == 1 && cat <= 5) {
            uint8_t baseClass = s_ctx->hero->GetClass() >> 4;
            bool canDualWield =
                (baseClass == 1 || baseClass == 3); // 1=DK, 3=MG
            auto defIt = g_itemDefs.find(g_dragDefIndex);
            bool isTwoHanded =
                (defIt != g_itemDefs.end() && defIt->second.twoHanded);
            if (!canDualWield) {
              ShowNotification("Only Dark Knights can dual-wield!");
              g_isDragging = false;
              g_dragFromSlot = -1;
              return;
            }
            if (isTwoHanded) {
              ShowNotification("Two-handed weapons can't go in left hand!");
              g_isDragging = false;
              g_dragFromSlot = -1;
              return;
            }
          }

          // Prepare logic for swap if equipped
          ClientInventoryItem swapItem = {};
          if (s_ctx->equipSlots[ep.slot].equipped) {
            swapItem.defIndex = ItemDatabase::GetDefIndexFromCategory(
                s_ctx->equipSlots[ep.slot].category,
                s_ctx->equipSlots[ep.slot].itemIndex);
            swapItem.quantity = 1;
            swapItem.itemLevel = s_ctx->equipSlots[ep.slot].itemLevel;
            swapItem.occupied = true;
          }

          // Equip the new item
          s_ctx->equipSlots[ep.slot].category = cat;
          s_ctx->equipSlots[ep.slot].itemIndex = idx;
          s_ctx->equipSlots[ep.slot].itemLevel = g_dragItemLevel;
          s_ctx->equipSlots[ep.slot].equipped = true;
          s_ctx->equipSlots[ep.slot].modelFile =
              ItemDatabase::GetDropModelName(g_dragDefIndex);

          auto defIt = g_itemDefs.find(g_dragDefIndex);

          // Handle conflict: 2H weapon in R.Hand forces L.Hand uneq
          if (ep.slot == 0 && defIt != g_itemDefs.end() &&
              defIt->second.twoHanded) {
            if (s_ctx->equipSlots[1].equipped) {
              std::cout << "[UI] 2H weapon equipped: auto-unequipping left hand"
                        << std::endl;
              // Unequip slot 1 locally (ideally server handles this, but we
              // force it)
              s_ctx->server->SendUnequip(*s_ctx->heroCharacterId, 1);
              s_ctx->equipSlots[1].equipped = false;
              s_ctx->equipSlots[1].category = 0xFF;
              WeaponEquipInfo emptyInfo;
              s_ctx->hero->EquipShield(emptyInfo);
            }
          }

          // Update Hero Visuals Immediately
          WeaponEquipInfo info;
          info.category = cat;
          info.itemIndex = idx;
          info.itemLevel = g_dragItemLevel;
          info.modelFile = s_ctx->equipSlots[ep.slot].modelFile;
          // Resolve twoHanded flag for animation selection
          if (defIt != g_itemDefs.end())
            info.twoHanded = defIt->second.twoHanded;

          if (ep.slot == 0)
            s_ctx->hero->EquipWeapon(info);
          if (ep.slot == 1)
            s_ctx->hero->EquipShield(info);

          // Body part equipment (Helm/Armor/Pants/Gloves/Boots)
          int bodyPart = ItemDatabase::GetBodyPartIndex(cat);
          if (bodyPart >= 0) {
            std::string partModel =
                ItemDatabase::GetBodyPartModelFile(cat, idx);
            if (!partModel.empty())
              s_ctx->hero->EquipBodyPart(bodyPart, partModel);
          }

          // Send Equip packet
          if (*s_ctx->syncDone) {
            s_ctx->server->SendEquip(*s_ctx->heroCharacterId,
                                     static_cast<uint8_t>(ep.slot), cat, idx,
                                     g_dragItemLevel);
          }

          // Handle source slot: clear dragged item, then place swapped item
          ClearBagItem(g_dragFromSlot);

          // If there was an old equipped item, place it in the vacated bag slot
          if (swapItem.occupied && swapItem.defIndex >= 0) {
            SetBagItem(g_dragFromSlot, swapItem.defIndex, swapItem.quantity,
                       swapItem.itemLevel);
            // Server handles the swap: unequip old + equip new via the
            // HandleEquip flow which saves unequipped items to inventory
            std::cout << "[UI] Swapped: old equip (def=" << swapItem.defIndex
                      << ") -> Inv " << g_dragFromSlot << std::endl;
          }
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
    if (vy >= s_ctx->hudCoords->ToVirtualY((float)winH - 60)) {
      auto it = g_itemDefs.find(g_dragDefIndex);
      if (it != g_itemDefs.end() && it->second.category == 14) {
        *s_ctx->quickSlotDefIndex = g_dragDefIndex;
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
            s_ctx->equipSlots[g_dragFromEquipSlot].equipped = false;
            s_ctx->equipSlots[g_dragFromEquipSlot].category = 0xFF;

            // Update Hero Visuals Immediately
            WeaponEquipInfo info;
            if (g_dragFromEquipSlot == 0)
              s_ctx->hero->EquipWeapon(info);
            if (g_dragFromEquipSlot == 1)
              s_ctx->hero->EquipShield(info);

            if (g_dragFromEquipSlot >= 2 && g_dragFromEquipSlot <= 6) {
              int partIdx = g_dragFromEquipSlot - 2;
              s_ctx->hero->EquipBodyPart(partIdx, "");
            }

            // Send Unequip packet
            if (*s_ctx->syncDone)
              s_ctx->server->SendUnequip(
                  *s_ctx->heroCharacterId,
                  static_cast<uint8_t>(g_dragFromEquipSlot));

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
            if (*s_ctx->syncDone)
              s_ctx->server->SendInventoryMove(
                  static_cast<uint8_t>(g_dragFromSlot),
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

  // 4. Drop item to ground — dragged from bag slot and released outside panels
  if (g_dragFromSlot >= 0 && *s_ctx->syncDone) {
    bool insideInvPanel = IsPointInPanel(vx, vy, GetInventoryPanelX());
    bool insideCharPanel =
        *s_ctx->showCharInfo && IsPointInPanel(vx, vy, GetCharInfoPanelX());
    bool insideShopPanel =
        *s_ctx->shopOpen && IsPointInPanel(vx, vy, GetShopPanelX());

    if (!insideInvPanel && !insideCharPanel && !insideShopPanel &&
        !droppedOnHUD) {
      s_ctx->server->SendDropItem(static_cast<uint8_t>(g_dragFromSlot));
      std::cout << "[UI] Dropped item from slot " << g_dragFromSlot
                << " to ground" << std::endl;
    }
  }

  g_dragFromSlot = -1;
  g_dragFromEquipSlot = -1;
}

bool IsDragging() { return g_isDragging; }

int16_t GetDragDefIndex() { return g_dragDefIndex; }

const std::vector<ItemRenderJob> &GetRenderQueue() { return g_renderQueue; }

void ClearRenderQueue() {
  g_renderQueue.clear();
  g_deferredOverlays.clear();
}

void AddRenderJob(const ItemRenderJob &job) { g_renderQueue.push_back(job); }

bool HasDeferredOverlays() { return !g_deferredOverlays.empty(); }

void FlushDeferredOverlays() {
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  for (const auto &ov : g_deferredOverlays) {
    // Shadow
    dl->AddText(ImVec2(ov.x + 1, ov.y + 1), IM_COL32(0, 0, 0, 220), ov.text);
    dl->AddText(ImVec2(ov.x, ov.y), ov.color, ov.text);
  }
}

bool HasPendingTooltip() { return g_pendingTooltip.active; }

void ResetPendingTooltip() { g_pendingTooltip.active = false; }

void LoadSlotBackgrounds(const std::string &dataPath) {
  g_texInventoryBg =
      UITexture::Load(dataPath + "/Interface/mu_inventory_bg.png");

  g_slotBackgrounds[0] = TextureLoader::Resolve(dataPath + "/Interface",
                                                "newui_item_weapon(R).OZT");
  g_slotBackgrounds[1] = TextureLoader::Resolve(dataPath + "/Interface",
                                                "newui_item_weapon(L).OZT");
  g_slotBackgrounds[2] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_cap.OZT");
  g_slotBackgrounds[3] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_upper.OZT");
  g_slotBackgrounds[4] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_lower.OZT");
  g_slotBackgrounds[5] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_gloves.OZT");
  g_slotBackgrounds[6] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_boots.OZT");
  g_slotBackgrounds[7] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_wing.OZT");
  g_slotBackgrounds[8] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_fairy.OZT");
  g_slotBackgrounds[9] = TextureLoader::Resolve(dataPath + "/Interface",
                                                "newui_item_necklace.OZT");
  g_slotBackgrounds[10] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_ring.OZT");
  g_slotBackgrounds[11] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_ring.OZT");

  // Skill icon sprite sheet (25 icons per row, 20x28px each on 512x512)
  g_texSkillIcons = TextureLoader::LoadOZJ(dataPath + "/Interface/Skill.OZJ");
  if (g_texSkillIcons) {
    // LINEAR for smooth upscaling, CLAMP_TO_EDGE to reduce edge bleed
    glBindTexture(GL_TEXTURE_2D, g_texSkillIcons);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    printf("[UI] Loaded Skill.OZJ icon sheet (tex=%u)\n", g_texSkillIcons);
  }
}

float GetCharInfoPanelX() { return PANEL_X_RIGHT; }

float GetInventoryPanelX() {
  return *s_ctx->showCharInfo ? PANEL_X_RIGHT - PANEL_W - 5.0f : PANEL_X_RIGHT;
}

float GetShopPanelX() { return GetInventoryPanelX() - PANEL_W - 5.0f; }

bool IsPointInPanel(float vx, float vy, float panelX) {
  return vx >= panelX && vx < panelX + PANEL_W && vy >= PANEL_Y &&
         vy < PANEL_Y + PANEL_H;
}

// ═══════════════════════════════════════════════════════════════════
// Skill Window (S key) — DK 0.97d / Blade Knight skills
// ═══════════════════════════════════════════════════════════════════

// DK skill data (0.97d scope + Blade Knight)
struct DKSkillDef {
  uint8_t skillId;
  const char *name;
  int agCost;
  int levelReq;
  int damageBonus;
  const char *desc;
};

static const DKSkillDef g_dkSkills[] = {
    {19, "Falling Slash", 9, 1, 15, "Downward slash attack"},
    {20, "Lunge", 9, 1, 15, "Forward thrust attack"},
    {21, "Uppercut", 8, 1, 15, "Upward strike attack"},
    {22, "Cyclone", 9, 1, 18, "Spinning attack"},
    {23, "Slash", 10, 1, 20, "Horizontal slash"},
    {41, "Twisting Slash", 10, 30, 25, "AoE spinning slash"},
    {42, "Rageful Blow", 20, 170, 60, "Powerful ground strike"},
    {43, "Death Stab", 12, 160, 70, "Piercing stab attack"},
};
static constexpr int NUM_DK_SKILLS = 8;

// Skill icon sprite sheet: 25 cols, 20x28px per icon, 512x512 texture
static constexpr int SKILL_ICON_COLS = 25;
static constexpr float SKILL_ICON_W = 20.0f;
static constexpr float SKILL_ICON_H = 28.0f;
static constexpr float SKILL_TEX_SIZE = 512.0f;

void RenderSkillPanel(ImDrawList *dl, const UICoords &c) {
  // Grid layout: 4 columns x 2 rows of skill cells
  static constexpr int GRID_COLS = 4;
  static constexpr int GRID_ROWS = 2;
  static constexpr float CELL_W = 110.0f;
  static constexpr float CELL_H = 105.0f;
  static constexpr float CELL_PAD = 10.0f;
  static constexpr float TITLE_H = 32.0f;
  static constexpr float FOOTER_H = 24.0f;
  static constexpr float MARGIN = 16.0f;

  float pw = MARGIN * 2 + GRID_COLS * CELL_W + (GRID_COLS - 1) * CELL_PAD;
  float ph = TITLE_H + MARGIN + GRID_ROWS * CELL_H + (GRID_ROWS - 1) * CELL_PAD + FOOTER_H + MARGIN;

  // Center on screen
  float px = (UICoords::VIRTUAL_W - pw) * 0.5f;
  float py = (UICoords::VIRTUAL_H - ph) * 0.5f;

  // Colors
  const ImU32 colBg = IM_COL32(15, 15, 25, 240);
  const ImU32 colBr = IM_COL32(60, 65, 90, 200);
  const ImU32 colTitle = IM_COL32(255, 210, 80, 255);
  const ImU32 colLabel = IM_COL32(170, 170, 190, 255);
  const ImU32 colValue = IM_COL32(255, 255, 255, 255);
  const ImU32 colGreen = IM_COL32(100, 255, 100, 255);
  const ImU32 colRed = IM_COL32(255, 80, 80, 255);
  const ImU32 colDim = IM_COL32(255, 255, 255, 100);
  char buf[256];

  // Background
  dl->AddRectFilled(ImVec2(c.ToScreenX(px), c.ToScreenY(py)),
                    ImVec2(c.ToScreenX(px + pw), c.ToScreenY(py + ph)), colBg,
                    6.0f);
  dl->AddRect(ImVec2(c.ToScreenX(px), c.ToScreenY(py)),
              ImVec2(c.ToScreenX(px + pw), c.ToScreenY(py + ph)), colBr, 6.0f,
              0, 1.5f);

  // Title — centered
  {
    const char *title = "Skills";
    ImVec2 tsz = ImGui::CalcTextSize(title);
    float tx = c.ToScreenX(px + pw * 0.5f) - tsz.x * 0.5f;
    float ty = c.ToScreenY(py + 10.0f);
    dl->AddText(ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 180), title);
    dl->AddText(ImVec2(tx, ty), colTitle, title);
  }

  // Close button (top-right)
  {
    float bx = px + pw - 24.0f;
    float by = py + 8.0f;
    float bw = 16.0f, bh = 16.0f;
    ImVec2 bMin(c.ToScreenX(bx), c.ToScreenY(by));
    ImVec2 bMax(c.ToScreenX(bx + bw), c.ToScreenY(by + bh));
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool hovered =
        mp.x >= bMin.x && mp.x < bMax.x && mp.y >= bMin.y && mp.y < bMax.y;
    ImU32 bgCol =
        hovered ? IM_COL32(200, 40, 40, 255) : IM_COL32(100, 20, 20, 200);
    dl->AddRectFilled(bMin, bMax, bgCol, 2.0f);
    if (hovered)
      dl->AddRect(bMin, bMax, IM_COL32(255, 100, 100, 255), 2.0f);
    ImVec2 xSize = ImGui::CalcTextSize("X");
    ImVec2 xPos(bMin.x + (bMax.x - bMin.x) * 0.5f - xSize.x * 0.5f,
                bMin.y + (bMax.y - bMin.y) * 0.5f - xSize.y * 0.5f);
    dl->AddText(xPos, IM_COL32(255, 255, 255, 255), "X");
    if (hovered && ImGui::IsMouseClicked(0))
      *s_ctx->showSkillWindow = false;
  }

  // Check which skills are learned
  auto isLearned = [](uint8_t skillId) -> bool {
    if (!s_ctx->learnedSkills)
      return false;
    for (auto s : *s_ctx->learnedSkills) {
      if (s == skillId)
        return true;
    }
    return false;
  };

  ImVec2 mousePos = ImGui::GetIO().MousePos;

  // Icon display size (preserve 20:28 aspect ratio = 5:7)
  static constexpr float ICON_DISP_W = 46.0f;
  static constexpr float ICON_DISP_H = 64.0f; // 46 * 28/20 = 64.4

  float gridStartY = py + TITLE_H;

  for (int i = 0; i < NUM_DK_SKILLS; i++) {
    const auto &skill = g_dkSkills[i];
    bool learned = isLearned(skill.skillId);

    int col = i % GRID_COLS;
    int row = i / GRID_COLS;
    float cx = px + MARGIN + col * (CELL_W + CELL_PAD);
    float cy = gridStartY + row * (CELL_H + CELL_PAD);

    // Cell background
    ImVec2 cMin(c.ToScreenX(cx), c.ToScreenY(cy));
    ImVec2 cMax(c.ToScreenX(cx + CELL_W), c.ToScreenY(cy + CELL_H));

    bool cellHovered = mousePos.x >= cMin.x && mousePos.x < cMax.x &&
                       mousePos.y >= cMin.y && mousePos.y < cMax.y;

    ImU32 cellBg = cellHovered ? IM_COL32(40, 45, 65, 220)
                               : IM_COL32(25, 28, 40, 200);
    dl->AddRectFilled(cMin, cMax, cellBg, 4.0f);
    dl->AddRect(cMin, cMax, IM_COL32(50, 55, 75, 150), 4.0f);

    // Icon centered at top of cell
    if (g_texSkillIcons != 0) {
      int iconIdx = skill.skillId;
      int ic = iconIdx % SKILL_ICON_COLS;
      int ir = iconIdx / SKILL_ICON_COLS;
      // Tiny inset (0.1 texels) prevents bilinear bleed from neighbors
      static constexpr float UV_INSET = 0.1f / SKILL_TEX_SIZE;
      float u0 = (SKILL_ICON_W * ic) / SKILL_TEX_SIZE + UV_INSET;
      float v0 = (SKILL_ICON_H * ir) / SKILL_TEX_SIZE + UV_INSET;
      float u1 = (SKILL_ICON_W * (ic + 1)) / SKILL_TEX_SIZE - UV_INSET;
      float v1 = (SKILL_ICON_H * (ir + 1)) / SKILL_TEX_SIZE - UV_INSET;

      float iconX = cx + (CELL_W - ICON_DISP_W) * 0.5f;
      float iconY = cy + 6.0f;

      ImVec2 iMin(c.ToScreenX(iconX), c.ToScreenY(iconY));
      ImVec2 iMax(c.ToScreenX(iconX + ICON_DISP_W),
                  c.ToScreenY(iconY + ICON_DISP_H));

      ImU32 iconTint = learned ? IM_COL32(255, 255, 255, 255)
                               : IM_COL32(255, 255, 255, 100);
      dl->AddImage((ImTextureID)(uintptr_t)g_texSkillIcons, iMin, iMax,
                   ImVec2(u0, v0), ImVec2(u1, v1), iconTint);
    }

    // Skill name centered below icon
    {
      ImU32 nameCol = learned ? colValue : colDim;
      ImVec2 nsz = ImGui::CalcTextSize(skill.name);
      float nameY = cy + 6.0f + ICON_DISP_H + 4.0f;
      // Clamp text to cell width
      float nx = c.ToScreenX(cx + CELL_W * 0.5f) - nsz.x * 0.5f;
      float cellLeft = c.ToScreenX(cx + 2.0f);
      if (nx < cellLeft) nx = cellLeft;
      float ny = c.ToScreenY(nameY);
      dl->AddText(ImVec2(nx + 1, ny + 1), IM_COL32(0, 0, 0, 180), skill.name);
      dl->AddText(ImVec2(nx, ny), nameCol, skill.name);
    }

    // Level req badge (bottom-right corner of cell, only if > 1)
    if (skill.levelReq > 1) {
      snprintf(buf, sizeof(buf), "Lv%d", skill.levelReq);
      bool meetsReq = *s_ctx->serverLevel >= skill.levelReq;
      ImU32 reqCol = meetsReq ? colGreen : colRed;
      if (!learned)
        reqCol = (reqCol & 0x00FFFFFF) | 0x64000000;
      ImVec2 rsz = ImGui::CalcTextSize(buf);
      float rx = c.ToScreenX(cx + CELL_W - 3.0f) - rsz.x;
      float ry = c.ToScreenY(cy + CELL_H - 16.0f);
      dl->AddText(ImVec2(rx + 1, ry + 1), IM_COL32(0, 0, 0, 180), buf);
      dl->AddText(ImVec2(rx, ry), reqCol, buf);
    }

    // Tooltip on hover
    if (cellHovered) {
      float tw = 200;
      float lineH = 18;
      int numLines = 5;
      float th = lineH * numLines + 10;

      BeginPendingTooltip(tw, th);
      AddPendingTooltipLine(colTitle, skill.name);

      snprintf(buf, sizeof(buf), "AG Cost: %d", skill.agCost);
      AddPendingTooltipLine(IM_COL32(100, 180, 255, 255), buf);

      snprintf(buf, sizeof(buf), "Damage: +%d", skill.damageBonus);
      AddPendingTooltipLine(IM_COL32(255, 200, 100, 255), buf);

      snprintf(buf, sizeof(buf), "Level Req: %d", skill.levelReq);
      bool meetsReq = *s_ctx->serverLevel >= skill.levelReq;
      AddPendingTooltipLine(meetsReq ? colGreen : colRed, buf);

      AddPendingTooltipLine(colLabel, skill.desc);

      if (!learned) {
        AddPendingTooltipLine(IM_COL32(255, 150, 50, 255), "(Not learned)");
      }
    }
  }

  // Footer — centered
  int learnedCount = s_ctx->learnedSkills ? (int)s_ctx->learnedSkills->size() : 0;
  snprintf(buf, sizeof(buf), "Learned: %d / %d", learnedCount, NUM_DK_SKILLS);
  {
    ImVec2 fsz = ImGui::CalcTextSize(buf);
    float fx = c.ToScreenX(px + pw * 0.5f) - fsz.x * 0.5f;
    float fy = c.ToScreenY(py + ph - FOOTER_H);
    dl->AddText(ImVec2(fx + 1, fy + 1), IM_COL32(0, 0, 0, 180), buf);
    dl->AddText(ImVec2(fx, fy), colLabel, buf);
  }
}

} // namespace InventoryUI
