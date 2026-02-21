#include "handlers/CharacterHandler.hpp"
#include "PacketDefs.hpp"
#include "StatCalculator.hpp"
#include "handlers/InventoryHandler.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace CharacterHandler {

void SendCharStats(Session &session, Database &db, int characterId) {
  CharacterData c = db.GetCharacterById(characterId);
  if (c.id == 0) {
    printf("[Character] Character %d not found for stats send\n", characterId);
    return;
  }

  // Pre-calculate equipment defense for accurate stat reporting
  auto equip = db.GetCharacterEquipment(characterId);
  session.totalDefense = 0;
  for (auto &slot : equip) {
    auto itemDef = db.GetItemDefinition(slot.category, slot.itemIndex);
    if (itemDef.id > 0) {
      session.totalDefense += itemDef.defense + slot.itemLevel * 2;
    }
  }

  // Sync session state from DB data
  session.characterId = c.id;
  session.characterName = c.name;
  session.charClass = c.charClass;
  session.levelUpPoints = c.levelUpPoints;
  session.level = c.level;
  session.strength = c.strength;
  session.dexterity = c.dexterity;
  session.vitality = c.vitality;
  session.energy = c.energy;

  CharacterClass charCls = static_cast<CharacterClass>(c.charClass);
  session.classCode = c.charClass;

  session.maxHp = StatCalculator::CalculateMaxHP(charCls, c.level, c.vitality);
  session.hp = std::min(static_cast<int>(c.life), session.maxHp);
  session.maxMana = StatCalculator::CalculateMaxManaOrAG(
      charCls, c.level, c.strength, c.dexterity, c.vitality, c.energy);
  session.mana = std::min(static_cast<int>(c.mana), session.maxMana);

  session.experience = c.experience;
  session.quickSlotDefIndex = c.quickSlotDefIndex;

  PMSG_CHARSTATS_SEND pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::CHARSTATS);
  pkt.characterId = static_cast<uint16_t>(c.id);
  pkt.level = c.level;
  pkt.strength = c.strength;
  pkt.dexterity = c.dexterity;
  pkt.vitality = c.vitality;
  pkt.energy = c.energy;
  pkt.life = static_cast<uint16_t>(session.hp);
  pkt.maxLife = static_cast<uint16_t>(session.maxHp);
  pkt.mana = static_cast<uint16_t>(session.mana);
  pkt.maxMana = static_cast<uint16_t>(session.maxMana);
  pkt.levelUpPoints = c.levelUpPoints;
  pkt.experienceLo = static_cast<uint32_t>(c.experience & 0xFFFFFFFF);
  pkt.experienceHi = static_cast<uint32_t>(c.experience >> 32);
  pkt.charClass = c.charClass;
  pkt.quickSlotDefIndex = c.quickSlotDefIndex;

  bool hasBow = false;
  pkt.attackSpeed = static_cast<uint16_t>(
      StatCalculator::CalculateAttackSpeed(charCls, session.dexterity, hasBow));
  pkt.magicSpeed = static_cast<uint16_t>(
      StatCalculator::CalculateMagicSpeed(charCls, session.dexterity));
  pkt.defense = static_cast<uint16_t>(
      StatCalculator::CalculateDefense(charCls, session.dexterity) +
      session.totalDefense);

  session.Send(&pkt, sizeof(pkt));
  printf("[Character] Sent char stats to fd=%d: Lv%d STR=%d DEX=%d VIT=%d "
         "ENE=%d HP=%d/%d XP=%llu pts=%d DEF=%d SPD=%d\n",
         session.GetFd(), c.level, c.strength, c.dexterity, c.vitality,
         c.energy, session.hp, session.maxHp, (unsigned long long)c.experience,
         c.levelUpPoints, pkt.defense, pkt.attackSpeed);
}

void SendCharStats(Session &session) {
  PMSG_CHARSTATS_SEND pkt{};
  pkt.h = MakeC1Header(sizeof(pkt), Opcode::CHARSTATS);
  pkt.characterId = static_cast<uint16_t>(session.characterId);
  pkt.level = session.level;
  pkt.strength = session.strength;
  pkt.dexterity = session.dexterity;
  pkt.vitality = session.vitality;
  pkt.energy = session.energy;
  pkt.life = static_cast<uint16_t>(session.hp);
  pkt.maxLife = static_cast<uint16_t>(session.maxHp);
  pkt.mana = static_cast<uint16_t>(session.mana);
  pkt.maxMana = static_cast<uint16_t>(session.maxMana);
  pkt.levelUpPoints = session.levelUpPoints;
  CharacterClass charCls = static_cast<CharacterClass>(session.charClass);
  bool hasBow = false;
  pkt.attackSpeed = static_cast<uint16_t>(
      StatCalculator::CalculateAttackSpeed(charCls, session.dexterity, hasBow));
  pkt.magicSpeed = static_cast<uint16_t>(
      StatCalculator::CalculateMagicSpeed(charCls, session.dexterity));
  pkt.defense = static_cast<uint16_t>(
      StatCalculator::CalculateDefense(charCls, session.dexterity) +
      session.totalDefense);
  pkt.experienceLo = static_cast<uint32_t>(session.experience & 0xFFFFFFFF);
  pkt.experienceHi = static_cast<uint32_t>(session.experience >> 32);
  pkt.charClass = session.charClass;
  pkt.quickSlotDefIndex = session.quickSlotDefIndex;

  session.Send(&pkt, sizeof(pkt));
}

void SendEquipment(Session &session, Database &db, int characterId) {
  auto equip = db.GetCharacterEquipment(characterId);
  if (equip.empty()) {
    printf("[Character] No equipment for character %d\n", characterId);
    return;
  }

  size_t entrySize = 1 + 1 + 1 + 1 + 32;
  size_t totalSize = 5 + equip.size() * entrySize;
  std::vector<uint8_t> packet(totalSize, 0);

  packet[0] = 0xC2;
  packet[1] = static_cast<uint8_t>(totalSize >> 8);
  packet[2] = static_cast<uint8_t>(totalSize & 0xFF);
  packet[3] = Opcode::EQUIPMENT;
  packet[4] = static_cast<uint8_t>(equip.size());

  for (size_t i = 0; i < equip.size(); i++) {
    size_t off = 5 + i * entrySize;
    packet[off + 0] = equip[i].slot;
    packet[off + 1] = equip[i].category;
    packet[off + 2] = equip[i].itemIndex;
    packet[off + 3] = equip[i].itemLevel;

    auto itemDef = db.GetItemDefinition(equip[i].category, equip[i].itemIndex);
    strncpy(reinterpret_cast<char *>(&packet[off + 4]),
            itemDef.modelFile.c_str(), 31);
  }

  session.Send(packet.data(), packet.size());
  printf("[Character] Sent %zu equipment slots to fd=%d. Size=%zu\n",
         equip.size(), session.GetFd(), totalSize);
}

// OpenMU weapon damage bonus per item level (+0 to +15)
static const int kWeaponDmgBonus[] = {0,3,6,9,12,15,18,21,24,27,31,36,42,49,57,66};
// OpenMU armor defense bonus per item level
static const int kArmorDefBonus[]  = {0,3,6,9,12,15,18,21,24,27,31,36,42,49,57,66};
// Shields: +1 per level
static const int kShieldDefBonus[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

static int GetWeaponLevelBonus(int itemLevel) {
  if (itemLevel < 0) return 0;
  if (itemLevel < 16) return kWeaponDmgBonus[itemLevel];
  return 66 + (itemLevel - 15) * 8;
}
static int GetDefenseLevelBonus(int category, int itemLevel) {
  if (itemLevel < 0) return 0;
  if (category == 6) { // Shield
    return itemLevel < 16 ? kShieldDefBonus[itemLevel] : itemLevel;
  }
  return itemLevel < 16 ? kArmorDefBonus[itemLevel] : 66 + (itemLevel - 15) * 8;
}

void RefreshCombatStats(Session &session, Database &db, int characterId) {
  auto equip = db.GetCharacterEquipment(characterId);
  session.weaponDamageMin = 0;
  session.weaponDamageMax = 0;
  session.totalDefense = 0;

  // Track per-hand weapon damage for dual-wield calculation
  int rightDmgMin = 0, rightDmgMax = 0;
  int leftDmgMin = 0, leftDmgMax = 0;
  bool hasRightWeapon = false, hasLeftWeapon = false;

  for (auto &slot : equip) {
    auto itemDef = db.GetItemDefinition(slot.category, slot.itemIndex);
    if (itemDef.id > 0) {
      int wpnBonus = GetWeaponLevelBonus(slot.itemLevel);
      if (slot.slot == 0 && slot.category <= 5) { // R.Hand weapon
        rightDmgMin = itemDef.damageMin + wpnBonus;
        rightDmgMax = itemDef.damageMax + wpnBonus;
        hasRightWeapon = true;
      }
      if (slot.slot == 1 && slot.category <= 5) { // L.Hand weapon (dual-wield)
        leftDmgMin = itemDef.damageMin + wpnBonus;
        leftDmgMax = itemDef.damageMax + wpnBonus;
        hasLeftWeapon = true;
      }
      session.totalDefense += itemDef.defense + GetDefenseLevelBonus(slot.category, slot.itemLevel);

      if (slot.slot == 7) { // Wings
        rightDmgMin += 15;
        rightDmgMax += 25;
      }
    }
  }

  // Main 5.2 dual-wield: DK/MG with weapons in both hands get 55% per hand
  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  if (hasRightWeapon && hasLeftWeapon &&
      (charCls == CharacterClass::CLASS_DK ||
       charCls == CharacterClass::CLASS_MG)) {
    session.weaponDamageMin =
        (rightDmgMin * 55) / 100 + (leftDmgMin * 55) / 100;
    session.weaponDamageMax =
        (rightDmgMax * 55) / 100 + (leftDmgMax * 55) / 100;
    printf("[Character] Dual-wield: R(%d-%d)*55%% + L(%d-%d)*55%% = %d-%d\n",
           rightDmgMin, rightDmgMax, leftDmgMin, leftDmgMax,
           session.weaponDamageMin, session.weaponDamageMax);
  } else {
    session.weaponDamageMin = rightDmgMin + leftDmgMin;
    session.weaponDamageMax = rightDmgMax + leftDmgMax;
  }
}

void HandleCharSave(Session &session, const std::vector<uint8_t> &packet,
                    Database &db) {
  if (packet.size() < sizeof(PMSG_CHARSAVE_RECV))
    return;
  const auto *save =
      reinterpret_cast<const PMSG_CHARSAVE_RECV *>(packet.data());

  int charId = save->characterId;
  if (charId <= 0)
    charId = 1;

  session.quickSlotDefIndex = save->quickSlotDefIndex;

  db.UpdateCharacterStats(charId, session.level, session.strength,
                          session.dexterity, session.vitality, session.energy,
                          save->life, save->maxLife, session.levelUpPoints,
                          session.experience, session.quickSlotDefIndex);

  session.hp = save->life;
  if (save->maxLife >= save->life) {
    session.maxHp = save->maxLife;
  }
  if (session.dead && save->life > 0) {
    session.dead = false;
    printf("[Character] Player fd=%d respawned (HP=%d)\n", session.GetFd(),
           save->life);
  }

  printf("[Character] Character %d saved from fd=%d (lvl=%d pts=%d)\n", charId,
         session.GetFd(), save->level, save->levelUpPoints);
}

void HandleEquip(Session &session, const std::vector<uint8_t> &packet,
                 Database &db) {
  if (packet.size() < sizeof(PMSG_EQUIP_RECV))
    return;
  const auto *eq = reinterpret_cast<const PMSG_EQUIP_RECV *>(packet.data());

  int charId = eq->characterId;
  if (charId <= 0)
    charId = 1;

  // Authoritative requirement check (if not un-equipping)
  if (eq->category != 0xFF) {
    auto itemDef = db.GetItemDefinition(eq->category, eq->itemIndex);
    if (itemDef.id > 0) {
      if (session.level < itemDef.level ||
          session.strength < itemDef.reqStrength ||
          session.dexterity < itemDef.reqDexterity) {
        printf(
            "[Character] Rejecting equip char=%d cat=%d idx=%d: reqs not met "
            "(Lv:%d/%d Str:%d/%d Dex:%d/%d)\n",
            charId, eq->category, eq->itemIndex, (int)session.level,
            itemDef.level, (int)session.strength, itemDef.reqStrength,
            (int)session.dexterity, itemDef.reqDexterity);
        SendEquipment(session, db, charId);
        return;
      }
      int bitIndex = session.charClass >> 4;
      if (!(itemDef.classFlags & (1 << bitIndex))) {
        printf("[Character] Rejecting equip char=%d cat=%d idx=%d: class "
               "mismatch (Class:%d Bit:%d Flags:0x%X)\n",
               charId, eq->category, eq->itemIndex, (int)session.charClass,
               bitIndex, itemDef.classFlags);
        SendEquipment(session, db, charId);
        return;
      }

      // Slot 1 (left hand) constraint logic
      if (eq->slot == 1) {
        // If equipping a weapon (categories 0-5) in the left hand
        if (eq->category <= 5) {
          // 1. Must not be a two-handed weapon
          if (itemDef.twoHanded) {
            printf("[Character] Rejecting equip char=%d cat=%d: cannot dual "
                   "wield two-handed weapon\n",
                   charId, eq->category);
            SendEquipment(session, db, charId);
            return;
          }
          // 2. Must be DK (class 1) or MG (class 3) to dual wield
          CharacterClass charCls =
              static_cast<CharacterClass>(session.classCode);
          if (charCls != CharacterClass::CLASS_DK &&
              charCls != CharacterClass::CLASS_MG) {
            printf("[Character] Rejecting equip char=%d cat=%d: only DK/MG can "
                   "dual wield\n",
                   charId, eq->category);
            SendEquipment(session, db, charId);
            return;
          }
        } else if (eq->category != 6) { // Not a shield either
          printf("[Character] Rejecting equip char=%d cat=%d: invalid item for "
                 "left hand\n",
                 charId, eq->category);
          SendEquipment(session, db, charId);
          return;
        }
      }

      // Two-handed weapon logic (Slot 0 only)
      if (eq->slot == 0 && itemDef.twoHanded) {
        auto equip = db.GetCharacterEquipment(charId);
        for (auto &slot : equip) {
          if (slot.slot == 1 && slot.category != 0xFF) {
            printf("[Character] Rejecting equip char=%d cat=%d: left hand not "
                   "empty for two-handed weapon\n",
                   charId, eq->category);
            SendEquipment(session, db, charId);
            return;
          }
        }
      }
    }
  }

  // Save old equipment in this slot for swap/unequip
  auto oldEquip = db.GetCharacterEquipment(charId);
  uint8_t oldCat = 0xFF, oldIdx = 0, oldLvl = 0;
  for (auto &oe : oldEquip) {
    if (oe.slot == eq->slot && oe.category != 0xFF) {
      oldCat = oe.category;
      oldIdx = oe.itemIndex;
      oldLvl = oe.itemLevel;
      break;
    }
  }

  // When equipping (category != 0xFF), remove item from inventory bag
  if (eq->category != 0xFF) {
    int16_t defIdx = (int16_t)((int)eq->category * 32 + (int)eq->itemIndex);
    for (int i = 0; i < 64; i++) {
      if (session.bag[i].occupied && session.bag[i].primary &&
          session.bag[i].defIndex == defIdx) {
        // Clear bag slot (including multi-cell)
        auto itemDef = db.GetItemDefinition(defIdx);
        int w = itemDef.width > 0 ? itemDef.width : 1;
        int h = itemDef.height > 0 ? itemDef.height : 1;
        int r = i / 8, c = i % 8;
        for (int hh = 0; hh < h; hh++) {
          for (int ww = 0; ww < w; ww++) {
            int s = (r + hh) * 8 + (c + ww);
            if (s < 64)
              session.bag[s] = {};
          }
        }
        db.DeleteCharacterInventoryItem(charId, i);
        printf("[Character] Removed equipped item from bag slot %d (def=%d)\n",
               i, defIdx);
        break;
      }
    }
  }

  // Move old equipped item back to inventory (both unequip AND swap cases)
  if (oldCat != 0xFF && !(eq->category == oldCat && eq->itemIndex == oldIdx)) {
    int16_t oldDef = (int16_t)((int)oldCat * 32 + (int)oldIdx);
    auto itemDef = db.GetItemDefinition(oldDef);
    int w = itemDef.width > 0 ? itemDef.width : 1;
    int h = itemDef.height > 0 ? itemDef.height : 1;
    // Find empty space in bag
    bool placed = false;
    for (int r = 0; r <= 8 - h && !placed; r++) {
      for (int c2 = 0; c2 <= 8 - w && !placed; c2++) {
        bool fits = true;
        for (int hh = 0; hh < h && fits; hh++)
          for (int ww = 0; ww < w && fits; ww++)
            if (session.bag[(r + hh) * 8 + (c2 + ww)].occupied)
              fits = false;
        if (fits) {
          int startSlot = r * 8 + c2;
          for (int hh = 0; hh < h; hh++) {
            for (int ww = 0; ww < w; ww++) {
              int s = (r + hh) * 8 + (c2 + ww);
              session.bag[s].occupied = true;
              session.bag[s].primary = (hh == 0 && ww == 0);
              session.bag[s].defIndex = oldDef;
              session.bag[s].category = oldCat;
              session.bag[s].itemIndex = oldIdx;
              if (hh == 0 && ww == 0) {
                session.bag[s].quantity = 1;
                session.bag[s].itemLevel = oldLvl;
              }
            }
          }
          db.SaveCharacterInventory(charId, oldDef, 1, oldLvl,
                                    static_cast<uint8_t>(startSlot));
          printf("[Character] Unequipped item saved to bag slot %d (def=%d)\n",
                 startSlot, oldDef);
          placed = true;
        }
      }
    }
    if (!placed) {
      printf("[Character] WARNING: No bag space for unequipped item def=%d\n",
             oldDef);
    }
  }

  db.UpdateEquipment(charId, eq->slot, eq->category, eq->itemIndex,
                     eq->itemLevel);

  RefreshCombatStats(session, db, charId);

  printf("[Character] Equipment change fd=%d: char=%d slot=%d cat=%d idx=%d "
         "+%d (weapon=%d-%d def=%d)\n",
         session.GetFd(), charId, eq->slot, eq->category, eq->itemIndex,
         eq->itemLevel, session.weaponDamageMin, session.weaponDamageMax,
         session.totalDefense);

  SendEquipment(session, db, charId);
  InventoryHandler::SendInventorySync(session);
  SendCharStats(session);
}

void HandleStatAlloc(Session &session, const std::vector<uint8_t> &packet,
                     Database &db) {
  if (packet.size() < sizeof(PMSG_STAT_ALLOC_RECV))
    return;
  auto *req = reinterpret_cast<const PMSG_STAT_ALLOC_RECV *>(packet.data());

  PMSG_STAT_ALLOC_SEND resp{};
  resp.h = MakeC1Header(sizeof(resp), Opcode::STAT_ALLOC_RESULT);
  resp.statType = req->statType;

  if (session.levelUpPoints == 0 || req->statType > 3) {
    resp.result = 0;
    session.Send(&resp, sizeof(resp));
    return;
  }

  session.levelUpPoints--;
  switch (req->statType) {
  case 0:
    session.strength++;
    resp.newValue = session.strength;
    break;
  case 1:
    session.dexterity++;
    resp.newValue = session.dexterity;
    break;
  case 2:
    session.vitality++;
    resp.newValue = session.vitality;
    break;
  case 3:
    session.energy++;
    resp.newValue = session.energy;
    break;
  }

  CharacterClass charCls = static_cast<CharacterClass>(session.classCode);
  session.maxHp =
      StatCalculator::CalculateMaxHP(charCls, session.level, session.vitality);
  session.maxMana = StatCalculator::CalculateMaxManaOrAG(
      charCls, session.level, session.strength, session.dexterity,
      session.vitality, session.energy);

  resp.result = 1;
  resp.levelUpPoints = session.levelUpPoints;
  resp.maxLife = static_cast<uint16_t>(session.maxHp);
  session.Send(&resp, sizeof(resp));

  SendCharStats(session);

  db.UpdateCharacterStats(
      session.characterId, session.level, session.strength, session.dexterity,
      session.vitality, session.energy, static_cast<uint16_t>(session.hp),
      static_cast<uint16_t>(session.maxHp), session.levelUpPoints,
      session.experience, session.quickSlotDefIndex);

  printf("[Character] Stat alloc: type=%d newVal=%d pts=%d maxHP=%d\n",
         req->statType, resp.newValue, session.levelUpPoints, session.maxHp);
}

void SendSkillList(Session &session) {
  uint8_t count = static_cast<uint8_t>(session.learnedSkills.size());
  size_t totalSize = 4 + 1 + count; // C2 header(4) + count(1) + skillIds
  std::vector<uint8_t> packet(totalSize, 0);

  auto *head = reinterpret_cast<PWMSG_HEAD *>(packet.data());
  *head = MakeC2Header(static_cast<uint16_t>(totalSize), Opcode::SKILL_LIST);
  packet[4] = count;

  for (int i = 0; i < count; i++) {
    packet[5 + i] = session.learnedSkills[i];
  }

  session.Send(packet.data(), packet.size());
  printf("[Character] Sent %d skills to fd=%d\n", count, session.GetFd());
}

} // namespace CharacterHandler
