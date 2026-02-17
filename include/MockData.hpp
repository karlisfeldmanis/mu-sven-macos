#ifndef MOCK_DATA_HPP
#define MOCK_DATA_HPP

#include <cstdint>

// Mock game state for UI development without a server connection.
// All values can be controlled via the ImGui debug panel.
struct MockData {
    // Character identity
    const char* charName = "DarkKnight";
    const char* className = "Dark Knight";
    int level = 50;
    int levelUpPoints = 5;

    // Vitals
    int hp = 850;
    int maxHp = 1200;
    int mp = 320;
    int maxMp = 500;
    int ag = 180;
    int maxAg = 250;

    // Experience
    uint64_t xp = 3500000;
    uint64_t prevLevelXp = 2000000;
    uint64_t nextLevelXp = 5000000;

    // Stats
    int strength = 150;
    int agility = 80;
    int vitality = 100;
    int energy = 50;

    // Derived combat stats (simplified)
    int attackDmgMin = 85;
    int attackDmgMax = 120;
    int attackRate = 210;
    int defense = 95;
    int defenseRate = 68;
    int attackSpeed = 15;

    // Economy
    int gold = 125000;

    // Initialize with Dark Knight level 50 defaults
    static MockData CreateDK50();
};

#endif // MOCK_DATA_HPP
