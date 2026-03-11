# UI System

The UI system is a hybrid of custom OpenGL textured quad rendering and ImGui for complex interaction panels.

## Diablo HUD (Resource Orbs)

The main HUD is inspired by Diablo, featuring large circular orbs for primary resources.

- **HP Orb**: Red liquid fill reflecting player Health.
- **Mana/AG Orb**: Blue liquid for DW (Mana) or Purple for DK (Ability Gauge).
- **Orb Rendering**: 
    - Gradient-filled circles with specular highlights.
    - Floating text overlays for current/max values.
    - Scaling based on `g_uiPanelScale` (derived from window height).

## Quickbar Expansion

The quickbar supports expanded slots for both skills and consumables.

- **Skill Slots (1-10)**: Support for up to 10 assignable skills.
- **Potion Slots (Q, W, E)**: 3 dedicated slots for quick-access consumables.
- **Cooldown Display**: Semi-transparent radial overlay on icons during cooldown.
- **Hotkeys**: Standard 0.97d keys (1-9 for skills, Q/W/E/R for potions).

## Region Name Display

When entering a new map or region, a stylized nameplate pops up.

- **Texture-based**: Uses OZT images from `Data/Local/[Lang]/ImgsMapName/`.
- **4-State Animation**: `HIDE` → `FADEIN` → `SHOW` (2s hold) → `FADEOUT`.
- **Main 5.2 Implementation**: Matches original `CUIMapName` behavior.

## Notification System

A central screen notification area for non-blocking feedback.

- **Feedback**: Used for "HP is full", "Level Up", and inventory messages.
- **Animation**: Fades out after 1 second.
- **Style**: Dark translucent background with colored text.

## Modules

| Module | Purpose |
|--------|---------|
| `InventoryUI.cpp` | Main UI orchestrator (Panels, HUD, Quickbar) |
| `HUD.cpp` | Layout and rendering of the Diablo HUD |
| `GameUI.cpp` | General UI texture and widget helpers |
| `UITexture.cpp` | Low-level UI texture loading and management |
| `UIWidget.cpp` | UI primitives (buttons, slots, bars) |
| `UICoords.hpp` | Coordinate system mapping for responsive UI |
