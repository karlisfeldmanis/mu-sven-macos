#include "PacketHandler.hpp"
#include "PacketDefs.hpp"
#include "Server.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace PacketHandler {

void SendWelcome(Session &session) {
    PMSG_WELCOME_SEND pkt{};
    pkt.h = MakeC1SubHeader(sizeof(pkt), 0xF1, 0x00);
    pkt.result = 0x01;
    session.Send(&pkt, sizeof(pkt));
    printf("[Handler] Sent welcome to fd=%d\n", session.GetFd());
}

void SendNpcViewport(Session &session, const GameWorld &world) {
    auto packet = world.BuildNpcViewportPacket();
    if (packet.empty()) return;
    session.Send(packet.data(), packet.size());
    printf("[Handler] Sent %zu NPC viewport entries to fd=%d\n",
           world.GetNpcs().size(), session.GetFd());
}

void SendMonsterViewport(Session &session, const GameWorld &world) {
    auto packet = world.BuildMonsterViewportPacket();
    if (packet.empty()) return;
    session.Send(packet.data(), packet.size());
    printf("[Handler] Sent %zu monster viewport entries to fd=%d\n",
           world.GetMonsterInstances().size(), session.GetFd());
}

void SendEquipment(Session &session, Database &db, int characterId) {
    auto equip = db.GetCharacterEquipment(characterId);
    if (equip.empty()) {
        printf("[Handler] No equipment for character %d\n", characterId);
        return;
    }

    // Build C1 packet: header + count + N * slot entries (with full weapon config)
    size_t totalSize = sizeof(PMSG_EQUIPMENT_HEAD) + equip.size() * sizeof(PMSG_EQUIPMENT_SLOT);
    std::vector<uint8_t> packet(totalSize, 0);

    auto *head = reinterpret_cast<PMSG_EQUIPMENT_HEAD *>(packet.data());
    head->h = MakeC1Header(static_cast<uint8_t>(totalSize), 0x24);
    head->count = static_cast<uint8_t>(equip.size());

    auto *slots = reinterpret_cast<PMSG_EQUIPMENT_SLOT *>(packet.data() + sizeof(PMSG_EQUIPMENT_HEAD));
    for (size_t i = 0; i < equip.size(); i++) {
        auto &s = slots[i];
        s.slot = equip[i].slot;
        s.category = equip[i].category;
        s.itemIndex = equip[i].itemIndex;
        s.itemLevel = equip[i].itemLevel;

        // Look up item definition for model file
        auto itemDef = db.GetItemDefinition(equip[i].category, equip[i].itemIndex);
        std::memset(s.modelFile, 0, 32);
        std::strncpy(s.modelFile, itemDef.modelFile.c_str(), 31);

        printf("[Handler] Equipment slot %d: %s (+%d) [%s]\n",
               s.slot, itemDef.name.c_str(), s.itemLevel, s.modelFile);
    }

    session.Send(packet.data(), packet.size());
    printf("[Handler] Sent %zu equipment slots to fd=%d\n", equip.size(), session.GetFd());
}

void SendCharStats(Session &session, Database &db, int characterId) {
    CharacterData c = db.GetCharacter("TestDK"); // Hardcoded for single-character remaster
    if (c.id == 0) {
        printf("[Handler] Character not found for stats send\n");
        return;
    }

    PMSG_CHARSTATS_SEND pkt{};
    pkt.h = MakeC1Header(sizeof(pkt), 0x25);
    pkt.characterId = static_cast<uint16_t>(c.id);
    pkt.level = c.level;
    pkt.strength = c.strength;
    pkt.dexterity = c.dexterity;
    pkt.vitality = c.vitality;
    pkt.energy = c.energy;
    pkt.life = c.life;
    pkt.maxLife = c.maxLife;
    pkt.levelUpPoints = c.levelUpPoints;
    pkt.experienceLo = static_cast<uint32_t>(c.experience & 0xFFFFFFFF);
    pkt.experienceHi = static_cast<uint32_t>(c.experience >> 32);

    session.Send(&pkt, sizeof(pkt));
    printf("[Handler] Sent char stats to fd=%d: Lv%d STR=%d DEX=%d VIT=%d ENE=%d HP=%d/%d XP=%llu pts=%d\n",
           session.GetFd(), c.level, c.strength, c.dexterity, c.vitality, c.energy,
           c.life, c.maxLife, (unsigned long long)c.experience, c.levelUpPoints);
}

void HandleCharSave(Session &session, const std::vector<uint8_t> &packet, Database &db) {
    if (packet.size() < sizeof(PMSG_CHARSAVE_RECV)) return;
    const auto *save = reinterpret_cast<const PMSG_CHARSAVE_RECV *>(packet.data());

    int charId = save->characterId;
    if (charId <= 0) charId = 1; // Fallback to TestDK

    uint64_t experience = static_cast<uint64_t>(save->experienceLo) |
                          (static_cast<uint64_t>(save->experienceHi) << 32);

    db.UpdateCharacterStats(charId, save->level, save->strength, save->dexterity,
                            save->vitality, save->energy, save->life, save->maxLife,
                            save->levelUpPoints, experience);

    // Sync session combat state from save data (handles respawn: client sends
    // full HP after respawning, which resets the dead flag server-side)
    session.hp = save->life;
    session.maxHp = save->maxLife;
    session.strength = save->strength;
    session.dexterity = save->dexterity;
    if (session.dead && save->life > 0) {
        session.dead = false;
        printf("[Handler] Player fd=%d respawned (HP=%d)\n", session.GetFd(), save->life);
    }

    printf("[Handler] Character %d saved from fd=%d\n", charId, session.GetFd());
}

void HandleEquip(Session &session, const std::vector<uint8_t> &packet, Database &db) {
    if (packet.size() < sizeof(PMSG_EQUIP_RECV)) return;
    const auto *eq = reinterpret_cast<const PMSG_EQUIP_RECV *>(packet.data());

    int charId = eq->characterId;
    if (charId <= 0) charId = 1; // Fallback to TestDK

    db.UpdateEquipment(charId, eq->slot, eq->category, eq->itemIndex, eq->itemLevel);

    // Refresh cached combat stats from equipment
    auto equip = db.GetCharacterEquipment(charId);
    session.weaponDamageMin = 0;
    session.weaponDamageMax = 0;
    session.totalDefense = 0;
    for (auto &slot : equip) {
        auto itemDef = db.GetItemDefinition(slot.category, slot.itemIndex);
        if (itemDef.id > 0) {
            if (slot.slot == 0) { // Weapon slot
                session.weaponDamageMin = itemDef.damageMin + slot.itemLevel * 3;
                session.weaponDamageMax = itemDef.damageMax + slot.itemLevel * 3;
            }
            session.totalDefense += itemDef.defense + slot.itemLevel * 2;
        }
    }

    printf("[Handler] Equipment change from fd=%d: char=%d slot=%d cat=%d idx=%d +%d (weapon=%d-%d def=%d)\n",
           session.GetFd(), charId, eq->slot, eq->category, eq->itemIndex, eq->itemLevel,
           session.weaponDamageMin, session.weaponDamageMax, session.totalDefense);
}

void HandleMove(Session &session, const std::vector<uint8_t> &packet, Database &db) {
    if (packet.size() < sizeof(PMSG_MOVE_RECV)) return;
    const auto *move = reinterpret_cast<const PMSG_MOVE_RECV *>(packet.data());

    // Update session world position (grid * 100 = world coords)
    session.worldX = move->y * 100.0f; // MU grid Y → world X
    session.worldZ = move->x * 100.0f; // MU grid X → world Z

    if (session.characterId > 0) {
        db.UpdatePosition(session.characterId, move->x, move->y);
    }
}

void HandlePrecisePosition(Session &session, const std::vector<uint8_t> &packet) {
    if (packet.size() < sizeof(PMSG_PRECISE_POS_RECV)) return;
    const auto *pos = reinterpret_cast<const PMSG_PRECISE_POS_RECV *>(packet.data());
    session.worldX = pos->worldX;
    session.worldZ = pos->worldZ;
}

void HandleLogin(Session &session, const std::vector<uint8_t> &packet, Database &db) {
    if (packet.size() < sizeof(PMSG_LOGIN_RECV)) return;

    PMSG_LOGIN_RECV login;
    std::memcpy(&login, packet.data(), sizeof(login));

    // BUX decode the account and password
    BuxDecode(login.account, 10);
    BuxDecode(login.password, 20);

    // Null-terminate
    char account[11] = {};
    char password[21] = {};
    std::memcpy(account, login.account, 10);
    std::memcpy(password, login.password, 20);

    printf("[Handler] Login attempt: account='%s' from fd=%d\n", account, session.GetFd());

    int accountId = db.ValidateLogin(account, password);

    PMSG_LOGIN_RESULT_SEND result{};
    result.h = MakeC1SubHeader(sizeof(result), 0xF1, 0x01);

    if (accountId > 0) {
        result.result = 0x01; // Success
        session.accountId = accountId;
        printf("[Handler] Login success: accountId=%d\n", accountId);
    } else {
        result.result = 0x00; // Fail
        printf("[Handler] Login failed for '%s'\n", account);
    }

    session.Send(&result, sizeof(result));
}

void HandleCharListRequest(Session &session, Database &db) {
    auto chars = db.GetCharacterList(session.accountId);

    // Build response: header + N entries
    size_t totalSize = sizeof(PMSG_CHARLIST_HEAD) + chars.size() * sizeof(PMSG_CHARLIST_ENTRY);
    std::vector<uint8_t> packet(totalSize, 0);

    auto *head = reinterpret_cast<PMSG_CHARLIST_HEAD *>(packet.data());
    head->h = MakeC1SubHeader(static_cast<uint8_t>(totalSize), 0xF3, 0x00);
    head->classCode = 0;
    head->moveCnt = 0;
    head->count = static_cast<uint8_t>(chars.size());

    auto *entries = reinterpret_cast<PMSG_CHARLIST_ENTRY *>(packet.data() + sizeof(PMSG_CHARLIST_HEAD));
    for (size_t i = 0; i < chars.size(); i++) {
        auto &e = entries[i];
        e.slot = static_cast<uint8_t>(chars[i].slot);
        std::memset(e.name, 0, 10);
        std::strncpy(e.name, chars[i].name.c_str(), 10);
        SetWordBE(reinterpret_cast<uint8_t *>(&e.level), chars[i].level);
        e.ctlCode = 0;
        std::memset(e.charSet, 0, 18);
        // Encode class in charSet[0] bits
        e.charSet[0] = static_cast<uint8_t>((chars[i].charClass >> 4) << 4);
        e.guildStatus = 0xFF;
    }

    session.Send(packet.data(), packet.size());
    printf("[Handler] Sent char list (%zu chars) to fd=%d\n", chars.size(), session.GetFd());
}

void HandleCharSelect(Session &session, const std::vector<uint8_t> &packet,
                      Database &db, GameWorld &world) {
    if (packet.size() < sizeof(PMSG_CHARSELECT_RECV)) return;
    const auto *sel = reinterpret_cast<const PMSG_CHARSELECT_RECV *>(packet.data());

    char name[11] = {};
    std::memcpy(name, sel->name, 10);
    printf("[Handler] Char select: '%s' from fd=%d\n", name, session.GetFd());

    CharacterData c = db.GetCharacter(name);
    if (c.id == 0) {
        printf("[Handler] Character '%s' not found\n", name);
        return;
    }

    session.characterId = c.id;
    session.characterName = c.name;

    // Send character info
    PMSG_CHARINFO_SEND info{};
    info.h = MakeC1SubHeader(sizeof(info), 0xF3, 0x03);
    info.x = c.posX;
    info.y = c.posY;
    info.map = c.mapId;
    info.dir = c.direction;
    std::memset(info.experience, 0, 8);
    std::memset(info.nextExperience, 0, 8);
    SetDwordBE(info.experience + 4, static_cast<uint32_t>(c.experience));
    SetDwordBE(info.nextExperience + 4, 1000); // Next level exp
    info.levelUpPoint = 0;
    info.strength = c.strength;
    info.dexterity = c.dexterity;
    info.vitality = c.vitality;
    info.energy = c.energy;
    info.life = c.life;
    info.maxLife = c.maxLife;
    info.mana = c.mana;
    info.maxMana = c.maxMana;
    info.shield = 0;
    info.maxShield = 0;
    info.bp = 50;
    info.maxBP = 50;
    info.money = c.money;
    info.pkLevel = 3; // Normal
    info.ctlCode = 0;
    info.fruitAddPoint = 0;
    info.maxFruitAddPoint = 0;
    info.leadership = 0;
    info.fruitSubPoint = 0;
    info.maxFruitSubPoint = 0;

    session.Send(&info, sizeof(info));
    session.inWorld = true;

    // Cache full stats for server-side validation
    session.strength = c.strength;
    session.dexterity = c.dexterity;
    session.vitality = c.vitality;
    session.energy = c.energy;
    session.level = c.level;
    session.levelUpPoints = c.levelUpPoints;
    session.experience = c.experience;
    session.hp = c.life;
    session.maxHp = c.maxLife;
    session.worldX = c.posY * 100.0f; // MU grid Y → world X
    session.worldZ = c.posX * 100.0f; // MU grid X → world Z

    // Look up weapon damage from equipment
    auto equip = db.GetCharacterEquipment(c.id);
    session.weaponDamageMin = 0;
    session.weaponDamageMax = 0;
    session.totalDefense = 0;
    for (auto &slot : equip) {
        auto itemDef = db.GetItemDefinition(slot.category, slot.itemIndex);
        if (itemDef.id > 0) {
            if (slot.slot == 0) { // Weapon slot
                session.weaponDamageMin = itemDef.damageMin + slot.itemLevel * 3;
                session.weaponDamageMax = itemDef.damageMax + slot.itemLevel * 3;
            }
            session.totalDefense += itemDef.defense + slot.itemLevel * 2;
        }
    }

    // Load inventory from DB
    session.zen = c.money;
    auto invItems = db.GetCharacterInventory(c.id);
    for (auto &item : invItems) {
        if (item.slot < 64) {
            session.bag[item.slot].defIndex = item.defIndex;
            session.bag[item.slot].quantity = item.quantity;
            session.bag[item.slot].itemLevel = item.itemLevel;
            session.bag[item.slot].occupied = true;
        }
    }

    // Send NPC viewport
    SendNpcViewport(session, world);

    // Send inventory sync
    SendInventorySync(session);

    printf("[Handler] Character '%s' entered Lorencia at (%d,%d) STR=%d weapon=%d-%d def=%d zen=%u inv=%zu\n",
           name, c.posX, c.posY, session.strength, session.weaponDamageMin,
           session.weaponDamageMax, session.totalDefense, session.zen, invItems.size());
}

void HandleAttack(Session &session, const std::vector<uint8_t> &packet,
                  GameWorld &world, Server &server) {
    if (packet.size() < sizeof(PMSG_ATTACK_RECV)) return;
    const auto *atk = reinterpret_cast<const PMSG_ATTACK_RECV *>(packet.data());

    auto *mon = world.FindMonster(atk->monsterIndex);
    if (!mon || mon->state != MonsterInstance::ALIVE) return;

    // DK damage formula: damage = rand(STR/8+weaponMin .. STR/4+weaponMax) - defense
    int baseMin = session.strength / 8 + session.weaponDamageMin;
    int baseMax = session.strength / 4 + session.weaponDamageMax;
    if (baseMax < baseMin) baseMax = baseMin;

    // Defense rate check (miss)
    uint8_t damageType = 1; // normal
    int damage = 0;
    bool missed = false;

    int defRate = mon->defenseRate;
    if (defRate > 0 && rand() % 100 < defRate * 10) {
        missed = true;
        damageType = 0; // miss
    }

    if (!missed) {
        damage = baseMin + (baseMax > baseMin ? rand() % (baseMax - baseMin + 1) : 0);

        // Critical (5%) and Excellent (1%) rolls
        int critRoll = rand() % 100;
        if (critRoll < 1) {
            damage = baseMax * 2; // Excellent
            damageType = 3;
        } else if (critRoll < 6) {
            damage = (int)(baseMax * 1.5f); // Critical
            damageType = 2;
        }

        damage = std::max(1, damage - mon->defense);
        mon->hp -= damage;
    }

    bool killed = mon->hp <= 0;
    if (killed) mon->hp = 0;

    // Broadcast damage result to all clients
    PMSG_DAMAGE_SEND dmgPkt{};
    dmgPkt.h = MakeC1Header(sizeof(dmgPkt), 0x29);
    dmgPkt.monsterIndex = mon->index;
    dmgPkt.damage = static_cast<uint16_t>(damage);
    dmgPkt.damageType = damageType;
    dmgPkt.remainingHp = static_cast<uint16_t>(std::max(0, mon->hp));
    dmgPkt.attackerCharId = static_cast<uint16_t>(session.characterId);
    server.Broadcast(&dmgPkt, sizeof(dmgPkt));

    if (killed) {
        mon->state = MonsterInstance::DYING;
        mon->stateTimer = 0.0f;

        // Calculate XP reward (same formula as client MonsterManager)
        int xp = mon->level * 10;
        if (xp < 1) xp = 1;

        // Broadcast death + XP
        PMSG_MONSTER_DEATH_SEND deathPkt{};
        deathPkt.h = MakeC1Header(sizeof(deathPkt), 0x2A);
        deathPkt.monsterIndex = mon->index;
        deathPkt.killerCharId = static_cast<uint16_t>(session.characterId);
        deathPkt.xpReward = static_cast<uint32_t>(xp);
        server.Broadcast(&deathPkt, sizeof(deathPkt));

        // Spawn drops and broadcast them
        auto drops = world.SpawnDrops(mon->worldX, mon->worldZ);
        for (auto &drop : drops) {
            PMSG_DROP_SPAWN_SEND dropPkt{};
            dropPkt.h = MakeC1Header(sizeof(dropPkt), 0x2B);
            dropPkt.dropIndex = drop.index;
            dropPkt.defIndex = drop.defIndex;
            dropPkt.quantity = drop.quantity;
            dropPkt.itemLevel = drop.itemLevel;
            dropPkt.worldX = drop.worldX;
            dropPkt.worldZ = drop.worldZ;
            server.Broadcast(&dropPkt, sizeof(dropPkt));
        }

        printf("[Handler] Monster %d killed by char %d (dmg=%d, xp=%d, drops=%zu)\n",
               mon->index, session.characterId, damage, xp, drops.size());
    }
}

void SendInventorySync(Session &session) {
    // Count occupied slots
    int count = 0;
    for (int i = 0; i < 64; i++)
        if (session.bag[i].occupied) count++;

    // Build C2 packet: header(4) + zen(4) + count(1) + N * item(4)
    size_t totalSize = 4 + 4 + 1 + count * sizeof(PMSG_INVENTORY_ITEM);
    std::vector<uint8_t> packet(totalSize, 0);

    auto *head = reinterpret_cast<PWMSG_HEAD *>(packet.data());
    *head = MakeC2Header(static_cast<uint16_t>(totalSize), 0x36);

    // Zen (4 bytes after C2 header)
    uint32_t zen = session.zen;
    std::memcpy(packet.data() + 4, &zen, 4);
    packet[8] = static_cast<uint8_t>(count);

    auto *items = reinterpret_cast<PMSG_INVENTORY_ITEM *>(packet.data() + 9);
    int idx = 0;
    for (int i = 0; i < 64; i++) {
        if (session.bag[i].occupied) {
            items[idx].slot = static_cast<uint8_t>(i);
            items[idx].defIndex = session.bag[i].defIndex;
            items[idx].quantity = session.bag[i].quantity;
            items[idx].itemLevel = session.bag[i].itemLevel;
            idx++;
        }
    }

    session.Send(packet.data(), packet.size());
    printf("[Handler] Sent inventory sync: %d items, %u zen\n", count, session.zen);
}

void HandleStatAlloc(Session &session, const std::vector<uint8_t> &packet, Database &db) {
    if (packet.size() < sizeof(PMSG_STAT_ALLOC_RECV)) return;
    auto *req = reinterpret_cast<const PMSG_STAT_ALLOC_RECV *>(packet.data());

    PMSG_STAT_ALLOC_SEND resp{};
    resp.h = MakeC1Header(sizeof(resp), 0x38);
    resp.statType = req->statType;

    if (session.levelUpPoints == 0 || req->statType > 3) {
        resp.result = 0;
        session.Send(&resp, sizeof(resp));
        return;
    }

    session.levelUpPoints--;
    switch (req->statType) {
        case 0: session.strength++; resp.newValue = session.strength; break;
        case 1: session.dexterity++; resp.newValue = session.dexterity; break;
        case 2: session.vitality++; resp.newValue = session.vitality; break;
        case 3: session.energy++; resp.newValue = session.energy; break;
    }

    // Recalc max HP (DK formula: 110 + 2*(level-1) + (VIT-25)*3)
    int newMaxHp = 110 + 2 * (session.level - 1) + (session.vitality - 25) * 3;
    if (newMaxHp < 1) newMaxHp = 1;
    session.maxHp = newMaxHp;

    resp.result = 1;
    resp.levelUpPoints = session.levelUpPoints;
    resp.maxLife = static_cast<uint16_t>(session.maxHp);
    session.Send(&resp, sizeof(resp));

    // Persist to DB
    db.UpdateCharacterStats(session.characterId, session.level, session.strength,
                            session.dexterity, session.vitality, session.energy,
                            static_cast<uint16_t>(session.hp), static_cast<uint16_t>(session.maxHp),
                            session.levelUpPoints, session.experience);

    printf("[Handler] Stat alloc: type=%d newVal=%d pts=%d maxHP=%d\n",
           req->statType, resp.newValue, session.levelUpPoints, session.maxHp);
}

void HandlePickup(Session &session, const std::vector<uint8_t> &packet,
                  GameWorld &world, Server &server, Database &db) {
    if (packet.size() < sizeof(PMSG_PICKUP_RECV)) return;
    const auto *pick = reinterpret_cast<const PMSG_PICKUP_RECV *>(packet.data());

    auto *drop = world.FindDrop(pick->dropIndex);

    PMSG_PICKUP_RESULT_SEND result{};
    result.h = MakeC1Header(sizeof(result), 0x2D);
    result.dropIndex = pick->dropIndex;

    if (drop) {
        result.success = 1;
        result.defIndex = drop->defIndex;
        result.quantity = drop->quantity;
        result.itemLevel = drop->itemLevel;
        session.Send(&result, sizeof(result));

        // Add to inventory
        if (drop->defIndex == -1) {
            // Zen pickup
            session.zen += drop->quantity;
            db.UpdateCharacterMoney(session.characterId, session.zen);
            printf("[Handler] Zen pickup: +%d (total=%u)\n", drop->quantity, session.zen);
        } else {
            // Item pickup - find first empty bag slot
            for (int i = 0; i < 64; i++) {
                if (!session.bag[i].occupied) {
                    session.bag[i].defIndex = drop->defIndex;
                    session.bag[i].quantity = drop->quantity;
                    session.bag[i].itemLevel = drop->itemLevel;
                    session.bag[i].occupied = true;
                    db.SaveCharacterInventory(session.characterId, drop->defIndex,
                                               drop->quantity, drop->itemLevel, static_cast<uint8_t>(i));
                    printf("[Handler] Item to bag slot %d: def=%d +%d\n", i, drop->defIndex, drop->itemLevel);
                    break;
                }
            }
        }
        // Send updated inventory to client
        SendInventorySync(session);

        printf("[Handler] Pickup drop %d by fd=%d (def=%d qty=%d +%d)\n",
               pick->dropIndex, session.GetFd(), drop->defIndex, drop->quantity, drop->itemLevel);

        // Remove and broadcast removal
        world.RemoveDrop(pick->dropIndex);
        PMSG_DROP_REMOVE_SEND rmPkt{};
        rmPkt.h = MakeC1Header(sizeof(rmPkt), 0x2E);
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

void Handle(Session &session, const std::vector<uint8_t> &packet,
            Database &db, GameWorld &world, Server &server) {
    if (packet.size() < 3) return;

    uint8_t type = packet[0];
    uint8_t headcode;
    uint8_t subcode = 0;

    if (type == 0xC1 || type == 0xC3) {
        headcode = packet[2];
        if (packet.size() >= 4) subcode = packet[3];
    } else if (type == 0xC2 || type == 0xC4) {
        headcode = packet[3];
        if (packet.size() >= 5) subcode = packet[4];
    } else {
        return;
    }

    switch (headcode) {
    case 0xF1:
        if (subcode == 0x01) HandleLogin(session, packet, db);
        break;
    case 0xF3:
        if (subcode == 0x00) HandleCharListRequest(session, db);
        else if (subcode == 0x03) HandleCharSelect(session, packet, db, world);
        break;
    case 0xD4:
        HandleMove(session, packet, db);
        break;
    case 0x26:
        HandleCharSave(session, packet, db);
        break;
    case 0x27:
        HandleEquip(session, packet, db);
        break;
    case 0x28:
        HandleAttack(session, packet, world, server);
        break;
    case 0x2C:
        HandlePickup(session, packet, world, server, db);
        break;
    case 0x37:
        HandleStatAlloc(session, packet, db);
        break;
    case 0xD7:
        HandlePrecisePosition(session, packet);
        break;
    default:
        break;
    }
}

} // namespace PacketHandler
