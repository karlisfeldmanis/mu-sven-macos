#include "handlers/InventoryHandler.hpp"
#include "PacketDefs.hpp"
#include "Server.hpp"
#include "handlers/CharacterHandler.hpp"
#include <cstdio>
#include <cstring>

namespace InventoryHandler {

void SendInventorySync(Session &session) {
  // Count primary slots only
  int count = 0;
  for (int i = 0; i < 64; i++)
    if (session.bag[i].occupied && session.bag[i].primary)
      count++;

  size_t totalSize = 4 + 4 + 1 + count * sizeof(PMSG_INVENTORY_ITEM);
  std::vector<uint8_t> packet(totalSize, 0);

  auto *head = reinterpret_cast<PWMSG_HEAD *>(packet.data());
  *head = MakeC2Header(static_cast<uint16_t>(totalSize), Opcode::INV_SYNC);

  uint32_t zen = session.zen;
  std::memcpy(packet.data() + 4, &zen, 4);
  packet[8] = static_cast<uint8_t>(count);

  auto *items = reinterpret_cast<PMSG_INVENTORY_ITEM *>(packet.data() + 9);
  int idx = 0;
  for (int i = 0; i < 64; i++) {
    if (session.bag[i].occupied && session.bag[i].primary) {
      items[idx].slot = static_cast<uint8_t>(i);
      items[idx].category = session.bag[i].category;
      items[idx].itemIndex = session.bag[i].itemIndex;
      items[idx].quantity = session.bag[i].quantity;
      items[idx].itemLevel = session.bag[i].itemLevel;
      idx++;
    }
  }

  session.Send(packet.data(), packet.size());
  printf("[Inventory] Sent inventory sync: %d items, %u zen\n", count,
         session.zen);
}

void LoadInventory(Session &session, Database &db, int characterId) {
  auto invItems = db.GetCharacterInventory(characterId);
  printf("[Inventory] Loading %zu inventory items from DB for char %d\n",
         invItems.size(), characterId);
  for (auto &item : invItems) {
    if (item.slot < 64) {
      ItemDefinition itemDef = db.GetItemDefinition(item.defIndex);
      int w = itemDef.width > 0 ? itemDef.width : 1;
      int h = itemDef.height > 0 ? itemDef.height : 1;
      int r = item.slot / 8;
      int c = item.slot % 8;

      for (int hh = 0; hh < h; hh++) {
        for (int ww = 0; ww < w; ww++) {
          int slot = (r + hh) * 8 + (c + ww);
          if (slot < 64) {
            session.bag[slot].defIndex = item.defIndex;
            session.bag[slot].quantity = item.quantity;
            session.bag[slot].itemLevel = item.itemLevel;
            session.bag[slot].occupied = true;
            session.bag[slot].primary = (hh == 0 && ww == 0);
            session.bag[slot].category =
                static_cast<uint8_t>(item.defIndex / 32);
            session.bag[slot].itemIndex =
                static_cast<uint8_t>(item.defIndex % 32);
          }
        }
      }
      if (item.defIndex >= 0) {
        session.bag[item.slot].category =
            static_cast<uint8_t>(item.defIndex / 32);
        session.bag[item.slot].itemIndex =
            static_cast<uint8_t>(item.defIndex % 32);
        auto def = db.GetItemDefinition(session.bag[item.slot].category,
                                        session.bag[item.slot].itemIndex);
        if (def.id > 0) {
          printf("[Inventory] Inv slot %d: defIdx=%d cat=%d idx=%d '%s'\n",
                 item.slot, item.defIndex, def.category, def.itemIndex,
                 def.name.c_str());
        } else {
          printf("[Inventory] Inv slot %d: defIdx=%d (cat=%d idx=%d) - no DB "
                 "def\n",
                 item.slot, item.defIndex, session.bag[item.slot].category,
                 session.bag[item.slot].itemIndex);
        }
      }
    }
  }
}

void HandleInventoryMove(Session &session, const std::vector<uint8_t> &packet,
                         Database &db) {
  if (packet.size() < sizeof(PMSG_INVENTORY_MOVE_RECV))
    return;
  const auto *mv =
      reinterpret_cast<const PMSG_INVENTORY_MOVE_RECV *>(packet.data());

  uint8_t from = mv->fromSlot;
  uint8_t to = mv->toSlot;
  if (from >= 64 || to >= 64)
    return;

  if (session.bag[from].occupied && session.bag[from].primary) {
    int16_t defIdx = session.bag[from].defIndex;
    uint8_t qty = session.bag[from].quantity;
    uint8_t lvl = session.bag[from].itemLevel;

    auto itemDef = db.GetItemDefinition(defIdx);
    int w = itemDef.width > 0 ? itemDef.width : 1;
    int h = itemDef.height > 0 ? itemDef.height : 1;

    int fromRow = from / 8, fromCol = from % 8;
    for (int hh = 0; hh < h; hh++) {
      for (int ww = 0; ww < w; ww++) {
        int s = (fromRow + hh) * 8 + (fromCol + ww);
        if (s < 64) {
          session.bag[s].occupied = false;
          session.bag[s].primary = false;
        }
      }
    }

    int toRow = to / 8, toCol = to % 8;
    bool canFit = (toCol + w <= 8 && toRow + h <= 8);
    if (canFit) {
      for (int hh = 0; hh < h && canFit; hh++) {
        for (int ww = 0; ww < w && canFit; ww++) {
          if (session.bag[(toRow + hh) * 8 + (toCol + ww)].occupied)
            canFit = false;
        }
      }
    }

    if (canFit) {
      for (int hh = 0; hh < h; hh++) {
        for (int ww = 0; ww < w; ww++) {
          int s = (toRow + hh) * 8 + (toCol + ww);
          session.bag[s].occupied = true;
          session.bag[s].primary = (hh == 0 && ww == 0);
          session.bag[s].defIndex = defIdx;
          if (session.bag[s].primary) {
            session.bag[s].quantity = qty;
            session.bag[s].itemLevel = lvl;
          }
        }
      }
      db.DeleteCharacterInventoryItem(session.characterId, from);
      db.SaveCharacterInventory(session.characterId, defIdx, qty, lvl, to);
      printf("[Inventory] Server-side move def=%d from %d to %d\n", defIdx,
             from, to);
    } else {
      for (int hh = 0; hh < h; hh++) {
        for (int ww = 0; ww < w; ww++) {
          int s = (fromRow + hh) * 8 + (fromCol + ww);
          session.bag[s].occupied = true;
          session.bag[s].primary = (hh == 0 && ww == 0);
          session.bag[s].defIndex = defIdx;
          if (session.bag[s].primary) {
            session.bag[s].quantity = qty;
            session.bag[s].itemLevel = lvl;
          }
        }
      }
      printf("[Inventory] Authoritative move FAILED for def=%d to %d\n", defIdx,
             to);
      SendInventorySync(session);
    }
  }
}

void HandlePickup(Session &session, const std::vector<uint8_t> &packet,
                  GameWorld &world, Server &server, Database &db) {
  if (packet.size() < sizeof(PMSG_PICKUP_RECV))
    return;
  const auto *pick = reinterpret_cast<const PMSG_PICKUP_RECV *>(packet.data());

  auto *drop = world.FindDrop(pick->dropIndex);

  PMSG_PICKUP_RESULT_SEND result{};
  result.h = MakeC1Header(sizeof(result), Opcode::PICKUP_RESULT);
  result.dropIndex = pick->dropIndex;

  if (drop) {
    result.defIndex = drop->defIndex;
    result.quantity = drop->quantity;
    result.itemLevel = drop->itemLevel;

    if (drop->defIndex == -1) {
      // Zen pickup
      session.zen += drop->quantity;
      db.UpdateCharacterMoney(session.characterId, session.zen);
      result.success = 1;
      session.Send(&result, sizeof(result));
      SendInventorySync(session);
      printf("[Inventory] Zen pickup: +%d (total=%u)\n", drop->quantity,
             session.zen);
    } else {
      uint8_t cat = static_cast<uint8_t>(drop->defIndex / 32);
      uint8_t idx = static_cast<uint8_t>(drop->defIndex % 32);
      ItemDefinition itemDef = db.GetItemDefinition(cat, idx);

      if (itemDef.id == 0) {
        printf("[Inventory] Unknown item defIndex=%d (cat=%d idx=%d)\n",
               drop->defIndex, cat, idx);
      }

      int w = itemDef.width;
      int h = itemDef.height;
      if (w == 0 || h == 0) {
        w = 1;
        h = 1;
      }

      // Stackable items (category 14 = potions): try to merge into existing stack
      static constexpr uint8_t MAX_STACK = 20;
      if (cat == 14 && w == 1 && h == 1) {
        for (int i = 0; i < 64; i++) {
          if (session.bag[i].occupied && session.bag[i].primary &&
              session.bag[i].defIndex == drop->defIndex &&
              session.bag[i].quantity < MAX_STACK) {
            session.bag[i].quantity++;
            db.SaveCharacterInventory(session.characterId, drop->defIndex,
                                      session.bag[i].quantity,
                                      session.bag[i].itemLevel,
                                      static_cast<uint8_t>(i));
            printf("[Inventory] Stacked potion def=%d in slot %d (qty=%d)\n",
                   drop->defIndex, i, session.bag[i].quantity);
            result.success = 1;
            session.Send(&result, sizeof(result));
            SendInventorySync(session);
            return;
          }
        }
      }

      bool placed = false;
      int startSlot = 0;

      for (int r = 0; r <= 8 - h; r++) {
        for (int c = 0; c <= 8 - w; c++) {
          bool fits = true;
          for (int rr = 0; rr < h; rr++) {
            for (int cc = 0; cc < w; cc++) {
              int slot = (r + rr) * 8 + (c + cc);
              if (session.bag[slot].occupied) {
                fits = false;
                break;
              }
            }
            if (!fits)
              break;
          }

          if (fits) {
            startSlot = r * 8 + c;
            for (int rr = 0; rr < h; rr++) {
              for (int cc = 0; cc < w; cc++) {
                int s = (r + rr) * 8 + (c + cc);
                session.bag[s].occupied = true;
                session.bag[s].primary = (rr == 0 && cc == 0);
                session.bag[s].category = cat;
                session.bag[s].itemIndex = idx;
                session.bag[s].defIndex = drop->defIndex;
                session.bag[s].quantity = drop->quantity;
                session.bag[s].itemLevel = drop->itemLevel;
              }
            }
            db.SaveCharacterInventory(session.characterId, drop->defIndex,
                                      drop->quantity, drop->itemLevel,
                                      static_cast<uint8_t>(startSlot));
            printf("[Inventory] Pickup: def=%d (cat=%d idx=%d) -> bag slot %d\n",
                   drop->defIndex, cat, idx, startSlot);
            placed = true;
            goto done_placement;
          }
        }
      }
    done_placement:;

      if (placed) {
        result.success = 1;
        session.Send(&result, sizeof(result));
        SendInventorySync(session);
      } else {
        result.success = 0;
        session.Send(&result, sizeof(result));
        printf("[Inventory] Bag full, cannot pick up item def=%d\n",
               drop->defIndex);
      }
    }

    printf("[Inventory] Pickup drop %d by fd=%d (def=%d qty=%d +%d)\n",
           pick->dropIndex, session.GetFd(), drop->defIndex, drop->quantity,
           drop->itemLevel);

    world.RemoveDrop(pick->dropIndex);
    PMSG_DROP_REMOVE_SEND rmPkt{};
    rmPkt.h = MakeC1Header(sizeof(rmPkt), Opcode::DROP_REMOVE);
    rmPkt.dropIndex = pick->dropIndex;
    server.Broadcast(&rmPkt, sizeof(rmPkt));
  } else {
    result.success = 0;
    result.defIndex = 0;
    result.quantity = 0;
    result.itemLevel = 0;
    session.Send(&result, sizeof(result));
  }
}

void HandleItemUse(Session &session, const std::vector<uint8_t> &packet,
                   Database &db) {
  if (packet.size() < sizeof(PMSG_ITEM_USE_RECV))
    return;
  const auto *req = reinterpret_cast<const PMSG_ITEM_USE_RECV *>(packet.data());

  if (req->slot >= 64)
    return;

  if (session.potionCooldown > 0.0f) {
    printf("[Inventory] Rejecting item use fd=%d: Cooldown active (%.1fs)\n",
           session.GetFd(), session.potionCooldown);
    return;
  }

  auto &item = session.bag[req->slot];
  if (!item.occupied || !item.primary) {
    printf("[Inventory] Rejecting item use fd=%d: Slot %d empty or secondary\n",
           session.GetFd(), req->slot);
    return;
  }

  // Skill orbs (category 12) — learn skill on use
  if (item.category == 12) {
    // Map orb itemIndex to skill ID
    static const struct {
      uint8_t orbIndex;
      uint8_t skillId;
    } orbSkillMap[] = {
        {20, 19}, // Orb of Falling Slash → Falling Slash
        {21, 20}, // Orb of Lunge → Lunge
        {22, 21}, // Orb of Uppercut → Uppercut
        {23, 22}, // Orb of Cyclone → Cyclone
        {24, 23}, // Orb of Slash → Slash
        {7, 41},  // Orb of Twisting Slash → Twisting Slash
        {12, 42}, // Orb of Rageful Blow → Rageful Blow
        {19, 43}, // Orb of Death Stab → Death Stab
    };

    uint8_t skillId = 0;
    for (auto &m : orbSkillMap) {
      if (m.orbIndex == item.itemIndex) {
        skillId = m.skillId;
        break;
      }
    }

    if (skillId > 0) {
      if (db.HasSkill(session.characterId, skillId)) {
        printf("[Inventory] fd=%d already knows skill %d\n", session.GetFd(),
               skillId);
        return;
      }

      // Learn the skill
      db.LearnSkill(session.characterId, skillId);
      session.learnedSkills.push_back(skillId);

      // Consume the orb
      if (item.quantity > 1) {
        item.quantity--;
        db.SaveCharacterInventory(session.characterId, item.defIndex,
                                  item.quantity, item.itemLevel, req->slot);
      } else {
        session.bag[req->slot] = {};
        db.DeleteCharacterInventoryItem(session.characterId, req->slot);
      }

      // Send updated skill list and inventory
      CharacterHandler::SendSkillList(session);
      SendInventorySync(session);

      printf("[Inventory] fd=%d learned skill %d from orb idx=%d\n",
             session.GetFd(), skillId, item.itemIndex);
      return;
    }
  }

  if (item.category == 14) {
    int healAmount = 0;
    if (item.itemIndex == 0)
      healAmount = 10;
    else if (item.itemIndex == 1)
      healAmount = 20;
    else if (item.itemIndex == 2)
      healAmount = 50;
    else if (item.itemIndex == 3)
      healAmount = 100;

    if (healAmount > 0) {
      // Full HP check — don't consume potion if already at max
      if (session.hp >= session.maxHp) {
        printf("[Inventory] Rejecting item use fd=%d: HP already full (%d/%d)\n",
               session.GetFd(), session.hp, session.maxHp);
        return;
      }

      session.hp += healAmount;
      if (session.hp > session.maxHp)
        session.hp = session.maxHp;

      session.potionCooldown = 30.0f;

      if (item.quantity > 1) {
        item.quantity--;
        db.SaveCharacterInventory(session.characterId, item.defIndex,
                                  item.quantity, item.itemLevel, req->slot);
      } else {
        ItemDefinition def = db.GetItemDefinition(item.defIndex);
        int w = def.width > 0 ? def.width : 1;
        int h = def.height > 0 ? def.height : 1;
        int r = req->slot / 8;
        int c = req->slot % 8;
        for (int hh = 0; hh < h; hh++) {
          for (int ww = 0; ww < w; ww++) {
            int s = (r + hh) * 8 + (c + ww);
            if (s < 64)
              session.bag[s] = {};
          }
        }
        db.DeleteCharacterInventoryItem(session.characterId, req->slot);
      }

      printf("[Inventory] Item used fd=%d: Healed %d HP. New HP: %d/%d. "
             "Cooldown started.\n",
             session.GetFd(), healAmount, session.hp, session.maxHp);

      CharacterHandler::SendCharStats(session);
      SendInventorySync(session);

      db.UpdateCharacterStats(
          session.characterId, session.level, session.strength,
          session.dexterity, session.vitality, session.energy,
          static_cast<uint16_t>(session.hp),
          static_cast<uint16_t>(session.maxHp), session.levelUpPoints,
          session.experience, session.quickSlotDefIndex);
    }
  }
}

void HandleItemDrop(Session &session, const std::vector<uint8_t> &packet,
                    GameWorld &world, Server &server, Database &db) {
  if (packet.size() < sizeof(PMSG_ITEM_DROP_RECV))
    return;
  const auto *req = reinterpret_cast<const PMSG_ITEM_DROP_RECV *>(packet.data());

  if (req->slot >= 64)
    return;
  if (session.dead)
    return;

  auto &item = session.bag[req->slot];
  if (!item.occupied || !item.primary) {
    printf("[Inventory] Drop rejected fd=%d: slot %d empty\n", session.GetFd(),
           req->slot);
    return;
  }

  int16_t defIndex = item.defIndex;
  uint8_t quantity = item.quantity;
  uint8_t itemLevel = item.itemLevel;

  // Get item dimensions to clear all occupied slots
  ItemDefinition def = db.GetItemDefinition(defIndex);
  int w = def.width > 0 ? def.width : 1;
  int h = def.height > 0 ? def.height : 1;
  int r = req->slot / 8;
  int c = req->slot % 8;

  // Clear all slots occupied by this item
  for (int hh = 0; hh < h; hh++) {
    for (int ww = 0; ww < w; ww++) {
      int s = (r + hh) * 8 + (c + ww);
      if (s < 64)
        session.bag[s] = {};
    }
  }
  db.DeleteCharacterInventoryItem(session.characterId, req->slot);

  // Spawn drop on the ground near the player
  GroundDrop drop{};
  drop.index = world.AllocDropIndex();
  drop.defIndex = defIndex;
  drop.quantity = quantity;
  drop.itemLevel = itemLevel;
  drop.worldX = session.worldX + (float)(rand() % 60 - 30);
  drop.worldZ = session.worldZ + (float)(rand() % 60 - 30);
  drop.age = 0.0f;
  world.AddDrop(drop);

  // Broadcast drop spawn to all clients
  PMSG_DROP_SPAWN_SEND spawnPkt{};
  spawnPkt.h = MakeC1Header(sizeof(spawnPkt), Opcode::DROP_SPAWN);
  spawnPkt.dropIndex = drop.index;
  spawnPkt.defIndex = drop.defIndex;
  spawnPkt.quantity = drop.quantity;
  spawnPkt.itemLevel = drop.itemLevel;
  spawnPkt.worldX = drop.worldX;
  spawnPkt.worldZ = drop.worldZ;
  server.Broadcast(&spawnPkt, sizeof(spawnPkt));

  // Sync inventory to client
  SendInventorySync(session);

  printf("[Inventory] Player fd=%d dropped item def=%d from slot %d at "
         "(%.0f,%.0f)\n",
         session.GetFd(), defIndex, req->slot, drop.worldX, drop.worldZ);
}

} // namespace InventoryHandler
