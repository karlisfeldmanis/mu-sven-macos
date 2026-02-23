#ifndef HUD_HPP
#define HUD_HPP

#include "MockData.hpp"
#include "UITexture.hpp"
#include "UIWidget.hpp"
#include <functional>
#include <string>

// Modern bottom HUD bar using decrypted LegendHUD PNG assets.
// 0.97d scope: HP/MP diamond orbs, XP bar, 5 skill slots, 4 menu buttons.
// No AG, no SD.
class HUD : public UIWidget {
public:
  void Init(const std::string &assetPath, GLFWwindow *window);
  void Cleanup();
  void Update(const MockData &data);

  bool HandleMouseMove(float vx, float vy) override;
  bool HandleMouseDown(float vx, float vy) override;
  bool HandleMouseUp(float vx, float vy) override;
  void Render(ImDrawList *dl, const UICoords &coords) override;

  // Font for HUD overlays (set externally, larger than default)
  ImFont *hudFont = nullptr;

  // Menu button callbacks
  std::function<void()> onToggleCharInfo;
  std::function<void()> onToggleInventory;
  std::function<void()> onToggleParty;
  std::function<void()> onToggleOptions;

private:
  void RenderFrame(ImDrawList *dl, const UICoords &coords);
  void RenderGemOrb(ImDrawList *dl, const UICoords &coords,
                    const UITexture &tex, float frac, int animFrame, float orbX,
                    float orbY);
  void RenderXPBar(ImDrawList *dl, const UICoords &coords);
  void RenderMenuIcons(ImDrawList *dl, const UICoords &coords);
  void RenderTextOverlays(ImDrawList *dl, const UICoords &coords);

  // Textures
  UITexture m_texBase;      // UI_HUD_Base.png
  UITexture m_texLife;      // UI_HUD_LIFE.png - 8x8 red gem sprite sheet
  UITexture m_texMana;      // UI_HUD_MANA.png - 8x8 blue gem sprite sheet
  UITexture m_texXPFill;    // ActionBarsView_I2B0.png - teal XP fill
  UITexture m_texXPBg;      // ActionBarsView_I2AC.png - XP background
  UITexture m_texMenuIcons; // UI_MainMenuIcons.png - 13x2 icon sheet

  // Game state
  float m_hpFrac = 1.0f, m_mpFrac = 1.0f, m_xpFrac = 0.0f;
  int m_hp = 0, m_maxHp = 1;
  int m_mp = 0, m_maxMp = 1;
  int m_classId = 0;
  int m_level = 1;
  int m_levelUpPoints = 0;
  int m_gold = 0;

  // Animation: gem shimmer
  int m_gemFrame = 0;
  float m_animTimer = 0.0f;

  // Menu icon interaction
  int m_hoveredIcon = -1;
  int m_pressedIcon = -1;

  GLFWwindow *m_window = nullptr;

  // --- Layout (1280x720 virtual coords) ---
  static constexpr float BASE_W = 1224.0f;
  static constexpr float BASE_H = 180.0f;
  static constexpr float VSCALE = 1280.0f / BASE_W; // ~1.046
  static constexpr float HUD_HEIGHT = BASE_H * VSCALE;
  static constexpr float HUD_TOP = 720.0f - HUD_HEIGHT;

  // HP orb - centered in left diamond cutout (PNG center ~x=202, y=85)
  static constexpr float ORB_W = 134.0f;
  static constexpr float ORB_H = 138.0f;
  static constexpr float HP_ORB_X = 202.0f * VSCALE - ORB_W * 0.5f; // ~144
  static constexpr float HP_ORB_Y = HUD_TOP + 18.0f * VSCALE;

  // MP orb - centered in right diamond cutout (symmetric)
  static constexpr float MP_ORB_X = 1280.0f - HP_ORB_X - ORB_W; // ~999
  static constexpr float MP_ORB_Y = HP_ORB_Y;

  // XP bar (within main bar body, PNG x ~285-945)
  static constexpr float XP_X = 290.0f;
  static constexpr float XP_Y = HUD_TOP + 155.0f * VSCALE;
  static constexpr float XP_W = 700.0f;
  static constexpr float XP_H = 8.0f;

  // Menu icons (right side of main bar, above XP)
  static constexpr float ICON_SIZE = 44.0f;
  static constexpr float ICON_GAP = 4.0f;
  static constexpr float ICON_X_START = 780.0f;
  static constexpr float ICON_Y_POS = HUD_TOP + 90.0f * VSCALE;
  static constexpr int MENU_ICON_COUNT = 4;
  static constexpr int ICON_IDX_CHAR = 0;
  static constexpr int ICON_IDX_INV = 1;
  static constexpr int ICON_IDX_PARTY = 3;
  static constexpr int ICON_IDX_OPT = 4;
  static constexpr float ICONS_PER_ROW = 13.0f;

  // Gem sprite sheet: 8x8 grid = 64 shimmer frames
  static constexpr int GEM_COLS = 8;
  static constexpr int GEM_ROWS = 8;
  static constexpr int GEM_FRAMES = 64;
};

#endif // HUD_HPP
