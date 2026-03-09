#ifndef MU_SYSTEM_MESSAGE_LOG_HPP
#define MU_SYSTEM_MESSAGE_LOG_HPP

#include "imgui.h"
#include <string>

enum MessageCategory { MSG_GENERAL = 0, MSG_COMBAT = 1, MSG_SYSTEM = 2 };

namespace SystemMessageLog {
void Init();
void Log(MessageCategory cat, ImU32 color, const char *fmt, ...);
void LogSilent(MessageCategory cat, ImU32 color, const char *fmt, ...);
void Update(float deltaTime);
void Render(ImDrawList *dl, ImFont *font, float screenW, float screenH,
            float hudBarHeight, float mouseX, float mouseY);
// Returns true if scroll event was consumed by the log area
bool HandleScroll(float mx, float my, float scrollDelta);
// Returns true if click was consumed by a tab
bool HandleClick(float mx, float my);
} // namespace SystemMessageLog

#endif // MU_SYSTEM_MESSAGE_LOG_HPP
