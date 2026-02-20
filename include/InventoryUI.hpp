#ifndef MU_INVENTORY_UI_HPP
#define MU_INVENTORY_UI_HPP

#include "ClientTypes.hpp"
#include "UICoords.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct ImDrawList;
struct ImFont;
struct GLFWwindow;
class HeroCharacter;
class ServerConnection;

// Render job for deferred 3D item rendering on top of UI
struct ItemRenderJob {
  std::string modelFile;
  int16_t defIndex;
  int x, y, w, h;
  bool hovered;
};

// Equipment slot layout rect (virtual coords)
struct EquipSlotRect {
  int slot;
  float x, y, w, h;
};

// Context struct: pointers to shared state owned by main.cpp
struct InventoryUIContext {
  ClientInventoryItem *inventory; // [INVENTORY_SLOTS]
  ClientEquipSlot *equipSlots;    // [12]
  uint32_t *zen;
  bool *syncDone;
  bool *showCharInfo;
  bool *showInventory;
  int16_t *quickSlotDefIndex;
  float *potionCooldown;
  bool *shopOpen;
  std::vector<ShopItem> *shopItems;

  // Server stats (written by ClientPacketHandler)
  int *serverLevel, *serverStr, *serverDex, *serverVit, *serverEne;
  int *serverLevelUpPoints, *serverDefense, *serverAttackSpeed,
      *serverMagicSpeed;
  int *serverHP, *serverMaxHP, *serverMP, *serverMaxMP;
  int64_t *serverXP;

  HeroCharacter *hero;
  ServerConnection *server;
  UICoords *hudCoords;
  ImFont *fontDefault;
};

namespace InventoryUI {

void Init(const InventoryUIContext &ctx);

// Inventory helpers (called from ClientPacketHandler callbacks too)
void SetBagItem(int slot, int16_t defIdx, uint8_t qty, uint8_t lvl);
void ClearBagItem(int slot);
void ConsumeQuickSlotItem();
void RecalcEquipmentStats();

// Panel rendering
void RenderCharInfoPanel(ImDrawList *dl, const UICoords &c);
void RenderInventoryPanel(ImDrawList *dl, const UICoords &c);
void RenderShopPanel(ImDrawList *dl, const UICoords &c);

// Tooltip
void AddPendingItemTooltip(int16_t defIndex, int itemLevel);
void FlushPendingTooltip();
bool HasPendingTooltip();
void ResetPendingTooltip();

// Click handling (returns true if click was consumed by a panel)
bool HandlePanelClick(float vx, float vy);
bool HandlePanelRightClick(float vx, float vy);
void HandlePanelMouseUp(GLFWwindow *window, float vx, float vy);

// Drag state queries
bool IsDragging();
int16_t GetDragDefIndex();

// Render queue (for deferred 3D item rendering)
const std::vector<ItemRenderJob> &GetRenderQueue();
void ClearRenderQueue();
void AddRenderJob(const ItemRenderJob &job);

// Deferred overlays (quantity labels rendered after 3D items)
bool HasDeferredOverlays();
void FlushDeferredOverlays();

// Load slot background textures
void LoadSlotBackgrounds(const std::string &dataPath);

// Panel geometry queries
float GetCharInfoPanelX();
float GetInventoryPanelX();
float GetShopPanelX();
bool IsPointInPanel(float vx, float vy, float panelX);

// Notifications
void UpdateAndRenderNotification(float deltaTime);
void ShowNotification(const char *msg);

} // namespace InventoryUI

#endif // MU_INVENTORY_UI_HPP
