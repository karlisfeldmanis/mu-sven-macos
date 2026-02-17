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
        CREATE TABLE IF NOT EXISTS monster_spawns (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            type INTEGER NOT NULL,
            map_id INTEGER NOT NULL DEFAULT 0,
            pos_x INTEGER NOT NULL,
            pos_y INTEGER NOT NULL,
            direction INTEGER NOT NULL DEFAULT 2
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
            level_up_points INTEGER DEFAULT 0,
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

    // Inventory bag table
    const char *invSql = R"(
        CREATE TABLE IF NOT EXISTS character_inventory (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            character_id INTEGER NOT NULL,
            slot INTEGER NOT NULL,
            def_index INTEGER NOT NULL,
            quantity INTEGER NOT NULL DEFAULT 1,
            item_level INTEGER NOT NULL DEFAULT 0,
            UNIQUE(character_id, slot),
            FOREIGN KEY (character_id) REFERENCES characters(id)
        );
    )";
    char *invErr = nullptr;
    if (sqlite3_exec(m_db, invSql, nullptr, nullptr, &invErr) != SQLITE_OK) {
        printf("[DB] character_inventory create error: %s\n", invErr);
        sqlite3_free(invErr);
    }

    // Migration: add level_up_points column to existing databases
    sqlite3_exec(m_db, "ALTER TABLE characters ADD COLUMN level_up_points INTEGER DEFAULT 0",
                 nullptr, nullptr, nullptr); // Silently ignores if column exists

    // Migration: add defense column to item_definitions
    sqlite3_exec(m_db, "ALTER TABLE item_definitions ADD COLUMN defense INTEGER DEFAULT 0",
                 nullptr, nullptr, nullptr); // Silently ignores if column exists
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

    // DK starting stats from DefaultClassInfo.txt (Class=1):
    // STR=28, DEX=20, VIT=25, ENE=10, BaseHP=110
    const char *sql = R"(
        INSERT INTO accounts (username, password) VALUES ('test', 'test');
        INSERT INTO characters (account_id, slot, name, class, level, strength, dexterity, vitality, energy, life, max_life, mana, max_mana, level_up_points)
            VALUES (1, 0, 'TestDK', 16, 1, 28, 20, 25, 10, 110, 110, 30, 30, 0);
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
                      "strength, dexterity, vitality, energy, life, max_life, mana, max_mana, money, experience, "
                      "level_up_points FROM characters WHERE name=?";
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
        c.levelUpPoints = static_cast<uint16_t>(sqlite3_column_int(stmt, 20));
    }
    sqlite3_finalize(stmt);
    return c;
}

void Database::UpdateCharacterStats(int charId, uint16_t level, uint16_t strength,
                                    uint16_t dexterity, uint16_t vitality, uint16_t energy,
                                    uint16_t life, uint16_t maxLife, uint16_t levelUpPoints,
                                    uint64_t experience) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "UPDATE characters SET level=?, strength=?, dexterity=?, vitality=?, energy=?, "
                      "life=?, max_life=?, level_up_points=?, experience=? WHERE id=?";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, level);
    sqlite3_bind_int(stmt, 2, strength);
    sqlite3_bind_int(stmt, 3, dexterity);
    sqlite3_bind_int(stmt, 4, vitality);
    sqlite3_bind_int(stmt, 5, energy);
    sqlite3_bind_int(stmt, 6, life);
    sqlite3_bind_int(stmt, 7, maxLife);
    sqlite3_bind_int(stmt, 8, levelUpPoints);
    sqlite3_bind_int64(stmt, 9, static_cast<int64_t>(experience));
    sqlite3_bind_int(stmt, 10, charId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    printf("[DB] Saved character %d: Lv%d STR=%d DEX=%d VIT=%d ENE=%d HP=%d/%d XP=%llu pts=%d\n",
           charId, level, strength, dexterity, vitality, energy, life, maxLife,
           (unsigned long long)experience, levelUpPoints);
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

        // Migration: seed shields and armor if only swords exist
        sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM item_definitions WHERE category>=6", -1, &stmt, nullptr);
        int shieldCount = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW)
            shieldCount = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);

        if (shieldCount == 0) {
            // Add shields and armor (INSERT OR IGNORE to be safe)
            const char *sql2 = R"(
                INSERT OR IGNORE INTO item_definitions (category, item_index, name, model_file, level_req, damage_min, damage_max, defense, attack_speed, two_handed) VALUES
                    (6, 0, 'Small Shield',    'Shield01.bmd', 0,  0, 0,  3, 0, 0),
                    (6, 1, 'Horn Shield',     'Shield02.bmd', 12, 0, 0,  5, 0, 0),
                    (6, 2, 'Kite Shield',     'Shield03.bmd', 24, 0, 0,  8, 0, 0),
                    (8, 0, 'Leather Armor',   '',             0,  0, 0,  5, 0, 0),
                    (8, 1, 'Bronze Armor',    '',             18, 0, 0, 12, 0, 0),
                    (11, 0, 'Leather Boots',  '',             0,  0, 0,  2, 0, 0),
                    (11, 1, 'Bronze Boots',   '',             18, 0, 0,  5, 0, 0);
            )";
            char *err2 = nullptr;
            if (sqlite3_exec(m_db, sql2, nullptr, nullptr, &err2) != SQLITE_OK) {
                printf("[DB] SeedItemDefinitions migration error: %s\n", err2);
                sqlite3_free(err2);
            } else {
                printf("[DB] Migrated: added shield/armor item definitions\n");
            }
        }
        return;
    }

    // Seed all item definitions (swords + shields + armor)
    const char *sql = R"(
        INSERT INTO item_definitions (category, item_index, name, model_file, level_req, damage_min, damage_max, defense, attack_speed, two_handed) VALUES
            (0, 0, 'Short Sword',       'Sword01.bmd', 0,  1,  6,  0, 30, 0),
            (0, 1, 'Kris',              'Sword02.bmd', 6,  4, 11,  0, 30, 0),
            (0, 2, 'Rapier',            'Sword03.bmd', 16, 8, 15,  0, 30, 0),
            (0, 3, 'Katache',           'Sword04.bmd', 26, 16, 26, 0, 25, 0),
            (0, 4, 'Sword of Assassin', 'Sword05.bmd', 36, 19, 28, 0, 30, 0),
            (0, 5, 'Blade',             'Sword06.bmd', 46, 22, 32, 0, 35, 0),
            (0, 6, 'Gladius',           'Sword07.bmd', 54, 24, 36, 0, 30, 0),
            (0, 7, 'Falchion',          'Sword08.bmd', 60, 28, 40, 0, 25, 0),
            (0, 8, 'Serpent Sword',     'Sword09.bmd', 68, 30, 44, 0, 30, 0),
            (0, 9, 'Legendary Sword',   'Sword10.bmd', 75, 34, 48, 0, 30, 0),
            (6, 0, 'Small Shield',      'Shield01.bmd', 0,  0,  0, 3,  0, 0),
            (6, 1, 'Horn Shield',       'Shield02.bmd', 12, 0,  0, 5,  0, 0),
            (6, 2, 'Kite Shield',       'Shield03.bmd', 24, 0,  0, 8,  0, 0),
            (8, 0, 'Leather Armor',     '',             0,  0,  0, 5,  0, 0),
            (8, 1, 'Bronze Armor',      '',             18, 0,  0, 12, 0, 0),
            (11, 0, 'Leather Boots',    '',             0,  0,  0, 2,  0, 0),
            (11, 1, 'Bronze Boots',     '',             18, 0,  0, 5,  0, 0);
    )";
    char *err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        printf("[DB] SeedItemDefinitions error: %s\n", err);
        sqlite3_free(err);
    } else {
        printf("[DB] Seeded 17 item definitions (swords + shields + armor)\n");
    }
}

ItemDefinition Database::GetItemDefinition(uint8_t category, uint8_t itemIndex) {
    ItemDefinition item;
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, category, item_index, name, model_file, level_req, "
                      "damage_min, damage_max, defense, attack_speed, two_handed "
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
        item.defense = static_cast<uint16_t>(sqlite3_column_int(stmt, 8));
        item.attackSpeed = static_cast<uint8_t>(sqlite3_column_int(stmt, 9));
        item.twoHanded = static_cast<uint8_t>(sqlite3_column_int(stmt, 10));
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

void Database::SeedMonsterSpawns() {
    // Check if already seeded
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM monster_spawns WHERE map_id=0", -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    if (count > 0) {
        printf("[DB] Monster spawns already seeded (%d entries)\n", count);
        return;
    }

    // Lorencia monster spawns from MonsterSetBase.txt (Main 5.2 / 0.97k reference)
    // Coordinates are grid (X,Y) from the reference spawn data
    const char *sql = R"(
        INSERT INTO monster_spawns (type, map_id, pos_x, pos_y, direction) VALUES
            -- Bull Fighter (type 0): Level 6 — northwest fields (MonsterSetBase: ~152,53)
            (0, 0, 152, 53, 2), (0, 0, 154, 51, 4), (0, 0, 155, 54, 6),
            (0, 0, 148, 50, 1), (0, 0, 156, 48, 3), (0, 0, 150, 56, 5),
            (0, 0, 160, 55, 0), (0, 0, 145, 45, 7), (0, 0, 165, 60, 2),
            (0, 0, 158, 42, 4), (0, 0, 140, 52, 6), (0, 0, 170, 50, 1),
            -- Hound (type 1): Level 9 — west fields (MonsterSetBase: ~143,76 + west region 8-94)
            (1, 0, 143, 76, 2), (1, 0, 145, 75, 5),
            (1, 0, 140, 80, 1), (1, 0, 138, 72, 3),
            (1, 0, 35, 55, 4), (1, 0, 50, 85, 6), (1, 0, 65, 115, 0),
            (1, 0, 30, 155, 7), (1, 0, 55, 195, 2), (1, 0, 80, 35, 1),
            (1, 0, 45, 130, 3), (1, 0, 70, 200, 5),
            -- Budge Dragon (type 2): Level 4 — east/southeast (MonsterSetBase: ~191,142 + 180-226,90-244)
            (2, 0, 191, 142, 2), (2, 0, 193, 143, 4), (2, 0, 190, 140, 6), (2, 0, 194, 141, 0),
            (2, 0, 198, 126, 3), (2, 0, 200, 130, 5), (2, 0, 195, 135, 1),
            (2, 0, 206, 151, 2), (2, 0, 208, 153, 4), (2, 0, 204, 149, 6),
            (2, 0, 224, 168, 0), (2, 0, 226, 170, 3),
            -- Spider (type 3): Level 2 — near town east (MonsterSetBase: ~187,119 + 181,128)
            (3, 0, 187, 119, 2), (3, 0, 188, 117, 4), (3, 0, 186, 120, 6), (3, 0, 189, 118, 1),
            (3, 0, 181, 128, 3), (3, 0, 183, 130, 5), (3, 0, 180, 126, 0), (3, 0, 182, 129, 7),
            (3, 0, 179, 154, 2), (3, 0, 175, 155, 4),
            (3, 0, 185, 135, 1), (3, 0, 190, 125, 3),
            (3, 0, 195, 115, 5), (3, 0, 192, 122, 6);
    )";
    char *err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        printf("[DB] SeedMonsterSpawns error: %s\n", err);
        sqlite3_free(err);
    } else {
        printf("[DB] Seeded Lorencia monster spawns (Bull Fighter, Hound, Budge Dragon, Spider)\n");
    }
}

std::vector<MonsterSpawnData> Database::GetMonsterSpawns(uint8_t mapId) {
    std::vector<MonsterSpawnData> monsters;
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT id, type, map_id, pos_x, pos_y, direction "
                      "FROM monster_spawns WHERE map_id=? ORDER BY id";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return monsters;

    sqlite3_bind_int(stmt, 1, mapId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MonsterSpawnData m;
        m.id = sqlite3_column_int(stmt, 0);
        m.type = static_cast<uint16_t>(sqlite3_column_int(stmt, 1));
        m.mapId = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
        m.posX = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
        m.posY = static_cast<uint8_t>(sqlite3_column_int(stmt, 4));
        m.direction = static_cast<uint8_t>(sqlite3_column_int(stmt, 5));
        monsters.push_back(m);
    }
    sqlite3_finalize(stmt);
    return monsters;
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

    // Give DK a Short Sword (category=0 Sword, index=0) in right hand (slot 0)
    const char *sql = "INSERT INTO character_equipment (character_id, slot, item_category, item_index, item_level) "
                      "VALUES (?, 0, 0, 0, 0)";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, characterId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Give DK a Small Shield (category=6 Shield, index=0) in left hand (slot 1)
    const char *sql2 = "INSERT INTO character_equipment (character_id, slot, item_category, item_index, item_level) "
                       "VALUES (?, 1, 6, 0, 0)";
    if (sqlite3_prepare_v2(m_db, sql2, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, characterId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    printf("[DB] Equipped Short Sword + Small Shield on character %d\n", characterId);
}

void Database::UpdateEquipment(int characterId, uint8_t slot, uint8_t category,
                               uint8_t itemIndex, uint8_t itemLevel) {
    sqlite3_stmt *stmt = nullptr;
    // UPSERT: insert or replace on (character_id, slot) unique constraint
    const char *sql = "INSERT INTO character_equipment (character_id, slot, item_category, item_index, item_level) "
                      "VALUES (?, ?, ?, ?, ?) "
                      "ON CONFLICT(character_id, slot) DO UPDATE SET "
                      "item_category=excluded.item_category, item_index=excluded.item_index, "
                      "item_level=excluded.item_level";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, characterId);
    sqlite3_bind_int(stmt, 2, slot);
    sqlite3_bind_int(stmt, 3, category);
    sqlite3_bind_int(stmt, 4, itemIndex);
    sqlite3_bind_int(stmt, 5, itemLevel);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    printf("[DB] Equipment updated: char=%d slot=%d cat=%d idx=%d +%d\n",
           characterId, slot, category, itemIndex, itemLevel);
}

std::vector<Database::InventorySlotData> Database::GetCharacterInventory(int characterId) {
    std::vector<InventorySlotData> items;
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT slot, def_index, quantity, item_level "
                      "FROM character_inventory WHERE character_id=? ORDER BY slot";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return items;
    sqlite3_bind_int(stmt, 1, characterId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        InventorySlotData d;
        d.slot = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
        d.defIndex = static_cast<int8_t>(sqlite3_column_int(stmt, 1));
        d.quantity = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
        d.itemLevel = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
        items.push_back(d);
    }
    sqlite3_finalize(stmt);
    return items;
}

void Database::SaveCharacterInventory(int characterId, int8_t defIndex, uint8_t quantity,
                                       uint8_t itemLevel, uint8_t slot) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "INSERT INTO character_inventory (character_id, slot, def_index, quantity, item_level) "
                      "VALUES (?, ?, ?, ?, ?) "
                      "ON CONFLICT(character_id, slot) DO UPDATE SET "
                      "def_index=excluded.def_index, quantity=excluded.quantity, item_level=excluded.item_level";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, characterId);
    sqlite3_bind_int(stmt, 2, slot);
    sqlite3_bind_int(stmt, 3, defIndex);
    sqlite3_bind_int(stmt, 4, quantity);
    sqlite3_bind_int(stmt, 5, itemLevel);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::ClearCharacterInventory(int characterId) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "DELETE FROM character_inventory WHERE character_id=?";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, characterId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::UpdateCharacterMoney(int characterId, uint32_t money) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "UPDATE characters SET money=? WHERE id=?";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, static_cast<int>(money));
    sqlite3_bind_int(stmt, 2, characterId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

