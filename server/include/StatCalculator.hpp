#pragma once
#include "PacketDefs.hpp"
#include <cstdint>

namespace StatCalculator {

int CalculateMaxHP(CharacterClass cls, int level, int vitality);
int CalculateMaxMP(CharacterClass cls, int level, int energy);

int CalculateMinDamage(CharacterClass cls, int strength, int dexterity,
                       int energy, bool hasBow);
int CalculateMaxDamage(CharacterClass cls, int strength, int dexterity,
                       int energy, bool hasBow);

int CalculateMinMagicDamage(CharacterClass cls, int energy);
int CalculateMaxMagicDamage(CharacterClass cls, int energy);

int CalculateDefense(CharacterClass cls, int dexterity);

int CalculateAttackRate(int level, int dexterity, int strength);
int CalculateDefenseRate(CharacterClass cls, int dexterity);

int CalculateAttackSpeed(CharacterClass cls, int dexterity, bool hasBow);
int CalculateMagicSpeed(CharacterClass cls, int dexterity);

// DK Ability Gauge (AG) — replaces mana for Dark Knight
int CalculateMaxAG(int strength, int dexterity, int vitality, int energy);

// Returns AG for DK, MP for other classes
int CalculateMaxManaOrAG(CharacterClass cls, int level, int strength,
                         int dexterity, int vitality, int energy);

// Helper function to get stat points per level
int GetLevelUpPoints(CharacterClass cls);

} // namespace StatCalculator
