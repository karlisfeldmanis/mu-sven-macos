#include "Database.hpp"
#include <cstdio>

bool Database::Open(const std::string &dbPath) {
    if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK) {
        printf("[DB] Failed to open: %s\n", sqlite3_errmsg(m_db));
        return false;
    }
    CreateTables();
    printf("[DB] Opened %s\n", dbPath.c_str());
    return true;
}

void Database::Close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

void Database::CreateTables() {
    const char *sql = R"(
        CREATE TABLE IF NOT EXISTS accounts (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password TEXT NOT NULL,
            blocked INTEGER DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS npc_spawns (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            type INTEGER NOT NULL,
            map_id INTEGER NOT NULL DEFAULT 0,
            pos_x INTEGER NOT NULL,
            pos_y INTEGER NOT NULL,
            direction INTEGER NOT NULL DEFAULT 2,
            name TEXT NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS characters (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            account_id INTEGER NOT NULL,
            slot INTEGER NOT NULL,
            name TEXT UNIQUE NOT NULL,
            class INTEGER DEFAULT 0,
            level INTEGER DEFAULT 1,
            map_id INTEGER DEFAULT 0,
            pos_x INTEGER DEFAULT 130,
            pos_y INTEGER DEFAULT 130,
            direction INTEGER DEFAULT 2,
            strength INTEGER DEFAULT 20,
            dexterity INTEGER DEFAULT 20,
            vitality INTEGER DEFAULT 20,
            energy INTEGER DEFAULT 20,
            life INTEGER DEFAULT 100,
            max_life INTEGER DEFAULT 100,
            mana INTEGER DEFAULT 50,
            max_mana INTEGER DEFAULT 50,
            money INTEGER DEFAULT 0,
            experience INTEGER DEFAULT 0,
            FOREIGN KEY (account_id) REFERENCES accounts(id)
        );
        CREATE TABLE IF NOT EXISTS item_definitions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            category INTEGER NOT NULL,
            item_index INTEGER NOT NULL,
            name TEXT NOT NULL,
            model_file TEXT NOT NULL,
            level_req INTEGER DEFAULT 0,
            damage_min INTEGER DEFAULT 0,
            damage_max INTEGER DEFAULT 0,
            attack_speed INTEGER DEFAULT 0,
            two_handed INTEGER DEFAULT 0,
            UNIQUE(category, item_index)
        );
        CREATE TABLE IF NOT EXISTS character_equipment (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            character_id INTEGER NOT NULL,
            slot INTEGER NOT NULL,
            item_category INTEGER NOT NULL,
            item_index INTEGER NOT NULL,
            item_level INTEGER DEFAULT 0,
            UNIQUE(character_id, slot),
            FOREIGN KEY (character_id) REFERENCES characters(id),
            FOREIGN KEY (item_category, item_index) REFERENCES item_definitions(category, item_index)
        );
    )";
    char *err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        printf("[DB] CreateTables error: %s\n", err);
        sqlite3_free(err);
    }
}

void Database::CreateDefaultAccount() {
    // Check if test account exists
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT id FROM accounts WHERE username='test'", -1, &stmt, nullptr);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return; // Already exists
    }
    sqlite3_finalize(stmt);

    const char *sql = R"(
        INSERT INTO accounts (username, password) VALUES ('test', 'test');
        INSERT INTO characters (account_id, slot, name, class, level, strength, dexterity, vitality, energy, life, max_life, mana, max_mana)
            VALUES (1, 0, 'TestDK', 16, 10, 30, 30, 30, 20, 150, 150, 60, 60);
    )";
    char *err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        printf("[DB] Seed error: %s\n", err);
        sqlite3_free(err);
    } else {
        printf("[DB] Created default account: test/test with character TestDK\n");
    }
}

int Database::ValidateLogin(const std::string &username, const std::string &password) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, blocked FROM accounts WHERE username=? AND password=?";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

    int accountId = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int blocked = sqlite3_column_int(stmt, 1);
        if (!blocked) {
            accountId = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    return accountId;
}

std::vector<CharacterData> Database::GetCharacterList(int accountId) {
    std::vector<CharacterData> chars;
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, slot, name, class, level FROM characters WHERE account_id=? ORDER BY slot";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return chars;

    sqlite3_bind_int(stmt, 1, accountId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CharacterData c;
        c.id = sqlite3_column_int(stmt, 0);
        c.accountId = accountId;
        c.slot = sqlite3_column_int(stmt, 1);
        c.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        c.charClass = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
        c.level = static_cast<uint16_t>(sqlite3_column_int(stmt, 4));
        chars.push_back(c);
    }
    sqlite3_finalize(stmt);
    return chars;
}

CharacterData Database::GetCharacter(const std::string &name) {
    CharacterData c;
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, account_id, slot, name, class, level, map_id, pos_x, pos_y, direction, "
                      "strength, dexterity, vitality, energy, life, max_life, mana, max_mana, money, experience "
                      "FROM characters WHERE name=?";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return c;

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        c.id = sqlite3_column_int(stmt, 0);
        c.accountId = sqlite3_column_int(stmt, 1);
        c.slot = sqlite3_column_int(stmt, 2);
        c.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        c.charClass = static_cast<uint8_t>(sqlite3_column_int(stmt, 4));
        c.level = static_cast<uint16_t>(sqlite3_column_int(stmt, 5));
        c.mapId = static_cast<uint8_t>(sqlite3_column_int(stmt, 6));
        c.posX = static_cast<uint8_t>(sqlite3_column_int(stmt, 7));
        c.posY = static_cast<uint8_t>(sqlite3_column_int(stmt, 8));
        c.direction = static_cast<uint8_t>(sqlite3_column_int(stmt, 9));
        c.strength = static_cast<uint16_t>(sqlite3_column_int(stmt, 10));
        c.dexterity = static_cast<uint16_t>(sqlite3_column_int(stmt, 11));
        c.vitality = static_cast<uint16_t>(sqlite3_column_int(stmt, 12));
        c.energy = static_cast<uint16_t>(sqlite3_column_int(stmt, 13));
        c.life = static_cast<uint16_t>(sqlite3_column_int(stmt, 14));
        c.maxLife = static_cast<uint16_t>(sqlite3_column_int(stmt, 15));
        c.mana = static_cast<uint16_t>(sqlite3_column_int(stmt, 16));
        c.maxMana = static_cast<uint16_t>(sqlite3_column_int(stmt, 17));
        c.money = static_cast<uint32_t>(sqlite3_column_int(stmt, 18));
        c.experience = static_cast<uint64_t>(sqlite3_column_int64(stmt, 19));
    }
    sqlite3_finalize(stmt);
    return c;
}

void Database::UpdatePosition(int charId, uint8_t x, uint8_t y) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "UPDATE characters SET pos_x=?, pos_y=? WHERE id=?";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, x);
    sqlite3_bind_int(stmt, 2, y);
    sqlite3_bind_int(stmt, 3, charId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::SeedNpcSpawns() {
    // Check if NPCs already seeded
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM npc_spawns WHERE map_id=0", -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    if (count > 0) {
        printf("[DB] NPC spawns already seeded (%d entries)\n", count);
        return;
    }

    // Lorencia (map 0) NPCs from MonsterSetBase.txt (0.97d scope)
    const char *sql = R"(
        INSERT INTO npc_spawns (type, map_id, pos_x, pos_y, direction, name) VALUES
            (253, 0, 127, 86,  2, 'Potion Girl Amy'),
            (250, 0, 183, 137, 2, 'Weapon Merchant'),
            (251, 0, 116, 141, 3, 'Hanzo the Blacksmith'),
            (254, 0, 118, 113, 3, 'Pasi the Mage'),
            (255, 0, 123, 135, 1, 'Lumen the Barmaid'),
            (240, 0, 146, 110, 3, 'Safety Guardian'),
            (240, 0, 147, 145, 1, 'Safety Guardian');
    )";
    char *err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        printf("[DB] SeedNpcSpawns error: %s\n", err);
        sqlite3_free(err);
    } else {
        printf("[DB] Seeded 7 Lorencia NPC spawns\n");
    }
}

std::vector<NpcSpawnData> Database::GetNpcSpawns(uint8_t mapId) {
    std::vector<NpcSpawnData> npcs;
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, type, map_id, pos_x, pos_y, direction, name "
                      "FROM npc_spawns WHERE map_id=? ORDER BY id";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return npcs;

    sqlite3_bind_int(stmt, 1, mapId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        NpcSpawnData n;
        n.id = sqlite3_column_int(stmt, 0);
        n.type = static_cast<uint16_t>(sqlite3_column_int(stmt, 1));
        n.mapId = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
        n.posX = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
        n.posY = static_cast<uint8_t>(sqlite3_column_int(stmt, 4));
        n.direction = static_cast<uint8_t>(sqlite3_column_int(stmt, 5));
        n.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
        npcs.push_back(n);
    }
    sqlite3_finalize(stmt);
    return npcs;
}

void Database::SeedItemDefinitions() {
    // Check if already seeded
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM item_definitions", -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    if (count > 0) {
        printf("[DB] Item definitions already seeded (%d entries)\n", count);
        return;
    }

    // Seed starter swords from original MU (0.97d scope)
    // Category 0 = Sword, item_index maps to Sword0X.bmd
    const char *sql = R"(
        INSERT INTO item_definitions (category, item_index, name, model_file, level_req, damage_min, damage_max, attack_speed, two_handed) VALUES
            (0, 0, 'Short Sword',    'Sword01.bmd', 0,  1,  6,  30, 0),
            (0, 1, 'Kris',           'Sword02.bmd', 6,  4, 11,  30, 0),
            (0, 2, 'Rapier',         'Sword03.bmd', 16, 8, 15,  30, 0),
            (0, 3, 'Katache',        'Sword04.bmd', 26, 16, 26, 25, 0),
            (0, 4, 'Sword of Assassin', 'Sword05.bmd', 36, 19, 28, 30, 0),
            (0, 5, 'Blade',          'Sword06.bmd', 46, 22, 32, 35, 0),
            (0, 6, 'Gladius',        'Sword07.bmd', 54, 24, 36, 30, 0),
            (0, 7, 'Falchion',       'Sword08.bmd', 60, 28, 40, 25, 0),
            (0, 8, 'Serpent Sword',  'Sword09.bmd', 68, 30, 44, 30, 0),
            (0, 9, 'Legendary Sword', 'Sword10.bmd', 75, 34, 48, 30, 0);
    )";
    char *err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        printf("[DB] SeedItemDefinitions error: %s\n", err);
        sqlite3_free(err);
    } else {
        printf("[DB] Seeded 10 sword item definitions\n");
    }
}

ItemDefinition Database::GetItemDefinition(uint8_t category, uint8_t itemIndex) {
    ItemDefinition item;
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, category, item_index, name, model_file, level_req, "
                      "damage_min, damage_max, attack_speed, two_handed "
                      "FROM item_definitions WHERE category=? AND item_index=?";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return item;

    sqlite3_bind_int(stmt, 1, category);
    sqlite3_bind_int(stmt, 2, itemIndex);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        item.id = sqlite3_column_int(stmt, 0);
        item.category = static_cast<uint8_t>(sqlite3_column_int(stmt, 1));
        item.itemIndex = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
        item.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        item.modelFile = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        item.level = static_cast<uint16_t>(sqlite3_column_int(stmt, 5));
        item.damageMin = static_cast<uint16_t>(sqlite3_column_int(stmt, 6));
        item.damageMax = static_cast<uint16_t>(sqlite3_column_int(stmt, 7));
        item.attackSpeed = static_cast<uint8_t>(sqlite3_column_int(stmt, 8));
        item.twoHanded = static_cast<uint8_t>(sqlite3_column_int(stmt, 9));
    }
    sqlite3_finalize(stmt);
    return item;
}

std::vector<EquipmentSlot> Database::GetCharacterEquipment(int characterId) {
    std::vector<EquipmentSlot> equip;
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT slot, item_category, item_index, item_level "
                      "FROM character_equipment WHERE character_id=? ORDER BY slot";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return equip;

    sqlite3_bind_int(stmt, 1, characterId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EquipmentSlot e;
        e.slot = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
        e.category = static_cast<uint8_t>(sqlite3_column_int(stmt, 1));
        e.itemIndex = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
        e.itemLevel = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
        equip.push_back(e);
    }
    sqlite3_finalize(stmt);
    return equip;
}

void Database::SeedDefaultEquipment(int characterId) {
    // Check if character already has equipment
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM character_equipment WHERE character_id=?",
                       -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, characterId);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    if (count > 0) return;

    // Give DK a Short Sword (category=0 Sword, index=0 Sword01) in right hand (slot 0)
    const char *sql = "INSERT INTO character_equipment (character_id, slot, item_category, item_index, item_level) "
                      "VALUES (?, 0, 0, 0, 0)";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, characterId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    printf("[DB] Equipped Short Sword on character %d (right hand)\n", characterId);
}

