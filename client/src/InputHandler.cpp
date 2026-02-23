#include "InputHandler.hpp"
#include "Camera.hpp"
#include "ClickEffect.hpp"
#include "ClientTypes.hpp"
#include "HeroCharacter.hpp"
#include "InventoryUI.hpp"
#include "MonsterManager.hpp"
#include "NpcManager.hpp"
#include "RayPicker.hpp"
#include "ServerConnection.hpp"
#include "UICoords.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>

// ── Stored context (set once via Init) ──
static InputContext s_ctxStore;
static const InputContext *s_ctx = &s_ctxStore;
static bool s_ctxInitialized = false;

// Pending NPC interaction (walk to NPC then open shop)
static int s_pendingNpcIdx = -1;

// Distance thresholds for NPC interaction
static constexpr float NPC_INTERACT_RANGE =
    200.0f; // Open shop when within this range
static constexpr float NPC_STOP_OFFSET =
    150.0f; // Stop this far from NPC center
static constexpr float NPC_CLOSE_RANGE = 500.0f; // Auto-close shop beyond this

// Set true after first ProcessInput — prevents callbacks during early init
static bool s_gameReady = false;

// GLFW cursors for hover feedback (Main 5.2: CursorId changes on hover)
static GLFWcursor *s_cursorArrow = nullptr;    // Default
static GLFWcursor *s_cursorAttack = nullptr;   // Monster hover (crosshair)
static GLFWcursor *s_cursorInteract = nullptr; // NPC/item hover (hand)
static GLFWwindow *s_window = nullptr;

// ── GLFW callbacks ──

static void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
  ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
  if (!s_ctxInitialized)
    return;

  // Update NPC, Monster, and Ground Item hover state on cursor move
  if (!ImGui::GetIO().WantCaptureMouse) {
    *s_ctx->hoveredNpc = RayPicker::PickNpc(window, xpos, ypos);
    // Also check NPC label hover (only after game is fully initialized)
    if (*s_ctx->hoveredNpc < 0 && s_gameReady) {
      int winW, winH;
      glfwGetWindowSize(window, &winW, &winH);
      glm::mat4 view = s_ctx->camera->GetViewMatrix();
      glm::mat4 proj =
          s_ctx->camera->GetProjectionMatrix((float)winW, (float)winH);
      glm::vec3 camPos = s_ctx->camera->GetPosition();
      *s_ctx->hoveredNpc = s_ctx->npcMgr->PickLabel(
          (float)xpos, (float)ypos, view, proj, winW, winH, camPos);
    }
    if (*s_ctx->hoveredNpc < 0) {
      // Ground items have higher pick priority than monsters so loot is easy
      *s_ctx->hoveredGroundItem = RayPicker::PickGroundItem(window, xpos, ypos);
      if (*s_ctx->hoveredGroundItem < 0) {
        *s_ctx->hoveredMonster = RayPicker::PickMonster(window, xpos, ypos);
      } else {
        *s_ctx->hoveredMonster = -1;
      }
    } else {
      *s_ctx->hoveredMonster = -1;
      *s_ctx->hoveredGroundItem = -1;
    }
  } else {
    *s_ctx->hoveredNpc = -1;
    *s_ctx->hoveredMonster = -1;
    *s_ctx->hoveredGroundItem = -1;
  }

  // Update cursor based on hover state (Main 5.2: CursorId switching)
  if (s_window) {
    if (*s_ctx->hoveredMonster >= 0)
      glfwSetCursor(s_window, s_cursorAttack);
    else if (*s_ctx->hoveredNpc >= 0 || *s_ctx->hoveredGroundItem >= 0)
      glfwSetCursor(s_window, s_cursorInteract);
    else
      glfwSetCursor(s_window, s_cursorArrow);
  }
}

static void scroll_callback(GLFWwindow *window, double xoffset,
                            double yoffset) {
  ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
  s_ctx->camera->ProcessMouseScroll(yoffset);
}

static void HandlePickupClick(GLFWwindow *window, double mx, double my) {
  if (*s_ctx->showInventory || *s_ctx->showCharInfo)
    return; // UI blocks pickup

  if (*s_ctx->hoveredGroundItem != -1) {
    int bestIdx = *s_ctx->hoveredGroundItem;
    float distToHero = glm::distance(s_ctx->hero->GetPosition(),
                                     s_ctx->groundItems[bestIdx].position);

    if (distToHero < 150.0f) {
      // Close enough, pick up immediately
      s_ctx->hero->CancelAttack();
      s_ctx->hero->StopMoving();
      s_ctx->server->SendPickup(s_ctx->groundItems[bestIdx].dropIndex);
      std::cout << "[Pickup] Sent direct pickup for index "
                << s_ctx->groundItems[bestIdx].dropIndex << " (Close range)"
                << std::endl;
      s_ctx->hero->ClearPendingPickup();
    } else {
      // Too far, move to it and set pending pickup
      s_ctx->hero->MoveTo(s_ctx->groundItems[bestIdx].position);
      s_ctx->hero->SetPendingPickup(bestIdx);
      std::cout << "[Pickup] Moving to item index "
                << s_ctx->groundItems[bestIdx].dropIndex << std::endl;
    }
  }
}

static void HandleNpcInteraction(int npcIdx) {
  *s_ctx->selectedNpc = npcIdx;
  s_ctx->hero->CancelAttack();
  s_ctx->hero->ClearPendingPickup();

  NpcInfo info = s_ctx->npcMgr->GetNpcInfo(npcIdx);
  float dist = glm::distance(s_ctx->hero->GetPosition(), info.position);

  if (dist < NPC_INTERACT_RANGE) {
    s_ctx->server->SendShopOpen(info.type);
    s_pendingNpcIdx = -1;
  } else {
    // Walk to a point NPC_STOP_OFFSET away from the NPC, toward the hero
    glm::vec3 heroPos = s_ctx->hero->GetPosition();
    glm::vec3 dir = heroPos - info.position;
    dir.y = 0;
    float len = glm::length(dir);
    if (len > 0.01f) {
      glm::vec3 stopPos = info.position + (dir / len) * NPC_STOP_OFFSET;
      s_ctx->hero->MoveTo(stopPos);
    } else {
      s_ctx->hero->MoveTo(info.position);
    }
    s_pendingNpcIdx = npcIdx;
  }
}

// --- Click-to-move mouse handler ---

static void mouse_button_callback(GLFWwindow *window, int button, int action,
                                  int mods) {
  ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
  if (!s_ctxInitialized)
    return;

  // Block world interactions while learning a skill
  if (s_ctx->isLearningSkill && *s_ctx->isLearningSkill)
    return;

  // Click-to-move on left click (NPC click takes priority)
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    if (!ImGui::GetIO().WantCaptureMouse) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);

      // Check if click is on a UI panel or HUD first
      float vx = s_ctx->hudCoords->ToVirtualX((float)mx);
      float vy = s_ctx->hudCoords->ToVirtualY((float)my);
      if (InventoryUI::HandlePanelClick(vx, vy))
        return;

      // Highest priority interactions: NPC > Monster > Ground Item > Movement
      int npcHit = RayPicker::PickNpc(window, mx, my);

      // Also check NPC label click (2D screen-space hit test)
      if (npcHit < 0 && s_gameReady) {
        int winW, winH;
        glfwGetWindowSize(window, &winW, &winH);
        glm::mat4 view = s_ctx->camera->GetViewMatrix();
        glm::mat4 proj =
            s_ctx->camera->GetProjectionMatrix((float)winW, (float)winH);
        glm::vec3 camPos = s_ctx->camera->GetPosition();
        npcHit = s_ctx->npcMgr->PickLabel((float)mx, (float)my, view, proj,
                                          winW, winH, camPos);
      }

      if (npcHit >= 0) {
        HandleNpcInteraction(npcHit);
      } else {
        // Preserve selectedNpc while shop is open (needed for auto-close
        // distance check)
        if (!*s_ctx->shopOpen) {
          *s_ctx->selectedNpc = -1;
        }
        s_pendingNpcIdx = -1;
        // Re-pick at click time for reliable priority (NPC > Item > Monster)
        int itemHit = RayPicker::PickGroundItem(window, mx, my);
        int monHit =
            (itemHit < 0) ? RayPicker::PickMonster(window, mx, my) : -1;
        // Update hover state to match click picks
        *s_ctx->hoveredGroundItem = itemHit;
        if (itemHit >= 0)
          *s_ctx->hoveredMonster = -1;
        else
          *s_ctx->hoveredMonster = monHit;

        if (itemHit >= 0) {
          HandlePickupClick(window, mx, my);
        } else if (monHit >= 0) {
          MonsterInfo info = s_ctx->monsterMgr->GetMonsterInfo(monHit);
          s_ctx->hero->AttackMonster(monHit, info.position);
          s_ctx->hero
              ->ClearPendingPickup(); // Cancel pickup if attacking monster
        } else {
          // Ground click — move to terrain
          if (s_ctx->hero->IsAttacking())
            s_ctx->hero->CancelAttack();
          s_ctx->hero->ClearPendingPickup(); // Cancel pickup if manually moving
          glm::vec3 target;
          if (RayPicker::ScreenToTerrain(window, mx, my, target)) {
            if (RayPicker::IsWalkable(target.x, target.z)) {
              s_ctx->hero->MoveTo(target);
              s_ctx->clickEffect->Show(target);
            }
          }
        }
      }
    }
  } else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
    if (!ImGui::GetIO().WantCaptureMouse) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);
      float vx = s_ctx->hudCoords->ToVirtualX((float)mx);
      float vy = s_ctx->hudCoords->ToVirtualY((float)my);

      // Try UI panel right-click first (sell, orb use)
      if (!InventoryUI::HandlePanelRightClick(vx, vy)) {
        // Not on UI panel — try skill attack on monster
        if (s_ctx->rmcSkillId && *s_ctx->rmcSkillId >= 0) {
          uint8_t skillId = (uint8_t)*s_ctx->rmcSkillId;
          int agCost = InventoryUI::GetSkillAGCost(skillId);
          int currentAG = s_ctx->serverMP ? *s_ctx->serverMP : 0;
          if (currentAG < agCost) {
            InventoryUI::ShowNotification("Not enough AG!");
          } else {
            int monHit = RayPicker::PickMonster(window, mx, my);
            if (monHit >= 0) {
              MonsterInfo info = s_ctx->monsterMgr->GetMonsterInfo(monHit);
              s_ctx->hero->SkillAttackMonster(monHit, info.position, skillId);
              s_ctx->hero->ClearPendingPickup();
            }
          }
        }
      }
    }
  }

  // Mouse up: handle drag release
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
    if (InventoryUI::IsDragging()) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);
      float vx = s_ctx->hudCoords->ToVirtualX((float)mx);
      float vy = s_ctx->hudCoords->ToVirtualY((float)my);
      InventoryUI::HandlePanelMouseUp(window, vx, vy);
    }
  }
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action,
                         int mods) {
  ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
  // Note: do NOT check WantCaptureKeyboard here — it blocks game hotkeys
  // when ImGui panels have focus. Only text-input widgets need that guard.

  if (action == GLFW_PRESS) {
    if (key == GLFW_KEY_C)
      *s_ctx->showCharInfo = !*s_ctx->showCharInfo;
    if (key == GLFW_KEY_I)
      *s_ctx->showInventory = !*s_ctx->showInventory;
    if (key == GLFW_KEY_S)
      *s_ctx->showSkillWindow = !*s_ctx->showSkillWindow;
    if (key == GLFW_KEY_Q)
      InventoryUI::ConsumeQuickSlotItem(0);
    if (key == GLFW_KEY_W)
      InventoryUI::ConsumeQuickSlotItem(1);
    if (key == GLFW_KEY_E)
      InventoryUI::ConsumeQuickSlotItem(2);
    if (key == GLFW_KEY_R)
      InventoryUI::ConsumeQuickSlotItem(3);
    // Skill hotkeys 1-9, 0
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
      int idx = key - GLFW_KEY_1;
      if (s_ctx->skillBar[idx] != -1)
        *s_ctx->rmcSkillId = s_ctx->skillBar[idx];
    }
    if (key == GLFW_KEY_0) {
      if (s_ctx->skillBar[9] != -1)
        *s_ctx->rmcSkillId = s_ctx->skillBar[9];
    }
    if (key == GLFW_KEY_ESCAPE) {
      if (*s_ctx->shopOpen) {
        *s_ctx->shopOpen = false;
        *s_ctx->selectedNpc = -1;
      } else if (*s_ctx->showCharInfo)
        *s_ctx->showCharInfo = false;
      else if (*s_ctx->showInventory)
        *s_ctx->showInventory = false;
      else if (*s_ctx->showSkillWindow)
        *s_ctx->showSkillWindow = false;
    }
  }
}

static void char_callback(GLFWwindow *window, unsigned int c) {
  ImGui_ImplGlfw_CharCallback(window, c);
}

// ── Public API ──

void InputHandler::Init(const InputContext &ctx) {
  s_ctxStore = ctx;
  s_ctx = &s_ctxStore;
  s_ctxInitialized = true;
}

void InputHandler::RegisterCallbacks(GLFWwindow *window) {
  s_window = window;

  // Create hover cursors (Main 5.2: sword cursor for attack, hand for interact)
  s_cursorArrow = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
  s_cursorAttack = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
  s_cursorInteract = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

  glfwSetCursorPosCallback(window, mouse_callback);
  glfwSetScrollCallback(window, scroll_callback);
  glfwSetMouseButtonCallback(window, mouse_button_callback);
  glfwSetKeyCallback(window, key_callback);
  glfwSetCharCallback(window, char_callback);
}

// --- Process input: hero movement + auto-pickup ---

void InputHandler::ProcessInput(GLFWwindow *window, float deltaTime) {
  s_gameReady = true;
  bool wasMoving = s_ctx->hero->IsMoving();
  s_ctx->hero->ProcessMovement(deltaTime);

  // Auto-pickup logic: check if we reached a pending item
  int pendingIdx = s_ctx->hero->GetPendingPickup();
  if (pendingIdx != -1) {
    if (pendingIdx >= 0 && pendingIdx < s_ctx->maxGroundItems &&
        s_ctx->groundItems[pendingIdx].active) {
      float dist = glm::distance(s_ctx->hero->GetPosition(),
                                 s_ctx->groundItems[pendingIdx].position);
      if (dist < 150.0f) {
        s_ctx->server->SendPickup(s_ctx->groundItems[pendingIdx].dropIndex);
        std::cout << "[Pickup] REACHED: Auto-picking item index "
                  << s_ctx->groundItems[pendingIdx].dropIndex << std::endl;
        s_ctx->hero->ClearPendingPickup();
      }
    } else {
      s_ctx->hero->ClearPendingPickup(); // Item no longer active
    }
  }

  // Pending NPC interaction: open shop when hero reaches the NPC
  if (s_pendingNpcIdx >= 0 && s_pendingNpcIdx < s_ctx->npcMgr->GetNpcCount()) {
    NpcInfo info = s_ctx->npcMgr->GetNpcInfo(s_pendingNpcIdx);
    float dist = glm::distance(s_ctx->hero->GetPosition(), info.position);
    if (dist < NPC_INTERACT_RANGE) {
      s_ctx->server->SendShopOpen(info.type);
      s_pendingNpcIdx = -1;
    }
  }

  // Auto-close shop when hero walks too far from the NPC
  if (*s_ctx->shopOpen && *s_ctx->selectedNpc >= 0 &&
      *s_ctx->selectedNpc < s_ctx->npcMgr->GetNpcCount()) {
    NpcInfo info = s_ctx->npcMgr->GetNpcInfo(*s_ctx->selectedNpc);
    float dist = glm::distance(s_ctx->hero->GetPosition(), info.position);
    if (dist > NPC_CLOSE_RANGE) {
      *s_ctx->shopOpen = false;
      *s_ctx->selectedNpc = -1;
    }
  }

  // Hide click effect when hero stops moving
  if (wasMoving && !s_ctx->hero->IsMoving())
    s_ctx->clickEffect->Hide();

  // Camera follows hero (Continuous)
  s_ctx->camera->SetPosition(s_ctx->hero->GetPosition());
}
