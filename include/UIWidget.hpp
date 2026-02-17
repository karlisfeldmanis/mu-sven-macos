#ifndef UI_WIDGET_HPP
#define UI_WIDGET_HPP

#include "UICoords.hpp"
#include "UITexture.hpp"
#include "imgui.h"

// Base class for all UI elements.
// Positions and sizes are in virtual coordinates (see UICoords).
class UIWidget {
public:
    virtual ~UIWidget() = default;

    float x = 0, y = 0, w = 0, h = 0;
    bool visible = true;

    // Input processing -- returns true if widget consumed the event
    virtual bool HandleMouseMove(float vx, float vy) { return false; }
    virtual bool HandleMouseDown(float vx, float vy) { return false; }
    virtual bool HandleMouseUp(float vx, float vy) { return false; }

    // Rendering via ImGui foreground draw list
    virtual void Render(ImDrawList* dl, const UICoords& coords) = 0;

    // AABB hit test in virtual coords
    bool HitTest(float vx, float vy) const {
        return visible && vx >= x && vx < x + w && vy >= y && vy < y + h;
    }

protected:
    // Draw a textured quad in virtual coords.
    // Automatically handles OZT V-flip for correct ImGui display.
    void DrawImage(ImDrawList* dl, const UICoords& coords,
                   const UITexture& tex, float vx, float vy, float vw, float vh,
                   ImVec2 uvMin = {0, 0}, ImVec2 uvMax = {1, 1},
                   ImU32 tint = IM_COL32_WHITE);

    // Draw text with a 1px drop shadow for readability.
    void DrawText(ImDrawList* dl, const UICoords& coords,
                  float vx, float vy, const char* text,
                  ImU32 color = IM_COL32(255, 255, 255, 230),
                  ImU32 shadowColor = IM_COL32(0, 0, 0, 200));

    // Draw text centered horizontally within a given virtual width.
    void DrawTextCentered(ImDrawList* dl, const UICoords& coords,
                          float vx, float vy, float vw, const char* text,
                          ImU32 color = IM_COL32(255, 255, 255, 230),
                          ImU32 shadowColor = IM_COL32(0, 0, 0, 200));

    // Draw a filled rect in virtual coords.
    void DrawRect(ImDrawList* dl, const UICoords& coords,
                  float vx, float vy, float vw, float vh, ImU32 color);
};

#endif // UI_WIDGET_HPP
