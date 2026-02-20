#include "ClientPacketHandler.hpp"
#include "../server/include/PacketDefs.hpp"
#include <cstring>
#include <iostream>

namespace ClientPacketHandler {

static ClientGameState *g_state = nullptr;

void Init(ClientGameState *state) { g_state = state; }

// ── Inventory helpers ──

static int16_t GetDefIndexFromCategory(uint8_t category, uint8_t index) {
  for (auto const &[id, def] : *g_state->itemDefs) {
    if (def.category == category && def.itemIndex == index)
      return id;
  }
  return -1;
}

static void SetBagItem(int slot, int16_t defIdx, uint8_t qty, uint8_t lvl) {
  auto it = g_state->itemDefs->find(defIdx);
  if (it == g_state->itemDefs->end())
    return;
  int w = it->second.width;
  int h = it->second.height;
  int r = slot / 8;
  int c = slot % 8;

  if (c + w > 8 || r + h > 8)
    return;

  for (int hh = 0; hh < h; hh++) {
    for (int ww = 0; ww < w; ww++) {
      int s = (r + hh) * 8 + (c + ww);
      if (s >= INVENTORY_SLOTS || g_state->inventory[s].occupied)
        return;
    }
  }

  for (int hh = 0; hh < h; hh++) {
    for (int ww = 0; ww < w; ww++) {
      int s = (r + hh) * 8 + (c + ww);
      g_state->inventory[s].occupied = true;
      g_state->inventory[s].primary = (hh == 0 && ww == 0);
      g_state->inventory[s].defIndex = defIdx;
      if (g_state->inventory[s].primary) {
        g_state->inventory[s].quantity = qty;
        g_state->inventory[s].itemLevel = lvl;
      }
    }
  }
}

// ── Equipment helpers ──

static void ApplyEquipToHero(uint8_t slot, const WeaponEquipInfo &weapon) {
  if (slot == 0) {
    g_state->hero->EquipWeapon(weapon);
  } else if (slot == 1) {
    g_state->hero->EquipShield(weapon);
  } else if (g_state->getBodyPartIndex) {
    int bodyPart = g_state->getBodyPartIndex(weapon.category);
    if (bodyPart >= 0 && g_state->getBodyPartModelFile) {
      std::string partModel =
          g_state->getBodyPartModelFile(weapon.category, weapon.itemIndex);
      if (!partModel.empty())
        g_state->hero->EquipBodyPart(bodyPart, partModel);
    }
  }
}

static void ApplyEquipToUI(uint8_t slot, const WeaponEquipInfo &weapon) {
  if (slot < 12) {
    g_state->equipSlots[slot].category = weapon.category;
    g_state->equipSlots[slot].itemIndex = weapon.itemIndex;
    g_state->equipSlots[slot].itemLevel = weapon.itemLevel;
    g_state->equipSlots[slot].modelFile = weapon.modelFile;
    g_state->equipSlots[slot].equipped = (weapon.category != 0xFF);
  }
}

// ── Stats sync helper ──

static void SyncCharStats(const PMSG_CHARSTATS_SEND *stats) {
  *g_state->serverLevel = stats->level;
  *g_state->serverStr = stats->strength;
  *g_state->serverDex = stats->dexterity;
  *g_state->serverVit = stats->vitality;
  *g_state->serverEne = stats->energy;
  *g_state->serverHP = stats->life;
  *g_state->serverMaxHP = stats->maxLife;
  *g_state->serverMP = stats->mana;
  *g_state->serverMaxMP = stats->maxMana;
  *g_state->serverLevelUpPoints = stats->levelUpPoints;
  *g_state->quickSlotDefIndex = stats->quickSlotDefIndex;
  *g_state->serverXP =
      ((int64_t)stats->experienceHi << 32) | stats->experienceLo;
  *g_state->serverDefense = stats->defense;
  *g_state->serverAttackSpeed = stats->attackSpeed;
  *g_state->serverMagicSpeed = stats->magicSpeed;
}

// ── Equipment packet parsing (shared between initial and ongoing) ──

static void ParseEquipmentPacket(const uint8_t *pkt, int pktSize,
                                 int countOffset, int dataOffset,
                                 ServerData *serverData) {
  uint8_t count = pkt[countOffset];
  int entrySize = 4 + 32; // slot(1)+cat(1)+idx(1)+lvl(1)+model(32)
  for (int i = 0; i < count; i++) {
    int off = dataOffset + i * entrySize;
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

    if (serverData)
      serverData->equipment.push_back({slot, weapon});

    ApplyEquipToHero(slot, weapon);
    ApplyEquipToUI(slot, weapon);
  }
}

// ── Inventory sync parsing (shared between initial and ongoing) ──

static void ParseInventorySync(const uint8_t *pkt, int pktSize) {
  if (pktSize < 9)
    return;

  uint32_t zen;
  std::memcpy(&zen, pkt + 4, 4);
  *g_state->zen = zen;
  uint8_t count = pkt[8];

  // Clear client grid
  for (int i = 0; i < INVENTORY_SLOTS; i++)
    g_state->inventory[i] = {};

  const int itemSize = 5; // slot(1)+cat(1)+idx(1)+qty(1)+lvl(1)
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
        g_state->inventory[slot].occupied = true;
        g_state->inventory[slot].primary = true;
        g_state->inventory[slot].quantity = qty;
        g_state->inventory[slot].itemLevel = lvl;
      }
    }
  }
  *g_state->syncDone = true;
}

// ═══════════════════════════════════════════════════════════════════
// Initial packet handler (connection burst: NPCs, monsters, equipment, stats)
// ═══════════════════════════════════════════════════════════════════

void HandleInitialPacket(const uint8_t *pkt, int pktSize, ServerData &result) {
  if (pktSize < 3)
    return;
  uint8_t type = pkt[0];

  // C2 packets (4-byte header)
  if (type == 0xC2 && pktSize >= 5) {
    uint8_t headcode = pkt[3];

    // NPC viewport
    if (headcode == Opcode::NPC_VIEWPORT) {
      uint8_t count = pkt[4];
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
      }
      std::cout << "[Net] NPC viewport: " << (int)count << " NPCs" << std::endl;
    }

    // Monster viewport V2
    if (headcode == Opcode::MON_VIEWPORT_V2) {
      uint8_t count = pkt[4];
      int entrySize = 12;
      for (int i = 0; i < count; i++) {
        int off = 5 + i * entrySize;
        if (off + entrySize > pktSize)
          break;
        ServerMonsterSpawn mon;
        mon.serverIndex = (uint16_t)((pkt[off + 0] << 8) | pkt[off + 1]);
        mon.monsterType = (uint16_t)((pkt[off + 2] << 8) | pkt[off + 3]);
        mon.gridX = pkt[off + 4];
        mon.gridY = pkt[off + 5];
        mon.dir = pkt[off + 6];
        result.monsters.push_back(mon);
      }
      std::cout << "[Net] Monster viewport V2: " << (int)count << " monsters"
                << std::endl;
    }

    // Inventory sync
    if (headcode == Opcode::INV_SYNC && pktSize >= 9) {
      // Initial sync uses slightly different format (category*32+index encoding)
      uint32_t zen;
      std::memcpy(&zen, pkt + 4, 4);
      *g_state->zen = zen;
      uint8_t count = pkt[8];
      for (int i = 0; i < INVENTORY_SLOTS; i++)
        g_state->inventory[i] = {};
      for (int i = 0; i < count; i++) {
        int off = 9 + i * sizeof(PMSG_INVENTORY_ITEM);
        if (off + (int)sizeof(PMSG_INVENTORY_ITEM) > pktSize)
          break;
        const auto *item =
            reinterpret_cast<const PMSG_INVENTORY_ITEM *>(pkt + off);
        uint8_t slot = item->slot;
        if (slot < INVENTORY_SLOTS) {
          int16_t defIdx =
              (int16_t)((int)item->category * 32 + (int)item->itemIndex);
          g_state->inventory[slot].defIndex = defIdx;
          g_state->inventory[slot].quantity = item->quantity;
          g_state->inventory[slot].itemLevel = item->itemLevel;
          g_state->inventory[slot].occupied = true;
          g_state->inventory[slot].primary = true;

          auto it = g_state->itemDefs->find(defIdx);
          if (it != g_state->itemDefs->end()) {
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
                  g_state->inventory[s].occupied = true;
                  g_state->inventory[s].primary = false;
                  g_state->inventory[s].defIndex = defIdx;
                }
              }
            }
          }
        }
      }
      std::cout << "[Net] Inventory sync: " << (int)count
                << " items, zen=" << *g_state->zen << std::endl;
    }

    // Equipment (C2)
    if (headcode == Opcode::EQUIPMENT && pktSize >= 5) {
      ParseEquipmentPacket(pkt, pktSize, 4, 5, &result);
      std::cout << "[Net] Equipment (C2): " << (int)pkt[4] << " slots"
                << std::endl;
    }
  }

  // C1 packets (2-byte header)
  if (type == 0xC1 && pktSize >= 3) {
    uint8_t headcode = pkt[2];

    // Monster viewport V1
    if (headcode == 0x1F && pktSize >= 4) {
      uint8_t count = pkt[3];
      int entrySize = 5;
      for (int i = 0; i < count; i++) {
        int off = 4 + i * entrySize;
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

    // Character stats
    if (headcode == Opcode::CHARSTATS &&
        pktSize >= (int)sizeof(PMSG_CHARSTATS_SEND)) {
      auto *stats = reinterpret_cast<const PMSG_CHARSTATS_SEND *>(pkt);
      SyncCharStats(stats);

      g_state->hero->LoadStats(
          *g_state->serverLevel, *g_state->serverStr, *g_state->serverDex,
          *g_state->serverVit, *g_state->serverEne,
          (uint64_t)*g_state->serverXP, *g_state->serverLevelUpPoints,
          *g_state->serverHP, *g_state->serverMaxHP, *g_state->serverMP,
          *g_state->serverMaxMP, stats->charClass);

      std::cout << "[Net] Character stats: Lv." << *g_state->serverLevel
                << " HP=" << *g_state->serverHP << "/"
                << *g_state->serverMaxHP << " STR=" << *g_state->serverStr
                << " XP=" << *g_state->serverXP
                << " Pts=" << *g_state->serverLevelUpPoints << std::endl;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// Ongoing game packet handler (monster AI, combat, drops, stats sync)
// ═══════════════════════════════════════════════════════════════════

void HandleGamePacket(const uint8_t *pkt, int pktSize) {
  if (pktSize < 3)
    return;
  uint8_t type = pkt[0];

  if (type == 0xC1) {
    uint8_t headcode = pkt[2];

    // Monster move/chase
    if (headcode == Opcode::MON_MOVE &&
        pktSize >= (int)sizeof(PMSG_MONSTER_MOVE_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_MOVE_SEND *>(pkt);
      int idx = g_state->monsterManager->FindByServerIndex(p->monsterIndex);
      if (idx >= 0) {
        float worldX = (float)p->targetY * 100.0f;
        float worldZ = (float)p->targetX * 100.0f;
        g_state->monsterManager->SetMonsterServerPosition(idx, worldX, worldZ,
                                                          p->chasing != 0);
      }
    }

    // Damage result (player hits monster)
    if (headcode == Opcode::DAMAGE &&
        pktSize >= (int)sizeof(PMSG_DAMAGE_SEND)) {
      auto *p = reinterpret_cast<const PMSG_DAMAGE_SEND *>(pkt);
      int idx = g_state->monsterManager->FindByServerIndex(p->monsterIndex);
      if (idx >= 0) {
        MonsterInfo mi = g_state->monsterManager->GetMonsterInfo(idx);
        g_state->monsterManager->SetMonsterHP(idx, p->remainingHp, mi.maxHp);
        g_state->monsterManager->TriggerHitAnimation(idx);

        glm::vec3 monPos = g_state->monsterManager->GetMonsterInfo(idx).position;
        g_state->vfxManager->SpawnBurst(ParticleType::BLOOD,
                                        monPos + glm::vec3(0, 50, 0), 12);

        uint8_t dmgType = 0;
        if (p->damageType == 0)
          dmgType = 7; // Miss
        else if (p->damageType == 2)
          dmgType = 2; // Critical
        else if (p->damageType == 3)
          dmgType = 3; // Excellent

        if (g_state->spawnDamageNumber)
          g_state->spawnDamageNumber(monPos + glm::vec3(0, 80, 0), p->damage,
                                     dmgType);
      }
    }

    // Monster death
    if (headcode == Opcode::MON_DEATH &&
        pktSize >= (int)sizeof(PMSG_MONSTER_DEATH_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_DEATH_SEND *>(pkt);
      int idx = g_state->monsterManager->FindByServerIndex(p->monsterIndex);
      if (idx >= 0)
        g_state->monsterManager->SetMonsterDying(idx);

      uint32_t xp = p->xpReward;
      if (xp > 0) {
        g_state->hero->GainExperience(xp);
        *g_state->serverXP = (int64_t)g_state->hero->GetExperience();
        *g_state->serverLevel = g_state->hero->GetLevel();
        *g_state->serverLevelUpPoints = g_state->hero->GetLevelUpPoints();
        *g_state->serverMaxHP = g_state->hero->GetMaxHP();
        if (g_state->spawnDamageNumber)
          g_state->spawnDamageNumber(g_state->hero->GetPosition(), (int)xp, 9);
      }
    }

    // Monster attack player
    if (headcode == Opcode::MON_ATTACK &&
        pktSize >= (int)sizeof(PMSG_MONSTER_ATTACK_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_ATTACK_SEND *>(pkt);
      int idx = g_state->monsterManager->FindByServerIndex(p->monsterIndex);
      if (idx >= 0)
        g_state->monsterManager->TriggerAttackAnimation(idx);

      if (g_state->hero->IsInSafeZone())
        return;

      *g_state->serverHP = (int)p->remainingHp;
      g_state->hero->SetHP(*g_state->serverHP);

      if (p->remainingHp <= 0) {
        *g_state->serverHP = 0;
        g_state->hero->ForceDie();
      }

      if (p->damage == 0) {
        if (g_state->spawnDamageNumber)
          g_state->spawnDamageNumber(g_state->hero->GetPosition(), 0, 7);
      } else {
        g_state->hero->ApplyHitReaction();
        if (g_state->spawnDamageNumber)
          g_state->spawnDamageNumber(g_state->hero->GetPosition(), p->damage,
                                     8);
      }
    }

    // Monster respawn
    if (headcode == Opcode::MON_RESPAWN &&
        pktSize >= (int)sizeof(PMSG_MONSTER_RESPAWN_SEND)) {
      auto *p = reinterpret_cast<const PMSG_MONSTER_RESPAWN_SEND *>(pkt);
      int idx = g_state->monsterManager->FindByServerIndex(p->monsterIndex);
      if (idx >= 0)
        g_state->monsterManager->RespawnMonster(idx, p->x, p->y, p->hp);
    }

    // Stat allocation response
    if (headcode == Opcode::STAT_ALLOC_RESULT &&
        pktSize >= (int)sizeof(PMSG_STAT_ALLOC_SEND)) {
      auto *resp = reinterpret_cast<const PMSG_STAT_ALLOC_SEND *>(pkt);
      if (resp->result) {
        switch (resp->statType) {
        case 0:
          *g_state->serverStr = resp->newValue;
          break;
        case 1:
          *g_state->serverDex = resp->newValue;
          break;
        case 2:
          *g_state->serverVit = resp->newValue;
          break;
        case 3:
          *g_state->serverEne = resp->newValue;
          break;
        }
        *g_state->serverLevelUpPoints = resp->levelUpPoints;
        *g_state->serverMaxHP = resp->maxLife;

        g_state->hero->LoadStats(
            *g_state->serverLevel, *g_state->serverStr, *g_state->serverDex,
            *g_state->serverVit, *g_state->serverEne,
            (uint64_t)*g_state->serverXP, *g_state->serverLevelUpPoints,
            *g_state->serverHP, *g_state->serverMaxHP, *g_state->serverMP,
            *g_state->serverMaxMP, g_state->hero->GetClass());

        std::cout << "[Net] Stat alloc OK: type=" << (int)resp->statType
                  << " val=" << resp->newValue
                  << " pts=" << resp->levelUpPoints << std::endl;
      }
    }

    // Ground drop spawned
    if (headcode == Opcode::DROP_SPAWN &&
        pktSize >= (int)sizeof(PMSG_DROP_SPAWN_SEND)) {
      auto *p = reinterpret_cast<const PMSG_DROP_SPAWN_SEND *>(pkt);
      for (int i = 0; i < MAX_GROUND_ITEMS; i++) {
        auto &gi = g_state->groundItems[i];
        if (!gi.active) {
          gi.dropIndex = p->dropIndex;
          gi.defIndex = p->defIndex;
          gi.quantity = p->quantity;
          gi.itemLevel = p->itemLevel;

          float h = g_state->terrain->GetHeight(p->worldX, p->worldZ);
          gi.position = glm::vec3(p->worldX, h + 100.0f, p->worldZ);
          gi.timer = 0.0f;
          gi.gravity = 15.0f;
          gi.scale = 1.0f;
          gi.isResting = false;

          if (g_state->getItemRestingAngle)
            g_state->getItemRestingAngle(gi.defIndex, gi.angle, gi.scale);
          gi.angle.y += (float)(rand() % 360);

          gi.active = true;
          break;
        }
      }
    }

    // Pickup result
    if (headcode == Opcode::PICKUP_RESULT &&
        pktSize >= (int)sizeof(PMSG_PICKUP_RESULT_SEND)) {
      auto *p = reinterpret_cast<const PMSG_PICKUP_RESULT_SEND *>(pkt);
      if (p->success) {
        for (int i = 0; i < MAX_GROUND_ITEMS; i++) {
          if (g_state->groundItems[i].active &&
              g_state->groundItems[i].dropIndex == p->dropIndex) {
            g_state->groundItems[i].active = false;
            break;
          }
        }
      }
    }

    // Drop removed
    if (headcode == Opcode::DROP_REMOVE &&
        pktSize >= (int)sizeof(PMSG_DROP_REMOVE_SEND)) {
      auto *p = reinterpret_cast<const PMSG_DROP_REMOVE_SEND *>(pkt);
      for (int i = 0; i < MAX_GROUND_ITEMS; i++) {
        if (g_state->groundItems[i].active &&
            g_state->groundItems[i].dropIndex == p->dropIndex) {
          g_state->groundItems[i].active = false;
          break;
        }
      }
    }

    // Equipment sync (C1 during gameplay)
    if (headcode == Opcode::EQUIPMENT && pktSize >= 4) {
      ParseEquipmentPacket(pkt, pktSize, 3, 4, nullptr);
    }

    // Character stats sync
    if (headcode == Opcode::CHARSTATS &&
        pktSize >= (int)sizeof(PMSG_CHARSTATS_SEND)) {
      auto *stats = reinterpret_cast<const PMSG_CHARSTATS_SEND *>(pkt);
      int oldHP = *g_state->serverHP;
      SyncCharStats(stats);

      g_state->hero->LoadStats(
          stats->level, stats->strength, stats->dexterity, stats->vitality,
          stats->energy, (uint64_t)*g_state->serverXP,
          *g_state->serverLevelUpPoints, *g_state->serverHP,
          *g_state->serverMaxHP, *g_state->serverMP, *g_state->serverMaxMP,
          stats->charClass);

      // Floating heal text if HP increased
      if (*g_state->serverHP > oldHP && oldHP > 0) {
        if (g_state->spawnDamageNumber)
          g_state->spawnDamageNumber(g_state->hero->GetPosition(),
                                     *g_state->serverHP - oldHP, 10);
      }
    }
  }

  // C2 packets (ongoing)
  if (type == 0xC2 && pktSize >= 5) {
    uint8_t headcode = pkt[3];

    // Inventory sync
    if (headcode == Opcode::INV_SYNC) {
      ParseInventorySync(pkt, pktSize);
    }

    // Equipment sync (C2)
    if (headcode == Opcode::EQUIPMENT && pktSize >= 5) {
      ParseEquipmentPacket(pkt, pktSize, 4, 5, nullptr);
    }
  }
}

} // namespace ClientPacketHandler
