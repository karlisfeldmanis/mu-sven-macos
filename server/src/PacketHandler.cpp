#include "PacketHandler.hpp"
#include "PacketDefs.hpp"
#include <cstdio>
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

void HandleMove(Session &session, const std::vector<uint8_t> &packet, Database &db) {
    if (packet.size() < sizeof(PMSG_MOVE_RECV)) return;
    const auto *move = reinterpret_cast<const PMSG_MOVE_RECV *>(packet.data());
    printf("[Handler] Move from fd=%d to (%d, %d)\n", session.GetFd(), move->x, move->y);

    if (session.characterId > 0) {
        db.UpdatePosition(session.characterId, move->x, move->y);
    }
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

    // Send NPC viewport
    SendNpcViewport(session, world);
    printf("[Handler] Character '%s' entered Lorencia at (%d,%d)\n",
           name, c.posX, c.posY);
}

void Handle(Session &session, const std::vector<uint8_t> &packet,
            Database &db, GameWorld &world) {
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

    printf("[Handler] Packet: type=0x%02X head=0x%02X sub=0x%02X size=%zu from fd=%d\n",
           type, headcode, subcode, packet.size(), session.GetFd());

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
    default:
        printf("[Handler] Unhandled headcode 0x%02X\n", headcode);
        break;
    }
}

} // namespace PacketHandler
