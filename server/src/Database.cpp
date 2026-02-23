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
            strength INTEGER DEFAULT 28,
            dexterity INTEGER DEFAULT 20,
            vitality INTEGER DEFAULT 25,
            energy INTEGER DEFAULT 10,
            life INTEGER DEFAULT 110,
            max_life INTEGER DEFAULT 110,
            mana INTEGER DEFAULT 20,
            max_mana INTEGER DEFAULT 20,
            money INTEGER DEFAULT 0,
            experience INTEGER DEFAULT 0,
            level_up_points INTEGER DEFAULT 0,
            skill_bar BLOB,
            potion_bar BLOB,
            rmc_skill_id INTEGER DEFAULT -1,
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
            defense INTEGER DEFAULT 0,
            width INTEGER DEFAULT 1,
            height INTEGER DEFAULT 1,
            req_str INTEGER DEFAULT 0,
            req_dex INTEGER DEFAULT 0,
            req_vit INTEGER DEFAULT 0,
            req_ene INTEGER DEFAULT 0,
            class_flags INTEGER DEFAULT 4294967295,
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
  sqlite3_exec(
      m_db,
      "ALTER TABLE characters ADD COLUMN level_up_points INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr); // Silently ignores if column exists

  // Migration: add defense column to item_definitions
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN defense INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(m_db, "ALTER TABLE characters ADD COLUMN ag INTEGER DEFAULT 50",
               nullptr, nullptr, nullptr);
  sqlite3_exec(m_db,
               "ALTER TABLE characters ADD COLUMN max_ag INTEGER DEFAULT 50",
               nullptr, nullptr, nullptr);
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN width INTEGER DEFAULT 1",
      nullptr, nullptr, nullptr);
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN height INTEGER DEFAULT 1",
      nullptr, nullptr, nullptr);
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN req_str INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN req_dex INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN req_vit INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(
      m_db, "ALTER TABLE item_definitions ADD COLUMN req_ene INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);
  sqlite3_exec(m_db,
               "ALTER TABLE item_definitions ADD COLUMN class_flags INTEGER "
               "DEFAULT 4294967295",
               nullptr, nullptr, nullptr);

  // Migration: add quick_slot_def_index to characters (default -1)
  sqlite3_exec(m_db,
               "ALTER TABLE characters ADD COLUMN quick_slot_def_index INTEGER "
               "DEFAULT -1",
               nullptr, nullptr, nullptr);

  // Migration: add buy_price to item_definitions (default 0)
  sqlite3_exec(
      m_db,
      "ALTER TABLE item_definitions ADD COLUMN buy_price INTEGER DEFAULT 0",
      nullptr, nullptr, nullptr);

  // Migration: add rmc_skill_id to characters (default -1)
  sqlite3_exec(m_db,
               "ALTER TABLE characters ADD COLUMN rmc_skill_id INTEGER "
               "DEFAULT -1",
               nullptr, nullptr, nullptr);
  sqlite3_exec(m_db, "ALTER TABLE characters ADD COLUMN skill_bar BLOB",
               nullptr, nullptr, nullptr);
  sqlite3_exec(m_db, "ALTER TABLE characters ADD COLUMN potion_bar BLOB",
               nullptr, nullptr, nullptr);

  // Character skills table (learned skills)
  const char *skillSql = R"(
        CREATE TABLE IF NOT EXISTS character_skills (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            character_id INTEGER NOT NULL,
            skill_id INTEGER NOT NULL,
            UNIQUE(character_id, skill_id),
            FOREIGN KEY (character_id) REFERENCES characters(id)
        );
    )";
  char *skillErr = nullptr;
  if (sqlite3_exec(m_db, skillSql, nullptr, nullptr, &skillErr) != SQLITE_OK) {
    printf("[DB] character_skills create error: %s\n", skillErr);
    sqlite3_free(skillErr);
  }
}

void Database::CreateDefaultAccount() {
  // Check if test account exists
  int accountId = 0;
  sqlite3_stmt *stmt = nullptr;
  sqlite3_prepare_v2(m_db, "SELECT id FROM accounts WHERE username='test'", -1,
                     &stmt, nullptr);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    accountId = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);

  if (accountId == 0) {
    sqlite3_exec(
        m_db,
        "INSERT INTO accounts (username, password) VALUES ('test', 'test')",
        nullptr, nullptr, nullptr);
    accountId = static_cast<int>(sqlite3_last_insert_rowid(m_db));
    printf("[DB] Created test account (ID: %d)\n", accountId);
  }

  // Only seed default characters if account has none
  sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM characters WHERE account_id=?",
                     -1, &stmt, nullptr);
  sqlite3_bind_int(stmt, 1, accountId);
  int charCount = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    charCount = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  if (charCount == 0) {
    char sql[512];
    snprintf(
        sql, sizeof(sql),
        "INSERT INTO characters (account_id, slot, name, class, level, "
        "strength, dexterity, vitality, energy, life, max_life, mana, "
        "max_mana, money, level_up_points, skill_bar, potion_bar) "
        "VALUES (%d, 0, 'RealPlayer', 16, 1, 28, 20, 25, 10, 110, 110, 20, "
        "20, 1000000, 0, NULL, NULL)",
        accountId);
    sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
    printf("[DB] Created RealPlayer character for account %d\n", accountId);
  }
}

int Database::ValidateLogin(const std::string &username,
                            const std::string &password) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, blocked FROM accounts WHERE username=? AND password=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return 0;

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
  const char *sql =
      "SELECT id, account_id, slot, name, class, level, map_id, "
      "pos_x, pos_y, direction, strength, dexterity, vitality, "
      "energy, life, max_life, mana, max_mana, ag, max_ag, money, "
      "experience, level_up_points, skill_bar, potion_bar, "
      "rmc_skill_id FROM characters WHERE account_id=? ORDER BY "
      "slot";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return chars;

  sqlite3_bind_int(stmt, 1, accountId);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    CharacterData c;
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
    c.ag = static_cast<uint16_t>(sqlite3_column_int(stmt, 18));
    c.maxAg = static_cast<uint16_t>(sqlite3_column_int(stmt, 19));
    c.money = static_cast<uint32_t>(sqlite3_column_int(stmt, 20));
    c.experience = static_cast<uint64_t>(sqlite3_column_int64(stmt, 21));
    c.levelUpPoints = static_cast<uint16_t>(sqlite3_column_int(stmt, 22));

    const void *skillBlob = sqlite3_column_blob(stmt, 23);
    if (skillBlob && sqlite3_column_bytes(stmt, 23) == 10) {
      memcpy(c.skillBar, skillBlob, 10);
    } else {
      memset(c.skillBar, -1, 10);
    }
    const void *potionBlob = sqlite3_column_blob(stmt, 24);
    int potionBytes = sqlite3_column_bytes(stmt, 24);
    memset(c.potionBar, -1, 8); // 4 × int16_t, default all empty
    if (potionBlob && potionBytes >= 6) {
      memcpy(c.potionBar, potionBlob, std::min(potionBytes, 8));
    }

    c.rmcSkillId = static_cast<int8_t>(sqlite3_column_int(stmt, 25));
    chars.push_back(c);
  }
  sqlite3_finalize(stmt);
  return chars;
}

CharacterData Database::GetCharacter(const std::string &name) {
  CharacterData c;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, account_id, slot, name, class, level, map_id, "
      "pos_x, pos_y, direction, strength, dexterity, vitality, "
      "energy, life, max_life, mana, max_mana, ag, max_ag, money, "
      "experience, level_up_points, skill_bar, potion_bar, "
      "rmc_skill_id FROM characters WHERE name=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return c;

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
    c.ag = static_cast<uint16_t>(sqlite3_column_int(stmt, 18));
    c.maxAg = static_cast<uint16_t>(sqlite3_column_int(stmt, 19));
    c.money = static_cast<uint32_t>(sqlite3_column_int(stmt, 20));
    c.experience = static_cast<uint64_t>(sqlite3_column_int64(stmt, 21));
    c.levelUpPoints = static_cast<uint16_t>(sqlite3_column_int(stmt, 22));

    const void *skillBlob = sqlite3_column_blob(stmt, 23);
    if (skillBlob && sqlite3_column_bytes(stmt, 23) == 10) {
      memcpy(c.skillBar, skillBlob, 10);
    } else {
      memset(c.skillBar, -1, 10);
    }
    const void *potionBlob = sqlite3_column_blob(stmt, 24);
    int potionBytes = sqlite3_column_bytes(stmt, 24);
    memset(c.potionBar, -1, 8); // 4 × int16_t, default all empty
    if (potionBlob && potionBytes >= 6) {
      memcpy(c.potionBar, potionBlob, std::min(potionBytes, 8));
    }

    c.rmcSkillId = static_cast<int8_t>(sqlite3_column_int(stmt, 25));
  }
  sqlite3_finalize(stmt);
  return c;
}

CharacterData Database::GetCharacterById(int id) {
  CharacterData c;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, account_id, slot, name, class, level, map_id, "
      "pos_x, pos_y, direction, strength, dexterity, vitality, "
      "energy, life, max_life, mana, max_mana, ag, max_ag, money, "
      "experience, level_up_points, skill_bar, potion_bar, "
      "rmc_skill_id FROM characters WHERE id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return c;

  sqlite3_bind_int(stmt, 1, id);
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
    c.ag = static_cast<uint16_t>(sqlite3_column_int(stmt, 18));
    c.maxAg = static_cast<uint16_t>(sqlite3_column_int(stmt, 19));
    c.money = static_cast<uint32_t>(sqlite3_column_int(stmt, 20));
    c.experience = static_cast<uint64_t>(sqlite3_column_int64(stmt, 21));
    c.levelUpPoints = static_cast<uint16_t>(sqlite3_column_int(stmt, 22));

    const void *skillBlob = sqlite3_column_blob(stmt, 23);
    if (skillBlob && sqlite3_column_bytes(stmt, 23) == 10) {
      memcpy(c.skillBar, skillBlob, 10);
    } else {
      memset(c.skillBar, -1, 10);
    }
    const void *potionBlob = sqlite3_column_blob(stmt, 24);
    int potionBytes = sqlite3_column_bytes(stmt, 24);
    memset(c.potionBar, -1, 8); // 4 × int16_t, default all empty
    if (potionBlob && potionBytes >= 6) {
      memcpy(c.potionBar, potionBlob, std::min(potionBytes, 8));
    }

    c.rmcSkillId = static_cast<int8_t>(sqlite3_column_int(stmt, 25));
  }
  sqlite3_finalize(stmt);
  return c;
}

void Database::UpdateCharacterStats(
    int charId, uint16_t level, uint16_t strength, uint16_t dexterity,
    uint16_t vitality, uint16_t energy, uint16_t life, uint16_t maxLife,
    uint16_t mana, uint16_t maxMana, uint16_t ag, uint16_t maxAg,
    uint16_t levelUpPoints, uint64_t experience, const int8_t *skillBar,
    const int16_t *potionBar, int8_t rmcSkillId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "UPDATE characters SET level=?, strength=?, dexterity=?, "
                    "vitality=?, energy=?, life=?, max_life=?, mana=?, "
                    "max_mana=?, ag=?, max_ag=?, level_up_points=?, "
                    "experience=?, skill_bar=?, potion_bar=?, rmc_skill_id=? "
                    "WHERE id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, level);
  sqlite3_bind_int(stmt, 2, strength);
  sqlite3_bind_int(stmt, 3, dexterity);
  sqlite3_bind_int(stmt, 4, vitality);
  sqlite3_bind_int(stmt, 5, energy);
  sqlite3_bind_int(stmt, 6, life);
  sqlite3_bind_int(stmt, 7, maxLife);
  sqlite3_bind_int(stmt, 8, mana);
  sqlite3_bind_int(stmt, 9, maxMana);
  sqlite3_bind_int(stmt, 10, ag);
  sqlite3_bind_int(stmt, 11, maxAg);
  sqlite3_bind_int(stmt, 12, levelUpPoints);
  sqlite3_bind_int64(stmt, 13, experience);
  sqlite3_bind_blob(stmt, 14, skillBar, 10, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 15, potionBar, 8, SQLITE_TRANSIENT); // 4 × int16_t
  sqlite3_bind_int(stmt, 16, rmcSkillId);
  sqlite3_bind_int(stmt, 17, charId);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  printf("[DB] Saved character %d stats: Lv%d STR=%d DEX=%d VIT=%d ENE=%d "
         "HP=%d/%d MP=%d/%d AG=%d/%d XP=%llu pts=%d RMC=%d\n",
         charId, level, strength, dexterity, vitality, energy, life, maxLife,
         mana, maxMana, ag, maxAg, (unsigned long long)experience,
         levelUpPoints, rmcSkillId);
}

void Database::SaveCharacterFull(int charId, uint16_t level, uint16_t strength,
                                 uint16_t dexterity, uint16_t vitality,
                                 uint16_t energy, uint16_t life,
                                 uint16_t maxLife, uint16_t mana,
                                 uint16_t maxMana, uint16_t ag, uint16_t maxAg,
                                 uint16_t levelUpPoints, uint64_t experience,
                                 uint32_t money, uint8_t posX, uint8_t posY,
                                 const int8_t *skillBar,
                                 const int16_t *potionBar, int8_t rmcSkillId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "UPDATE characters SET level=?, strength=?, dexterity=?, vitality=?, "
      "energy=?, life=?, max_life=?, mana=?, max_mana=?, ag=?, max_ag=?, "
      "level_up_points=?, experience=?, money=?, pos_x=?, pos_y=?, "
      "skill_bar=?, potion_bar=?, rmc_skill_id=? WHERE id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, level);
  sqlite3_bind_int(stmt, 2, strength);
  sqlite3_bind_int(stmt, 3, dexterity);
  sqlite3_bind_int(stmt, 4, vitality);
  sqlite3_bind_int(stmt, 5, energy);
  sqlite3_bind_int(stmt, 6, life);
  sqlite3_bind_int(stmt, 7, maxLife);
  sqlite3_bind_int(stmt, 8, mana);
  sqlite3_bind_int(stmt, 9, maxMana);
  sqlite3_bind_int(stmt, 10, ag);
  sqlite3_bind_int(stmt, 11, maxAg);
  sqlite3_bind_int(stmt, 12, levelUpPoints);
  sqlite3_bind_int64(stmt, 13, experience);
  sqlite3_bind_int(stmt, 14, money);
  sqlite3_bind_int(stmt, 15, posX);
  sqlite3_bind_int(stmt, 16, posY);
  sqlite3_bind_blob(stmt, 17, skillBar, 10, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 18, potionBar, 8, SQLITE_TRANSIENT); // 4 × int16_t
  sqlite3_bind_int(stmt, 19, rmcSkillId);
  sqlite3_bind_int(stmt, 20, charId);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::UpdatePosition(int charId, uint8_t x, uint8_t y) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "UPDATE characters SET pos_x=?, pos_y=? WHERE id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, x);
  sqlite3_bind_int(stmt, 2, y);
  sqlite3_bind_int(stmt, 3, charId);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::SeedNpcSpawns() {
  // Check if NPCs already seeded
  sqlite3_stmt *stmt = nullptr;
  sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM npc_spawns WHERE map_id=0", -1,
                     &stmt, nullptr);
  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    count = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if (count > 0) {
    printf("[DB] NPC spawns already seeded (%d entries)\n", count);
    return;
  }

  // Lorencia (map 0) NPCs from MonsterSetBase.txt + OpenMU Lorencia.cs
  // Direction convention: DB stores MonsterSetBase values + 1.
  // Client formula: facing = (dir - 2) * PI/4
  //   dir=2→West(0°), 3→SW(45°), 4→South(90°), 5→SE(135°),
  //   6→East(180°), 7→NE(225°), 8→North(270°), 1→NW(315°)
  const char *sql = R"(
        INSERT INTO npc_spawns (type, map_id, pos_x, pos_y, direction, name) VALUES
            (253, 0, 127, 86,  3, 'Potion Girl Amy'),
            (250, 0, 183, 137, 3, 'Weapon Merchant'),
            (251, 0, 116, 141, 4, 'Hanzo the Blacksmith'),
            (254, 0, 118, 113, 4, 'Pasi the Mage'),
            (255, 0, 123, 135, 2, 'Lumen the Barmaid'),
            (240, 0, 146, 110, 4, 'Safety Guardian'),
            (240, 0, 147, 145, 2, 'Safety Guardian'),
            (249, 0, 131, 88,  4, 'Guard'),
            (249, 0, 173, 125, 2, 'Guard'),
            (249, 0, 94,  125, 2, 'Guard'),
            (249, 0, 94,  130, 2, 'Guard'),
            (249, 0, 131, 148, 8, 'Guard');
    )";
  char *err = nullptr;
  if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] SeedNpcSpawns error: %s\n", err);
    sqlite3_free(err);
  } else {
    printf("[DB] Seeded 12 Lorencia NPC spawns (7 vendors + 5 guards)\n");
  }
}

std::vector<NpcSpawnData> Database::GetNpcSpawns(uint8_t mapId) {
  std::vector<NpcSpawnData> npcs;
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT id, type, map_id, pos_x, pos_y, direction, name "
                    "FROM npc_spawns WHERE map_id=? ORDER BY id";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return npcs;

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
  // Force re-seeding to apply model filename fixes
  sqlite3_exec(m_db, "DELETE FROM item_definitions", nullptr, nullptr, nullptr);

  // Seed all item definitions (MU 0.97d complete database — OpenMU
  // authoritative) Format: (category, item_index, name, model_file, level_req,
  // damage_min, damage_max, defense, attack_speed, two_handed, width, height,
  // req_str, req_dex, req_vit, req_ene, class_flags) class_flags bitmask: 1=DW,
  // 2=DK, 4=Elf, 8=MG
  const char *sql = R"(
        INSERT INTO item_definitions (category, item_index, name, model_file, level_req, damage_min, damage_max, defense, attack_speed, two_handed, width, height, req_str, req_dex, req_vit, req_ene, class_flags) VALUES

            -- Category 0: Swords (OpenMU 0.95d Weapons.cs)
            (0, 0, 'Kris', 'Sword01.bmd', 1, 6, 11, 0, 50, 0, 1, 2, 10, 8, 0, 0, 11),
            (0, 1, 'Short Sword', 'Sword02.bmd', 1, 3, 7, 0, 20, 0, 1, 3, 20, 0, 0, 0, 7),
            (0, 2, 'Rapier', 'Sword03.bmd', 9, 9, 15, 0, 40, 0, 1, 3, 50, 40, 0, 0, 6),
            (0, 3, 'Katana', 'Sword04.bmd', 16, 16, 26, 0, 35, 0, 1, 3, 80, 40, 0, 0, 2),
            (0, 4, 'Sword of Assassin', 'Sword05.bmd', 12, 12, 18, 0, 30, 0, 1, 3, 60, 40, 0, 0, 2),
            (0, 5, 'Blade', 'Sword06.bmd', 36, 36, 47, 0, 30, 0, 1, 3, 80, 50, 0, 0, 7),
            (0, 6, 'Gladius', 'Sword07.bmd', 20, 20, 30, 0, 20, 0, 1, 3, 110, 0, 0, 0, 6),
            (0, 7, 'Falchion', 'Sword08.bmd', 24, 24, 34, 0, 25, 0, 1, 3, 120, 0, 0, 0, 2),
            (0, 8, 'Serpent Sword', 'Sword09.bmd', 30, 30, 40, 0, 20, 0, 1, 3, 130, 0, 0, 0, 2),
            (0, 9, 'Sword of Salamander', 'Sword10.bmd', 32, 32, 46, 0, 30, 1, 2, 3, 103, 0, 0, 0, 2),
            (0, 10, 'Light Saber', 'Sword11.bmd', 40, 47, 61, 0, 25, 1, 2, 4, 80, 60, 0, 0, 6),
            (0, 11, 'Legendary Sword', 'Sword12.bmd', 44, 56, 72, 0, 20, 1, 2, 3, 120, 0, 0, 0, 2),
            (0, 12, 'Heliacal Sword', 'Sword13.bmd', 56, 73, 98, 0, 25, 1, 2, 3, 140, 0, 0, 0, 2),
            (0, 13, 'Double Blade', 'Sword14.bmd', 48, 48, 56, 0, 30, 0, 1, 3, 70, 70, 0, 0, 6),
            (0, 14, 'Lightning Sword', 'Sword15.bmd', 59, 59, 67, 0, 30, 0, 1, 3, 90, 50, 0, 0, 6),
            (0, 15, 'Giant Sword', 'Sword16.bmd', 52, 60, 85, 0, 20, 1, 2, 3, 140, 0, 0, 0, 2),
            (0, 16, 'Sword of Destruction', 'Sword17.bmd', 82, 82, 90, 0, 35, 0, 1, 4, 160, 60, 0, 0, 10),
            (0, 17, 'Dark Breaker', 'Sword18.bmd', 104, 128, 153, 0, 40, 1, 2, 4, 180, 50, 0, 0, 2),
            (0, 18, 'Thunder Blade', 'Sword19.bmd', 105, 140, 168, 0, 40, 1, 2, 3, 180, 50, 0, 0, 8),

            -- Category 1: Axes (OpenMU 0.95d Weapons.cs)
            (1, 0, 'Small Axe', 'Axe01.bmd', 1, 1, 6, 0, 20, 0, 1, 3, 20, 0, 0, 0, 7),
            (1, 1, 'Hand Axe', 'Axe02.bmd', 4, 4, 9, 0, 30, 0, 1, 3, 70, 0, 0, 0, 7),
            (1, 2, 'Double Axe', 'Axe03.bmd', 14, 14, 24, 0, 20, 0, 1, 3, 90, 0, 0, 0, 2),
            (1, 3, 'Tomahawk', 'Axe04.bmd', 18, 18, 28, 0, 30, 0, 1, 3, 100, 0, 0, 0, 2),
            (1, 4, 'Elven Axe', 'Axe05.bmd', 26, 26, 38, 0, 40, 0, 1, 3, 50, 70, 0, 0, 5),
            (1, 5, 'Battle Axe', 'Axe06.bmd', 30, 36, 44, 0, 20, 1, 2, 3, 120, 0, 0, 0, 6),
            (1, 6, 'Nikkea Axe', 'Axe07.bmd', 34, 38, 50, 0, 30, 1, 2, 3, 130, 0, 0, 0, 6),
            (1, 7, 'Larkan Axe', 'Axe08.bmd', 46, 54, 67, 0, 25, 1, 2, 3, 140, 0, 0, 0, 2),
            (1, 8, 'Crescent Axe', 'Axe09.bmd', 54, 69, 89, 0, 30, 1, 2, 3, 100, 40, 0, 0, 3),

            -- Category 2: Maces (OpenMU 0.95d Weapons.cs)
            (2, 0, 'Mace', 'Mace01.bmd', 7, 7, 13, 0, 15, 0, 1, 3, 100, 0, 0, 0, 2),
            (2, 1, 'Morning Star', 'Mace02.bmd', 13, 13, 22, 0, 15, 0, 1, 3, 100, 0, 0, 0, 2),
            (2, 2, 'Flail', 'Mace03.bmd', 22, 22, 32, 0, 15, 0, 1, 3, 80, 50, 0, 0, 2),
            (2, 3, 'Great Hammer', 'Mace04.bmd', 38, 45, 56, 0, 15, 1, 2, 3, 150, 0, 0, 0, 2),
            (2, 4, 'Crystal Morning Star', 'Mace05.bmd', 66, 78, 107, 0, 30, 1, 2, 3, 130, 0, 0, 0, 7),
            (2, 5, 'Crystal Sword', 'Mace06.bmd', 72, 89, 120, 0, 40, 1, 2, 4, 130, 70, 0, 0, 7),
            (2, 6, 'Chaos Dragon Axe', 'Mace07.bmd', 75, 102, 130, 0, 35, 1, 2, 4, 140, 50, 0, 0, 2),

            -- Category 3: Spears (OpenMU 0.95d Weapons.cs)
            (3, 0, 'Light Spear', 'Spear01.bmd', 42, 50, 63, 0, 25, 1, 2, 4, 60, 70, 0, 0, 6),
            (3, 1, 'Spear', 'Spear02.bmd', 23, 30, 41, 0, 30, 1, 2, 4, 70, 50, 0, 0, 6),
            (3, 2, 'Dragon Lance', 'Spear03.bmd', 15, 21, 33, 0, 30, 1, 2, 4, 70, 50, 0, 0, 6),
            (3, 3, 'Giant Trident', 'Spear04.bmd', 29, 35, 43, 0, 25, 1, 2, 4, 90, 30, 0, 0, 6),
            (3, 4, 'Serpent Spear', 'Spear05.bmd', 46, 58, 80, 0, 20, 1, 2, 4, 90, 30, 0, 0, 6),
            (3, 5, 'Double Poleaxe', 'Spear06.bmd', 13, 19, 31, 0, 30, 1, 2, 4, 70, 50, 0, 0, 6),
            (3, 6, 'Halberd', 'Spear07.bmd', 19, 25, 35, 0, 30, 1, 2, 4, 70, 50, 0, 0, 6),
            (3, 7, 'Berdysh', 'Spear08.bmd', 37, 42, 54, 0, 30, 1, 2, 4, 80, 50, 0, 0, 6),
            (3, 8, 'Great Scythe', 'Spear09.bmd', 54, 71, 92, 0, 25, 1, 2, 4, 90, 50, 0, 0, 6),
            (3, 9, 'Bill of Balrog', 'Spear10.bmd', 63, 76, 102, 0, 25, 1, 2, 4, 80, 50, 0, 0, 6),

            -- Category 4: Bows & Crossbows (OpenMU 0.95d Weapons.cs)
            (4, 0, 'Short Bow', 'Bow01.bmd', 2, 3, 5, 0, 30, 1, 2, 3, 20, 80, 0, 0, 4),
            (4, 1, 'Bow', 'Bow02.bmd', 8, 9, 13, 0, 30, 1, 2, 3, 30, 90, 0, 0, 4),
            (4, 2, 'Elven Bow', 'Bow03.bmd', 16, 17, 24, 0, 30, 1, 2, 3, 30, 90, 0, 0, 4),
            (4, 3, 'Battle Bow', 'Bow04.bmd', 26, 28, 37, 0, 30, 1, 2, 3, 30, 90, 0, 0, 4),
            (4, 4, 'Tiger Bow', 'Bow05.bmd', 40, 42, 52, 0, 30, 1, 2, 4, 30, 100, 0, 0, 4),
            (4, 5, 'Silver Bow', 'Bow06.bmd', 56, 59, 71, 0, 40, 1, 2, 4, 30, 100, 0, 0, 4),
            (4, 6, 'Chaos Nature Bow', 'Bow07.bmd', 75, 88, 106, 0, 35, 1, 2, 4, 40, 150, 0, 0, 4),
            (4, 7, 'Bolt', 'Bolt01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 4),
            (4, 8, 'Crossbow', 'CrossBow01.bmd', 4, 5, 8, 0, 40, 0, 2, 2, 20, 90, 0, 0, 4),
            (4, 9, 'Golden Crossbow', 'CrossBow02.bmd', 12, 13, 19, 0, 40, 0, 2, 2, 30, 90, 0, 0, 4),
            (4, 10, 'Arquebus', 'CrossBow03.bmd', 20, 22, 30, 0, 40, 0, 2, 2, 30, 90, 0, 0, 4),
            (4, 11, 'Light Crossbow', 'CrossBow04.bmd', 32, 35, 44, 0, 40, 0, 2, 3, 30, 90, 0, 0, 4),
            (4, 12, 'Serpent Crossbow', 'CrossBow05.bmd', 48, 50, 61, 0, 40, 0, 2, 3, 30, 100, 0, 0, 4),
            (4, 13, 'Bluewing Crossbow', 'CrossBow06.bmd', 68, 68, 82, 0, 40, 0, 2, 3, 40, 110, 0, 0, 4),
            (4, 14, 'Aquagold Crossbow', 'CrossBow07.bmd', 72, 78, 92, 0, 30, 0, 2, 3, 50, 130, 0, 0, 4),
            (4, 15, 'Arrows', 'Arrow01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 4),
            (4, 16, 'Saint Crossbow', 'CrossBow08.bmd', 83, 90, 108, 0, 35, 0, 2, 3, 50, 130, 0, 0, 4),

            -- Category 5: Staves (OpenMU 0.95d Weapons.cs)
            (5, 0, 'Skull Staff', 'Staff01.bmd', 6, 3, 4, 0, 20, 0, 1, 3, 40, 0, 0, 0, 1),
            (5, 1, 'Angelic Staff', 'Staff02.bmd', 18, 10, 12, 0, 25, 0, 2, 3, 50, 0, 0, 0, 1),
            (5, 2, 'Serpent Staff', 'Staff03.bmd', 30, 17, 18, 0, 25, 0, 2, 3, 50, 0, 0, 0, 1),
            (5, 3, 'Thunder Staff', 'Staff04.bmd', 42, 23, 25, 0, 25, 0, 2, 4, 40, 10, 0, 0, 1),
            (5, 4, 'Gorgon Staff', 'Staff05.bmd', 52, 29, 32, 0, 25, 0, 2, 4, 60, 0, 0, 0, 1),
            (5, 5, 'Legendary Staff', 'Staff06.bmd', 59, 29, 31, 0, 25, 0, 1, 4, 50, 0, 0, 0, 1),
            (5, 6, 'Staff of Resurrection', 'Staff07.bmd', 70, 35, 39, 0, 25, 0, 1, 4, 60, 10, 0, 0, 1),
            (5, 7, 'Chaos Lightning Staff', 'Staff08.bmd', 75, 47, 48, 0, 30, 0, 2, 4, 60, 10, 0, 0, 1),
            (5, 8, 'Staff of Destruction', 'Staff09.bmd', 90, 55, 60, 0, 35, 0, 2, 4, 60, 10, 0, 0, 9),

            -- Category 6: Shields (OpenMU v0.75)
            (6, 0, 'Small Shield', 'Shield01.bmd', 3, 0, 0, 3, 0, 0, 2, 2, 70, 0, 0, 0, 15),
            (6, 1, 'Horn Shield', 'Shield02.bmd', 9, 0, 0, 9, 0, 0, 2, 2, 100, 0, 0, 0, 2),
            (6, 2, 'Kite Shield', 'Shield03.bmd', 12, 0, 0, 12, 0, 0, 2, 2, 110, 0, 0, 0, 2),
            (6, 3, 'Elven Shield', 'Shield04.bmd', 21, 0, 0, 21, 0, 0, 2, 2, 30, 100, 0, 0, 4),
            (6, 4, 'Buckler', 'Shield05.bmd', 6, 0, 0, 6, 0, 0, 2, 2, 80, 0, 0, 0, 15),
            (6, 5, 'Dragon Slayer Shield', 'Shield06.bmd', 35, 0, 0, 36, 0, 0, 2, 2, 100, 40, 0, 0, 2),
            (6, 6, 'Skull Shield', 'Shield07.bmd', 15, 0, 0, 15, 0, 0, 2, 2, 110, 0, 0, 0, 15),
            (6, 7, 'Spiked Shield', 'Shield08.bmd', 30, 0, 0, 30, 0, 0, 2, 2, 130, 0, 0, 0, 2),
            (6, 8, 'Tower Shield', 'Shield09.bmd', 40, 0, 0, 40, 0, 0, 2, 2, 130, 0, 0, 0, 11),
            (6, 9, 'Plate Shield', 'Shield10.bmd', 25, 0, 0, 25, 0, 0, 2, 2, 120, 0, 0, 0, 2),
            (6, 10, 'Big Round Shield', 'Shield11.bmd', 18, 0, 0, 18, 0, 0, 2, 2, 120, 0, 0, 0, 2),
            (6, 11, 'Serpent Shield', 'Shield12.bmd', 45, 0, 0, 45, 0, 0, 2, 2, 130, 0, 0, 0, 11),
            (6, 12, 'Bronze Shield', 'Shield13.bmd', 54, 0, 0, 54, 0, 0, 2, 2, 140, 0, 0, 0, 2),
            (6, 13, 'Dragon Shield', 'Shield14.bmd', 60, 0, 0, 60, 0, 0, 2, 2, 120, 40, 0, 0, 2),
            (6, 14, 'Legendary Shield', 'Shield15.bmd', 48, 0, 0, 48, 0, 0, 2, 3, 90, 25, 0, 0, 5),

            -- Category 7-11: Armors (OpenMU v0.75 Armors.cs - defense values corrected)
            -- Helmets (7) - defense from OpenMU CreateArmor(num,slot,w,h,name,dropLvl,DEFENSE,dur,...)
            (7, 0, 'Bronze Helm', 'HelmMale01.bmd', 1, 0, 0, 9, 0, 0, 2, 2, 25, 20, 0, 0, 2),
            (7, 1, 'Dragon Helm', 'HelmMale02.bmd', 57, 0, 0, 24, 0, 0, 2, 2, 120, 30, 0, 0, 2),
            (7, 2, 'Pad Helm', 'HelmClass01.bmd', 1, 0, 0, 4, 0, 0, 2, 2, 0, 0, 0, 20, 1),
            (7, 3, 'Legendary Helm', 'HelmClass02.bmd', 50, 0, 0, 18, 0, 0, 2, 2, 30, 0, 0, 0, 1),
            (7, 4, 'Bone Helm', 'HelmClass03.bmd', 18, 0, 0, 9, 0, 0, 2, 2, 30, 0, 0, 0, 1),
            (7, 5, 'Leather Helm', 'HelmMale06.bmd', 1, 0, 0, 5, 0, 0, 2, 2, 20, 0, 0, 0, 2),
            (7, 6, 'Scale Helm', 'HelmMale07.bmd', 26, 0, 0, 12, 0, 0, 2, 2, 110, 0, 0, 0, 2),
            (7, 7, 'Sphinx Mask', 'HelmClass04.bmd', 32, 0, 0, 13, 0, 0, 2, 2, 30, 0, 0, 0, 1),
            (7, 8, 'Brass Helm', 'HelmMale09.bmd', 36, 0, 0, 17, 0, 0, 2, 2, 100, 30, 0, 0, 2),
            (7, 9, 'Plate Helm', 'HelmMale10.bmd', 46, 0, 0, 20, 0, 0, 2, 2, 130, 0, 0, 0, 2),
            (7, 10, 'Vine Helm', 'HelmClass05.bmd', 6, 0, 0, 4, 0, 0, 2, 2, 30, 60, 0, 0, 4),
            (7, 11, 'Silk Helm', 'HelmClass06.bmd', 1, 0, 0, 8, 0, 0, 2, 2, 0, 0, 0, 20, 4),
            (7, 12, 'Wind Helm', 'HelmClass07.bmd', 28, 0, 0, 12, 0, 0, 2, 2, 30, 80, 0, 0, 4),
            (7, 13, 'Spirit Helm', 'HelmClass08.bmd', 40, 0, 0, 16, 0, 0, 2, 2, 40, 80, 0, 0, 4),
            (7, 14, 'Guardian Helm', 'HelmClass09.bmd', 53, 0, 0, 23, 0, 0, 2, 2, 40, 80, 0, 0, 4),

            -- Armors (8)
            (8, 0, 'Bronze Armor', 'ArmorMale01.bmd', 1, 0, 0, 14, 0, 0, 2, 2, 25, 20, 0, 0, 2),
            (8, 1, 'Dragon Armor', 'ArmorMale02.bmd', 59, 0, 0, 37, 0, 0, 2, 3, 120, 30, 0, 0, 2),
            (8, 2, 'Pad Armor', 'ArmorClass01.bmd', 1, 0, 0, 7, 0, 0, 2, 2, 0, 0, 0, 20, 1),
            (8, 3, 'Legendary Armor', 'ArmorClass02.bmd', 56, 0, 0, 22, 0, 0, 2, 2, 40, 0, 0, 0, 1),
            (8, 4, 'Bone Armor', 'ArmorClass03.bmd', 22, 0, 0, 13, 0, 0, 2, 2, 40, 0, 0, 0, 1),
            (8, 5, 'Leather Armor', 'ArmorMale06.bmd', 1, 0, 0, 10, 0, 0, 2, 3, 20, 0, 0, 0, 2),
            (8, 6, 'Scale Armor', 'ArmorMale07.bmd', 28, 0, 0, 18, 0, 0, 2, 2, 110, 0, 0, 0, 2),
            (8, 7, 'Sphinx Armor', 'ArmorClass04.bmd', 38, 0, 0, 17, 0, 0, 2, 3, 40, 0, 0, 0, 1),
            (8, 8, 'Brass Armor', 'ArmorMale09.bmd', 38, 0, 0, 22, 0, 0, 2, 2, 100, 30, 0, 0, 2),
            (8, 9, 'Plate Armor', 'ArmorMale10.bmd', 48, 0, 0, 30, 0, 0, 2, 2, 130, 0, 0, 0, 2),
            (8, 10, 'Vine Armor', 'ArmorClass05.bmd', 10, 0, 0, 8, 0, 0, 2, 2, 30, 60, 0, 0, 4),
            (8, 11, 'Silk Armor', 'ArmorClass06.bmd', 1, 0, 0, 12, 0, 0, 2, 2, 0, 0, 0, 20, 4),
            (8, 12, 'Wind Armor', 'ArmorClass07.bmd', 32, 0, 0, 16, 0, 0, 2, 2, 30, 80, 0, 0, 4),
            (8, 13, 'Spirit Armor', 'ArmorClass08.bmd', 44, 0, 0, 21, 0, 0, 2, 2, 40, 80, 0, 0, 4),
            (8, 14, 'Guardian Armor', 'ArmorClass09.bmd', 57, 0, 0, 29, 0, 0, 2, 2, 40, 80, 0, 0, 4),

            -- Pants (9)
            (9, 0, 'Bronze Pants', 'PantMale01.bmd', 1, 0, 0, 10, 0, 0, 2, 2, 25, 20, 0, 0, 2),
            (9, 1, 'Dragon Pants', 'PantMale02.bmd', 55, 0, 0, 26, 0, 0, 2, 2, 120, 30, 0, 0, 2),
            (9, 2, 'Pad Pants', 'PantClass01.bmd', 1, 0, 0, 5, 0, 0, 2, 2, 0, 0, 0, 20, 1),
            (9, 3, 'Legendary Pants', 'PantClass02.bmd', 53, 0, 0, 20, 0, 0, 2, 2, 40, 0, 0, 0, 1),
            (9, 4, 'Bone Pants', 'PantClass03.bmd', 20, 0, 0, 10, 0, 0, 2, 2, 40, 0, 0, 0, 1),
            (9, 5, 'Leather Pants', 'PantMale06.bmd', 1, 0, 0, 7, 0, 0, 2, 2, 20, 0, 0, 0, 2),
            (9, 6, 'Scale Pants', 'PantMale07.bmd', 25, 0, 0, 14, 0, 0, 2, 2, 110, 0, 0, 0, 2),
            (9, 7, 'Sphinx Pants', 'PantClass04.bmd', 34, 0, 0, 15, 0, 0, 2, 2, 40, 0, 0, 0, 1),
            (9, 8, 'Brass Pants', 'PantMale09.bmd', 35, 0, 0, 18, 0, 0, 2, 2, 100, 30, 0, 0, 2),
            (9, 9, 'Plate Pants', 'PantMale10.bmd', 45, 0, 0, 22, 0, 0, 2, 2, 130, 0, 0, 0, 2),
            (9, 10, 'Vine Pants', 'PantClass05.bmd', 8, 0, 0, 6, 0, 0, 2, 2, 30, 60, 0, 0, 4),
            (9, 11, 'Silk Pants', 'PantClass06.bmd', 1, 0, 0, 10, 0, 0, 2, 2, 0, 0, 0, 20, 4),
            (9, 12, 'Wind Pants', 'PantClass07.bmd', 30, 0, 0, 14, 0, 0, 2, 2, 30, 80, 0, 0, 4),
            (9, 13, 'Spirit Pants', 'PantClass08.bmd', 42, 0, 0, 18, 0, 0, 2, 2, 40, 80, 0, 0, 4),
            (9, 14, 'Guardian Pants', 'PantClass09.bmd', 54, 0, 0, 25, 0, 0, 2, 2, 40, 80, 0, 0, 4),

            -- Gloves (10) - defense from OpenMU CreateGloves(num,name,dropLvl,DEFENSE,atkSpd,dur,...)
            (10, 0, 'Bronze Gloves', 'GloveMale01.bmd', 1, 0, 0, 4, 0, 0, 2, 2, 25, 20, 0, 0, 2),
            (10, 1, 'Dragon Gloves', 'GloveMale02.bmd', 52, 0, 0, 14, 0, 0, 2, 2, 120, 30, 0, 0, 2),
            (10, 2, 'Pad Gloves', 'GloveClass01.bmd', 1, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 20, 1),
            (10, 3, 'Legendary Gloves', 'GloveClass02.bmd', 44, 0, 0, 11, 0, 0, 2, 2, 20, 0, 0, 0, 1),
            (10, 4, 'Bone Gloves', 'GloveClass03.bmd', 14, 0, 0, 5, 0, 0, 2, 2, 20, 0, 0, 0, 1),
            (10, 5, 'Leather Gloves', 'GloveMale06.bmd', 1, 0, 0, 2, 0, 0, 2, 2, 20, 0, 0, 0, 2),
            (10, 6, 'Scale Gloves', 'GloveMale07.bmd', 22, 0, 0, 7, 0, 0, 2, 2, 110, 0, 0, 0, 2),
            (10, 7, 'Sphinx Gloves', 'GloveClass04.bmd', 28, 0, 0, 8, 0, 0, 2, 2, 20, 0, 0, 0, 1),
            (10, 8, 'Brass Gloves', 'GloveMale09.bmd', 32, 0, 0, 9, 0, 0, 2, 2, 100, 30, 0, 0, 2),
            (10, 9, 'Plate Gloves', 'GloveMale10.bmd', 42, 0, 0, 12, 0, 0, 2, 2, 130, 0, 0, 0, 2),
            (10, 10, 'Vine Gloves', 'GloveClass05.bmd', 4, 0, 0, 2, 0, 0, 2, 2, 30, 60, 0, 0, 4),
            (10, 11, 'Silk Gloves', 'GloveClass06.bmd', 1, 0, 0, 4, 0, 0, 2, 2, 0, 0, 0, 20, 4),
            (10, 12, 'Wind Gloves', 'GloveClass07.bmd', 26, 0, 0, 6, 0, 0, 2, 2, 30, 80, 0, 0, 4),
            (10, 13, 'Spirit Gloves', 'GloveClass08.bmd', 38, 0, 0, 9, 0, 0, 2, 2, 40, 80, 0, 0, 4),
            (10, 14, 'Guardian Gloves', 'GloveClass09.bmd', 50, 0, 0, 15, 0, 0, 2, 2, 40, 80, 0, 0, 4),

            -- Boots (11) - defense from OpenMU CreateBoots(num,slot,w,h,name,dropLvl,DEFENSE,walkSpd,dur,...)
            (11, 0, 'Bronze Boots', 'BootMale01.bmd', 1, 0, 0, 4, 0, 0, 2, 2, 25, 20, 0, 0, 2),
            (11, 1, 'Dragon Boots', 'BootMale02.bmd', 54, 0, 0, 15, 0, 0, 2, 2, 120, 30, 0, 0, 2),
            (11, 2, 'Pad Boots', 'BootClass01.bmd', 1, 0, 0, 3, 0, 0, 2, 2, 0, 0, 0, 20, 1),
            (11, 3, 'Legendary Boots', 'BootClass02.bmd', 46, 0, 0, 12, 0, 0, 2, 2, 30, 0, 0, 0, 1),
            (11, 4, 'Bone Boots', 'BootClass03.bmd', 16, 0, 0, 6, 0, 0, 2, 2, 30, 0, 0, 0, 1),
            (11, 5, 'Leather Boots', 'BootMale06.bmd', 1, 0, 0, 2, 0, 0, 2, 2, 20, 0, 0, 0, 2),
            (11, 6, 'Scale Boots', 'BootMale07.bmd', 22, 0, 0, 8, 0, 0, 2, 2, 110, 0, 0, 0, 2),
            (11, 7, 'Sphinx Boots', 'BootClass04.bmd', 30, 0, 0, 9, 0, 0, 2, 2, 30, 0, 0, 0, 1),
            (11, 8, 'Brass Boots', 'BootMale09.bmd', 32, 0, 0, 10, 0, 0, 2, 2, 100, 30, 0, 0, 2),
            (11, 9, 'Plate Boots', 'BootMale10.bmd', 42, 0, 0, 12, 0, 0, 2, 2, 130, 0, 0, 0, 2),
            (11, 10, 'Vine Boots', 'BootClass05.bmd', 5, 0, 0, 2, 0, 0, 2, 2, 30, 60, 0, 0, 4),
            (11, 11, 'Silk Boots', 'BootClass06.bmd', 1, 0, 0, 4, 0, 0, 2, 2, 0, 0, 0, 20, 4),
            (11, 12, 'Wind Boots', 'BootClass07.bmd', 27, 0, 0, 7, 0, 0, 2, 2, 30, 80, 0, 0, 4),
            (11, 13, 'Spirit Boots', 'BootClass08.bmd', 40, 0, 0, 10, 0, 0, 2, 2, 40, 80, 0, 0, 4),
            (11, 14, 'Guardian Boots', 'BootClass09.bmd', 52, 0, 0, 16, 0, 0, 2, 2, 40, 80, 0, 0, 4),

            -- Category 12: Wings/Orbs/Jewels (Group 12 — OpenMU 0.95d)
            -- Wings (Level 1 only in 0.95d)
             (12, 0, 'Wings of Elf', 'Wing01.bmd', 180, 0, 0, 10, 0, 0, 3, 2, 0, 0, 0, 0, 4),
             (12, 1, 'Wings of Heaven', 'Wing02.bmd', 180, 0, 0, 10, 0, 0, 3, 2, 0, 0, 0, 0, 1),
             (12, 2, 'Wings of Satan', 'Wing03.bmd', 180, 0, 0, 20, 0, 0, 3, 2, 0, 0, 0, 0, 2),
             (12, 3, 'Wings of Spirits', 'Wing04.bmd', 180, 0, 0, 20, 0, 0, 4, 3, 0, 0, 0, 0, 4),
             (12, 4, 'Wings of Soul', 'Wing05.bmd', 180, 0, 0, 20, 0, 0, 4, 3, 0, 0, 0, 0, 1),
             (12, 5, 'Wings of Dragon', 'Wing06.bmd', 180, 0, 0, 20, 0, 0, 4, 3, 0, 0, 0, 0, 2),
             (12, 6, 'Wings of Darkness', 'Wing07.bmd', 180, 0, 0, 20, 0, 0, 4, 3, 0, 0, 0, 0, 8),
             -- Orbs (Research confirmed: GemXX.bmd)
             (12, 7, 'Orb of Twisting Slash', 'Gem01.bmd', 47, 0, 0, 0, 0, 0, 1, 1, 80, 0, 0, 0, 10),
             (12, 8, 'Orb of Healing', 'Gem02.bmd', 8, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 100, 4),
             (12, 9, 'Orb of Greater Defense', 'Gem03.bmd', 13, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 100, 4),
             (12, 10, 'Orb of Greater Damage', 'Gem04.bmd', 18, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 100, 4),
             (12, 11, 'Orb of Summoning', 'Gem05.bmd', 3, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 4),
             (12, 12, 'Orb of Rageful Blow', 'Gem06.bmd', 78, 0, 0, 0, 0, 0, 1, 1, 170, 0, 0, 0, 2),
             (12, 13, 'Orb of Impale', 'Gem07.bmd', 20, 0, 0, 0, 0, 0, 1, 1, 28, 0, 0, 0, 2),
             (12, 14, 'Orb of Greater Fortitude', 'Gem08.bmd', 60, 0, 0, 0, 0, 0, 1, 1, 120, 0, 0, 0, 2),
             (12, 16, 'Orb of Fire Slash', 'Gem10.bmd', 60, 0, 0, 0, 0, 0, 1, 1, 320, 0, 0, 0, 8),
             (12, 17, 'Orb of Penetration', 'Gem11.bmd', 64, 0, 0, 0, 0, 0, 1, 1, 130, 0, 0, 0, 4),
             (12, 18, 'Orb of Ice Arrow', 'Gem12.bmd', 81, 0, 0, 0, 0, 0, 1, 1, 0, 258, 0, 0, 4),
             (12, 19, 'Orb of Death Stab', 'Gem13.bmd', 72, 0, 0, 0, 0, 0, 1, 1, 160, 0, 0, 0, 2),
             -- Basic DK skill orbs (indices 20-24)
             (12, 20, 'Orb of Falling Slash', 'Gem01.bmd', 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2),
             (12, 21, 'Orb of Lunge', 'Gem01.bmd', 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2),
             (12, 22, 'Orb of Uppercut', 'Gem01.bmd', 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2),
             (12, 23, 'Orb of Cyclone', 'Gem01.bmd', 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2),
             (12, 24, 'Orb of Slash', 'Gem01.bmd', 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2),
            -- Jewel of Chaos
            (12, 15, 'Jewel of Chaos', 'Jewel01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),

            -- Category 13: Pets/Jewelry (OpenMU 0.95d)
            (13, 0, 'Guardian Angel', 'Helper01.bmd', 23, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 1, 'Imp', 'Helper02.bmd', 28, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 2, 'Horn of Uniria', 'Helper03.bmd', 25, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 3, 'Horn of Dinorant', 'Pet04.bmd', 110, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 8, 'Ring of Ice', 'Ring01.bmd', 20, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 9, 'Ring of Poison', 'Ring02.bmd', 17, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 10, 'Transformation Ring', 'Ring01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 12, 'Pendant of Lighting', 'Necklace01.bmd', 21, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 13, 'Pendant of Fire', 'Necklace02.bmd', 13, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),

            -- Category 14: Consumables (Potions & Jewels — OpenMU 0.97d)
            (14, 0, 'Apple', 'Apple.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 1, 'Small Healing Potion', 'Potion01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 2, 'Medium Healing Potion', 'Potion02.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 3, 'Large Healing Potion', 'Potion03.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 4, 'Small Mana Potion', 'Potion04.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 5, 'Medium Mana Potion', 'Potion05.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 6, 'Large Mana Potion', 'Potion06.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 8, 'Antidote', 'Antidote01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 9, 'Ale', 'Potion09.bmd', 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 15),
            (14, 10, 'Town Portal Scroll', 'Scroll01.bmd', 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 15),
            (14, 13, 'Jewel of Bless', 'Jewel02.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 14, 'Jewel of Soul', 'Jewel03.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 16, 'Jewel of Life', 'Jewel04.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 22, 'Jewel of Creation', 'Jewel05.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),

             -- Category 15: Scrolls (Dark Wizard — Research confirmed: BookXX.bmd)
             (15, 0, 'Scroll of Poison', 'Book01.bmd', 30, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 140, 1),
             (15, 1, 'Scroll of Meteorite', 'Book02.bmd', 21, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 104, 1),
             (15, 2, 'Scroll of Lighting', 'Book03.bmd', 13, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 72, 1),
             (15, 3, 'Scroll of Fire Ball', 'Book04.bmd', 5, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 40, 1),
             (15, 4, 'Scroll of Flame', 'Book05.bmd', 35, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 160, 1),
             (15, 5, 'Scroll of Teleport', 'Book06.bmd', 17, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 88, 1),
             (15, 6, 'Scroll of Ice', 'Book07.bmd', 25, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 120, 1),
             (15, 7, 'Scroll of Twister', 'Book08.bmd', 40, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 180, 1),
             (15, 8, 'Scroll of Evil Spirit', 'Book09.bmd', 50, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 220, 1),
             (15, 9, 'Scroll of Hellfire', 'Book10.bmd', 60, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 260, 1),
             (15, 10, 'Scroll of Power Wave', 'Book11.bmd', 9, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 56, 1),
             (15, 11, 'Scroll of Aqua Beam', 'Book12.bmd', 74, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 345, 1),
             (15, 12, 'Scroll of Cometfall', 'Book13.bmd', 80, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 436, 1),
             (15, 13, 'Scroll of Inferno', 'Book14.bmd', 88, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 578, 1),

            -- Additional 0.97d items (Main 5.2 deep dive)

            -- Missing Swords (0)
            (0, 19, 'Sword of Destruction', 'Sword20.bmd', 76, 68, 93, 0, 30, 0, 1, 4, 124, 44, 0, 0, 8),
            (0, 20, 'Spirit Sword', 'Sword21.bmd', 88, 92, 112, 0, 35, 0, 1, 4, 140, 48, 0, 0, 2),
            (0, 21, 'Dark Master Sword', 'Sword22.bmd', 98, 108, 132, 0, 40, 0, 1, 4, 154, 50, 0, 0, 8),

            -- Missing Maces (2)
            (2, 7, 'Battle Scepter', 'Mace08.bmd', 80, 85, 110, 0, 30, 1, 2, 4, 132, 32, 0, 0, 2),
            (2, 8, 'Master Scepter', 'Mace09.bmd', 86, 92, 126, 0, 30, 1, 2, 4, 142, 38, 0, 0, 2),
            (2, 9, 'Great Scepter', 'Mace10.bmd', 92, 105, 140, 0, 35, 1, 2, 4, 152, 42, 0, 0, 2),
            (2, 10, 'Lord Scepter', 'Mace11.bmd', 96, 110, 148, 0, 35, 1, 2, 4, 158, 44, 0, 0, 2),
            (2, 11, 'Great Lord Scepter', 'Mace12.bmd', 100, 118, 156, 0, 35, 1, 2, 4, 164, 48, 0, 0, 2),
            (2, 12, 'Divine Scepter', 'Mace13.bmd', 104, 125, 168, 0, 40, 1, 2, 4, 170, 50, 0, 0, 2),
            (2, 13, 'Saint Scepter', 'Saint.bmd', 96, 106, 144, 0, 35, 0, 1, 3, 72, 18, 0, 0, 1),

            -- Missing Spears (3)
            (3, 10, 'Dragon Spear', 'Spear11.bmd', 92, 112, 140, 0, 35, 1, 2, 4, 170, 60, 0, 0, 2),

            -- Missing Bows (4)
            (4, 17, 'Celestial Bow', 'Bow18.bmd', 92, 127, 155, 0, 35, 1, 2, 4, 54, 198, 0, 0, 4),
            (4, 18, 'Divine CB of Archangel', 'CrossBow17.bmd', 100, 144, 166, 0, 35, 0, 2, 3, 40, 110, 0, 0, 4),

            -- Missing Staffs (5)
            (5, 9, 'Dragon Soul Staff', 'Staff10.bmd', 100, 46, 48, 0, 30, 0, 1, 4, 52, 16, 0, 0, 1),
            (5, 10, 'Staff of Imperial', 'Staff11.bmd', 104, 50, 53, 0, 30, 0, 2, 4, 36, 4, 0, 0, 1),
            (5, 11, 'Divine Staff of Archangel', 'Staff12.bmd', 104, 53, 55, 0, 20, 0, 2, 4, 36, 4, 0, 0, 1),

            -- Missing Shields (6)
            (6, 15, 'Grand Soul Shield', 'Shield16.bmd', 74, 0, 0, 55, 0, 0, 2, 3, 70, 23, 0, 0, 1),
            (6, 16, 'Elemental Shield', 'Shield17.bmd', 78, 0, 0, 58, 0, 0, 2, 3, 50, 110, 0, 0, 4),

            -- Missing Helms (7)
            (7, 15, 'Storm Crow Helm', 'HelmMale11.bmd', 72, 0, 0, 50, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (7, 16, 'Black Dragon Helm', 'HelmMale12.bmd', 82, 0, 0, 55, 0, 0, 2, 2, 170, 60, 0, 0, 2),
            (7, 17, 'Dark Phoenix Helm', 'HelmMale13.bmd', 92, 0, 0, 60, 0, 0, 2, 2, 205, 62, 0, 0, 10),
            (7, 18, 'Grand Soul Helm', 'HelmClass10.bmd', 81, 0, 0, 48, 0, 0, 2, 2, 59, 20, 0, 0, 1),
            (7, 19, 'Divine Helm', 'HelmClass11.bmd', 85, 0, 0, 52, 0, 0, 2, 2, 50, 110, 0, 0, 4),
            (7, 20, 'Thunder Hawk Helm', 'HelmMale14.bmd', 88, 0, 0, 54, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (7, 21, 'Great Dragon Helm', 'HelmMale15.bmd', 104, 0, 0, 66, 0, 0, 2, 2, 200, 58, 0, 0, 10),

            -- Missing Armors (8)
            (8, 15, 'Storm Crow Armor', 'ArmorMale11.bmd', 80, 0, 0, 58, 0, 0, 2, 3, 150, 70, 0, 0, 8),
            (8, 16, 'Black Dragon Armor', 'ArmorMale12.bmd', 90, 0, 0, 63, 0, 0, 2, 3, 170, 60, 0, 0, 2),
            (8, 17, 'Dark Phoenix Armor', 'ArmorMale13.bmd', 100, 0, 0, 70, 0, 0, 2, 3, 214, 65, 0, 0, 10),
            (8, 18, 'Grand Soul Armor', 'ArmorClass10.bmd', 91, 0, 0, 52, 0, 0, 2, 3, 59, 20, 0, 0, 1),
            (8, 19, 'Divine Armor', 'ArmorClass11.bmd', 92, 0, 0, 56, 0, 0, 2, 2, 50, 110, 0, 0, 4),
            (8, 20, 'Thunder Hawk Armor', 'ArmorMale14.bmd', 107, 0, 0, 68, 0, 0, 2, 3, 170, 70, 0, 0, 8),
            (8, 21, 'Great Dragon Armor', 'ArmorMale15.bmd', 126, 0, 0, 80, 0, 0, 2, 3, 200, 58, 0, 0, 10),

            -- Missing Pants (9)
            (9, 15, 'Storm Crow Pants', 'PantMale11.bmd', 74, 0, 0, 50, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (9, 16, 'Black Dragon Pants', 'PantMale12.bmd', 84, 0, 0, 55, 0, 0, 2, 2, 170, 60, 0, 0, 2),
            (9, 17, 'Dark Phoenix Pants', 'PantMale13.bmd', 96, 0, 0, 62, 0, 0, 2, 2, 207, 63, 0, 0, 10),
            (9, 18, 'Grand Soul Pants', 'PantClass10.bmd', 86, 0, 0, 48, 0, 0, 2, 2, 59, 20, 0, 0, 1),
            (9, 19, 'Divine Pants', 'PantClass11.bmd', 88, 0, 0, 52, 0, 0, 2, 2, 50, 110, 0, 0, 4),
            (9, 20, 'Thunder Hawk Pants', 'PantMale14.bmd', 99, 0, 0, 60, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (9, 21, 'Great Dragon Pants', 'PantMale15.bmd', 113, 0, 0, 72, 0, 0, 2, 2, 200, 58, 0, 0, 10),

            -- Missing Gloves (10)
            (10, 15, 'Storm Crow Gloves', 'GloveMale11.bmd', 70, 0, 0, 46, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (10, 16, 'Black Dragon Gloves', 'GloveMale12.bmd', 76, 0, 0, 50, 0, 0, 2, 2, 170, 60, 0, 0, 2),
            (10, 17, 'Dark Phoenix Gloves', 'GloveMale13.bmd', 86, 0, 0, 56, 0, 0, 2, 2, 205, 63, 0, 0, 10),
            (10, 18, 'Grand Soul Gloves', 'GloveClass10.bmd', 70, 0, 0, 44, 0, 0, 2, 2, 49, 10, 0, 0, 1),
            (10, 19, 'Divine Gloves', 'GloveClass11.bmd', 72, 0, 0, 48, 0, 0, 2, 2, 50, 110, 0, 0, 4),
            (10, 20, 'Thunder Hawk Gloves', 'GloveMale14.bmd', 88, 0, 0, 54, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (10, 21, 'Great Dragon Gloves', 'GloveMale15.bmd', 94, 0, 0, 64, 0, 0, 2, 2, 200, 58, 0, 0, 10),

            -- Missing Boots (11)
            (11, 15, 'Storm Crow Boots', 'BootMale11.bmd', 72, 0, 0, 48, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (11, 16, 'Black Dragon Boots', 'BootMale12.bmd', 78, 0, 0, 52, 0, 0, 2, 2, 170, 60, 0, 0, 2),
            (11, 17, 'Dark Phoenix Boots', 'BootMale13.bmd', 93, 0, 0, 58, 0, 0, 2, 2, 198, 60, 0, 0, 10),
            (11, 18, 'Grand Soul Boots', 'BootClass10.bmd', 76, 0, 0, 44, 0, 0, 2, 2, 59, 10, 0, 0, 1),
            (11, 19, 'Divine Boots', 'BootClass11.bmd', 81, 0, 0, 50, 0, 0, 2, 2, 50, 110, 0, 0, 4),
            (11, 20, 'Thunder Hawk Boots', 'BootMale14.bmd', 92, 0, 0, 56, 0, 0, 2, 2, 150, 70, 0, 0, 8),
            (11, 21, 'Great Dragon Boots', 'BootMale15.bmd', 98, 0, 0, 68, 0, 0, 2, 2, 200, 58, 0, 0, 10),

            -- Missing Helpers/Jewelry (13)
            (13, 4, 'Dark Horse Horn', 'DarkHorseHorn.bmd', 110, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 5, 'Spirit Bill', 'SpiritBill.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 7, 'Covenant', 'Covenant.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 11, 'Summon Book', 'SummonBook.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 21, 'Fire Ring', 'FireRing.bmd', 68, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 22, 'Ground Ring', 'GroundRing.bmd', 76, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 23, 'Wind Ring', 'WindRing.bmd', 84, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 24, 'Mana Ring', 'ManaRing.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 25, 'Ice Necklace', 'IceNecklace.bmd', 68, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 26, 'Wind Necklace', 'WindNecklace.bmd', 76, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 27, 'Water Necklace', 'WaterNecklace.bmd', 84, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 28, 'AG Necklace', 'AgNecklace.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (13, 29, 'Chaos Castle Invitation', 'EventChaosCastle.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),

            -- Missing Potions/Consumables (14)
            (14, 7, 'Special Healing Potion', 'SpecialPotion.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 11, 'Box of Luck', 'MagicBox01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 12, 'Heart of Love', 'Event01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 15, 'Zen', 'Gold01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 17, 'Devil Square Key (Bronze)', 'Devil00.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 18, 'Devil Square Key (Silver)', 'Devil01.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 19, 'Devil Square Key (Gold)', 'Devil02.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 20, 'Remedy of Love', 'Drink00.bmd', 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 15),
            (14, 31, 'Guardian Angel Scroll', 'Suho.bmd', 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 15)
     )";
  char *err = nullptr;
  if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] SeedItemDefinitions error: %s\n", err);
    sqlite3_free(err);
  } else {
    // Count actual items seeded
    sqlite3_stmt *countStmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM item_definitions", -1,
                       &countStmt, nullptr);
    int seeded = 0;
    if (sqlite3_step(countStmt) == SQLITE_ROW)
      seeded = sqlite3_column_int(countStmt, 0);
    sqlite3_finalize(countStmt);
    printf("[DB] Seeded %d 0.97d item definitions\n", seeded);

    // Set buy prices by category formula + specific overrides
    // Weapons (cats 0-5): level_req * 100 + damage_max * 20
    sqlite3_exec(m_db,
                 "UPDATE item_definitions SET buy_price = level_req * 100 + "
                 "damage_max * 20 "
                 "WHERE category <= 5 AND buy_price = 0",
                 nullptr, nullptr, nullptr);
    // Shields (cat 6): level_req * 80 + defense * 30
    sqlite3_exec(
        m_db,
        "UPDATE item_definitions SET buy_price = level_req * 80 + defense * 30 "
        "WHERE category = 6 AND buy_price = 0",
        nullptr, nullptr, nullptr);
    // Armor (cats 7-11): level_req * 80 + defense * 30
    sqlite3_exec(
        m_db,
        "UPDATE item_definitions SET buy_price = level_req * 80 + defense * 30 "
        "WHERE category >= 7 AND category <= 11 AND buy_price = 0",
        nullptr, nullptr, nullptr);
    // Wings (cat 12, idx 0-6): fixed 50000
    sqlite3_exec(m_db,
                 "UPDATE item_definitions SET buy_price = 50000 "
                 "WHERE category = 12 AND item_index <= 6",
                 nullptr, nullptr, nullptr);
    // Orbs (cat 12, idx 7-24): level_req * 200
    sqlite3_exec(m_db,
                 "UPDATE item_definitions SET buy_price = level_req * 200 "
                 "WHERE category = 12 AND item_index >= 7 AND item_index <= 24"
                 " AND item_index != 15",
                 nullptr, nullptr, nullptr);
    // Jewel of Chaos (12,15): 810000
    sqlite3_exec(m_db,
                 "UPDATE item_definitions SET buy_price = 810000 "
                 "WHERE category = 12 AND item_index = 15",
                 nullptr, nullptr, nullptr);
    // Pets/Jewelry (cat 13): level_req * 300
    sqlite3_exec(m_db,
                 "UPDATE item_definitions SET buy_price = level_req * 300 "
                 "WHERE category = 13 AND buy_price = 0",
                 nullptr, nullptr, nullptr);
    // Potions (cat 14) — specific prices
    const char *potionPrices = R"(
        UPDATE item_definitions SET buy_price = CASE item_index
            WHEN 0 THEN 20 WHEN 1 THEN 80 WHEN 2 THEN 300 WHEN 3 THEN 1000
            WHEN 4 THEN 120 WHEN 5 THEN 450 WHEN 6 THEN 1500
            WHEN 7 THEN 3500 WHEN 8 THEN 100 WHEN 9 THEN 1000 WHEN 10 THEN 2000
            WHEN 13 THEN 9000000 WHEN 14 THEN 6000000 WHEN 16 THEN 45000000
            WHEN 20 THEN 900 WHEN 22 THEN 36000000
            ELSE 500 END
        WHERE category = 14;
    )";
    sqlite3_exec(m_db, potionPrices, nullptr, nullptr, nullptr);
    // Scrolls (cat 15) — specific prices
    const char *scrollPrices = R"(
        UPDATE item_definitions SET buy_price = CASE item_index
            WHEN 0 THEN 3800 WHEN 1 THEN 3100 WHEN 2 THEN 2400
            WHEN 3 THEN 1500 WHEN 4 THEN 4400 WHEN 5 THEN 2800
            WHEN 6 THEN 3500 WHEN 7 THEN 5000 WHEN 8 THEN 6200
            WHEN 9 THEN 7500 WHEN 10 THEN 500
            WHEN 11 THEN 12000 WHEN 12 THEN 18000 WHEN 13 THEN 30000
            ELSE 1000 END
        WHERE category = 15;
    )";
    sqlite3_exec(m_db, scrollPrices, nullptr, nullptr, nullptr);
    // Ammo — Bolt (4,7) and Arrows (4,15) — cheap
    sqlite3_exec(m_db,
                 "UPDATE item_definitions SET buy_price = 100 "
                 "WHERE (category = 4 AND item_index = 7) OR (category = 4 AND "
                 "item_index = 15)",
                 nullptr, nullptr, nullptr);
    printf("[DB] Set buy prices for all items\n");
  }
}

ItemDefinition Database::GetItemDefinition(int id) {
  return GetItemDefinition(static_cast<uint8_t>(id / 32),
                           static_cast<uint8_t>(id % 32));
}

ItemDefinition Database::GetItemDefinition(uint8_t category,
                                           uint8_t itemIndex) {
  ItemDefinition item;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, category, item_index, name, model_file, level_req, "
      "damage_min, damage_max, defense, attack_speed, two_handed, "
      "width, height, req_str, req_dex, req_vit, req_ene, class_flags, "
      "buy_price "
      "FROM item_definitions WHERE category=? AND item_index=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return item;

  sqlite3_bind_int(stmt, 1, category);
  sqlite3_bind_int(stmt, 2, itemIndex);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    item.id = sqlite3_column_int(stmt, 0);
    item.category = static_cast<uint8_t>(sqlite3_column_int(stmt, 1));
    item.itemIndex = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
    item.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    item.modelFile =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    item.level = static_cast<uint16_t>(sqlite3_column_int(stmt, 5));
    item.damageMin = static_cast<uint16_t>(sqlite3_column_int(stmt, 6));
    item.damageMax = static_cast<uint16_t>(sqlite3_column_int(stmt, 7));
    item.defense = static_cast<uint16_t>(sqlite3_column_int(stmt, 8));
    item.attackSpeed = static_cast<uint8_t>(sqlite3_column_int(stmt, 9));
    item.twoHanded = static_cast<uint8_t>(sqlite3_column_int(stmt, 10));
    item.width = static_cast<uint8_t>(sqlite3_column_int(stmt, 11));
    item.height = static_cast<uint8_t>(sqlite3_column_int(stmt, 12));
    item.reqStrength = static_cast<uint16_t>(sqlite3_column_int(stmt, 13));
    item.reqDexterity = static_cast<uint16_t>(sqlite3_column_int(stmt, 14));
    item.reqVitality = static_cast<uint16_t>(sqlite3_column_int(stmt, 15));
    item.reqEnergy = static_cast<uint16_t>(sqlite3_column_int(stmt, 16));
    item.classFlags = static_cast<uint32_t>(sqlite3_column_int64(stmt, 17));
    item.buyPrice = static_cast<uint32_t>(sqlite3_column_int(stmt, 18));
  }
  sqlite3_finalize(stmt);
  return item;
}

std::vector<ItemDropInfo> Database::GetItemsByLevelRange(int minLevel,
                                                         int maxLevel) {
  std::vector<ItemDropInfo> items;
  sqlite3_stmt *stmt = nullptr;
  // Exclude Wings (12+), Orbs, Quest Items, etc. Only allow Categories 0-11
  // (Gear)
  const char *sql =
      "SELECT category, item_index, name, level_req FROM item_definitions "
      "WHERE level_req BETWEEN ? AND ? AND category <= 11";

  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return items;

  sqlite3_bind_int(stmt, 1, minLevel);
  sqlite3_bind_int(stmt, 2, maxLevel);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ItemDropInfo info;
    info.category = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
    info.itemIndex = static_cast<uint8_t>(sqlite3_column_int(stmt, 1));
    info.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    info.level = static_cast<uint16_t>(sqlite3_column_int(stmt, 3));
    items.push_back(info);
  }
  sqlite3_finalize(stmt);
  return items;
}

std::vector<EquipmentSlot> Database::GetCharacterEquipment(int characterId) {
  std::vector<EquipmentSlot> equip;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT slot, item_category, item_index, item_level "
      "FROM character_equipment WHERE character_id=? ORDER BY slot";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return equip;

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
  sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM monster_spawns WHERE map_id=0",
                     -1, &stmt, nullptr);
  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    count = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if (count > 0) {
    printf("[DB] Monster spawns already seeded (%d entries)\n", count);
    return;
  }

  // Lorencia monster spawns from MonsterSetBase.txt (Main 5.2 / 0.97k
  // reference) Coordinates are grid (X,Y) from the reference spawn data
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
            (3, 0, 195, 115, 5), (3, 0, 192, 122, 6),

            -- Elite Bull Fighter (type 4): Level 12 — west region (OpenMU: X:8-94, Y:11-244)
            (4, 0, 25, 30, 2), (4, 0, 40, 50, 4), (4, 0, 55, 70, 6),
            (4, 0, 30, 90, 1), (4, 0, 45, 110, 3), (4, 0, 60, 130, 5),
            (4, 0, 35, 150, 0), (4, 0, 50, 170, 7), (4, 0, 65, 190, 2),
            (4, 0, 40, 210, 4), (4, 0, 55, 230, 6), (4, 0, 70, 40, 1),
            (4, 0, 85, 60, 3), (4, 0, 20, 80, 5), (4, 0, 75, 100, 0),

            -- Lich (type 6): Level 14 — southeast region (OpenMU: X:95-175, Y:168-244)
            (6, 0, 110, 180, 2), (6, 0, 125, 195, 4), (6, 0, 140, 210, 6),
            (6, 0, 155, 225, 1), (6, 0, 100, 190, 3), (6, 0, 115, 205, 5),
            (6, 0, 130, 220, 0), (6, 0, 145, 235, 7), (6, 0, 160, 175, 2),
            (6, 0, 170, 200, 4), (6, 0, 105, 240, 6),

            -- Giant (type 7): Level 17 — far west (OpenMU: X:8-60, Y:11-80 — reduced count, high level)
            (7, 0, 15, 20, 2), (7, 0, 25, 35, 4), (7, 0, 40, 50, 6),
            (7, 0, 55, 65, 1), (7, 0, 20, 45, 3), (7, 0, 35, 60, 5),
            (7, 0, 50, 75, 0), (7, 0, 30, 25, 7), (7, 0, 45, 40, 2),

            -- Skeleton Warrior (type 14): Level 19 — southeast (OpenMU: X:95-175, Y:168-244)
            (14, 0, 100, 175, 2), (14, 0, 120, 185, 4), (14, 0, 135, 200, 6),
            (14, 0, 150, 215, 1), (14, 0, 165, 230, 3), (14, 0, 108, 195, 5),
            (14, 0, 142, 240, 0), (14, 0, 155, 180, 7), (14, 0, 170, 210, 2)
    )";
  char *err = nullptr;
  if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    printf("[DB] SeedMonsterSpawns error: %s\n", err);
    sqlite3_free(err);
  } else {
    printf("[DB] Seeded Lorencia monster spawns (8 types: Bull Fighter, Hound, "
           "Budge Dragon, Spider, Elite Bull Fighter, Lich, Giant, Skeleton "
           "Warrior)\n");
  }
}

std::vector<MonsterSpawnData> Database::GetMonsterSpawns(uint8_t mapId) {
  std::vector<MonsterSpawnData> monsters;
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT id, type, map_id, pos_x, pos_y, direction "
                    "FROM monster_spawns WHERE map_id=? ORDER BY id";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return monsters;

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
  char sql[256];
  // Reset character stats to Level 1 defaults
  snprintf(sql, sizeof(sql),
           "UPDATE characters SET level=1, strength=28, dexterity=20, "
           "vitality=25, energy=10, life=110, max_life=110, mana=20, "
           "max_mana=20, experience=0, level_up_points=0 "
           "WHERE id=%d",
           characterId);
  sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);

  // Clear equipment and inventory
  snprintf(sql, sizeof(sql),
           "DELETE FROM character_equipment WHERE character_id=%d",
           characterId);
  sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);

  snprintf(sql, sizeof(sql),
           "DELETE FROM character_inventory WHERE character_id=%d",
           characterId);
  sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);

  printf("[DB] Reset character %d to Level 1 DK defaults (EMPTY)\n",
         characterId);
}

void Database::UpdateEquipment(int characterId, uint8_t slot, uint8_t category,
                               uint8_t itemIndex, uint8_t itemLevel) {
  sqlite3_stmt *stmt = nullptr;
  // UPSERT: insert or replace on (character_id, slot) unique constraint
  const char *sql =
      "INSERT INTO character_equipment (character_id, slot, item_category, "
      "item_index, item_level) "
      "VALUES (?, ?, ?, ?, ?) "
      "ON CONFLICT(character_id, slot) DO UPDATE SET "
      "item_category=excluded.item_category, item_index=excluded.item_index, "
      "item_level=excluded.item_level";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
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

std::vector<Database::InventorySlotData>
Database::GetCharacterInventory(int characterId) {
  std::vector<InventorySlotData> items;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT slot, def_index, quantity, item_level "
      "FROM character_inventory WHERE character_id=? ORDER BY slot";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return items;
  sqlite3_bind_int(stmt, 1, characterId);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    InventorySlotData d;
    d.slot = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
    d.defIndex = static_cast<int16_t>(sqlite3_column_int(stmt, 1));
    d.quantity = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
    d.itemLevel = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
    items.push_back(d);
  }
  sqlite3_finalize(stmt);
  return items;
}

void Database::SaveCharacterInventory(int characterId, int16_t defIndex,
                                      uint8_t quantity, uint8_t itemLevel,
                                      uint8_t slot) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "INSERT INTO character_inventory (character_id, slot, "
                    "def_index, quantity, item_level) "
                    "VALUES (?, ?, ?, ?, ?) "
                    "ON CONFLICT(character_id, slot) DO UPDATE SET "
                    "def_index=excluded.def_index, quantity=excluded.quantity, "
                    "item_level=excluded.item_level";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, slot);
  sqlite3_bind_int(stmt, 3, defIndex);
  sqlite3_bind_int(stmt, 4, quantity);
  sqlite3_bind_int(stmt, 5, itemLevel);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::DeleteCharacterInventoryItem(int characterId, uint8_t slot) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "DELETE FROM character_inventory WHERE character_id=? AND slot=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, slot);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::DeleteCharacterInventoryAll(int characterId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "DELETE FROM character_inventory WHERE character_id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::UpdateCharacterMoney(int characterId, uint32_t money) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "UPDATE characters SET money=? WHERE id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, static_cast<int>(money));
  sqlite3_bind_int(stmt, 2, characterId);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

uint64_t Database::GetXPForLevel(int level) {
  // Cubic curve, MaxLevel=400 (matches client logic)
  static constexpr double kScale =
      ((double)0xFFFFFFFF * 0.95) / (400.0 * 400.0 * 400.0);
  return (uint64_t)(kScale * (double)level * (double)level * (double)level);
}

std::vector<uint8_t> Database::GetCharacterSkills(int characterId) {
  std::vector<uint8_t> skills;
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT skill_id FROM character_skills WHERE character_id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return skills;
  sqlite3_bind_int(stmt, 1, characterId);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    skills.push_back(static_cast<uint8_t>(sqlite3_column_int(stmt, 0)));
  }
  sqlite3_finalize(stmt);
  return skills;
}

void Database::LearnSkill(int characterId, uint8_t skillId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "INSERT OR IGNORE INTO character_skills (character_id, "
                    "skill_id) VALUES (?, ?)";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, skillId);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  printf("[DB] Character %d learned skill %d\n", characterId, skillId);
}

bool Database::HasSkill(int characterId, uint8_t skillId) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT 1 FROM character_skills WHERE character_id=? AND "
                    "skill_id=?";
  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_int(stmt, 1, characterId);
  sqlite3_bind_int(stmt, 2, skillId);
  bool found = (sqlite3_step(stmt) == SQLITE_ROW);
  sqlite3_finalize(stmt);
  return found;
}
