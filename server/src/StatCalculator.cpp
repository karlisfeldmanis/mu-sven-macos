#include "StatCalculator.hpp"
#include <algorithm>

namespace StatCalculator {

int CalculateMaxHP(CharacterClass cls, int level, int vitality) {
  int hp = 0;
  switch (cls) {
  case CharacterClass::CLASS_DW:
    hp = 60 + (level - 1) * 1 + (vitality - 15) * 2;
    break;
  case CharacterClass::CLASS_DK:
    hp = 110 + (level - 1) * 2 + (vitality - 25) * 3;
    break;
  case CharacterClass::CLASS_ELF:
    hp = 80 + (level - 1) * 1 + (vitality - 20) * 2;
    break;
  case CharacterClass::CLASS_MG:
    hp = 110 + (level - 1) * 1 + (vitality - 26) * 2;
    break;
  }
  return std::max(1, hp);
}

int CalculateMaxMP(CharacterClass cls, int level, int energy) {
  int mp = 0;
  switch (cls) {
  case CharacterClass::CLASS_DW:
    mp = 60 + (level - 1) * 2 + (energy - 30) * 2;
    break;
  case CharacterClass::CLASS_DK:
    mp = 20 + (level - 1) * 0.5f + (energy - 10) * 1; // 0.5 rounded down
    break;
  case CharacterClass::CLASS_ELF:
    mp = 30 + (level - 1) * 1.5f + (energy - 15) * 1.5f; // 1.5 rounded down
    break;
  case CharacterClass::CLASS_MG:
    mp = 60 + (level - 1) * 1 + (energy - 26) * 2;
    break;
  }
  return std::max(0, mp);
}

int CalculateMinDamage(CharacterClass cls, int strength, int dexterity,
                       int energy, bool hasBow) {
  switch (cls) {
  case CharacterClass::CLASS_DW:
    return strength / 6;
  case CharacterClass::CLASS_DK:
    return strength / 6;
  case CharacterClass::CLASS_ELF:
    return hasBow ? ((strength / 14) + (dexterity / 7))
                  : ((strength + dexterity) / 7);
  case CharacterClass::CLASS_MG:
    return (strength / 6) + (energy / 12);
  }
  return 1;
}

int CalculateMaxDamage(CharacterClass cls, int strength, int dexterity,
                       int energy, bool hasBow) {
  switch (cls) {
  case CharacterClass::CLASS_DW:
    return strength / 4;
  case CharacterClass::CLASS_DK:
    return strength / 4;
  case CharacterClass::CLASS_ELF:
    return hasBow ? ((strength / 8) + (dexterity / 4))
                  : ((strength + dexterity) / 4);
  case CharacterClass::CLASS_MG:
    return (strength / 4) + (energy / 8);
  }
  return 1;
}

int CalculateMinMagicDamage(CharacterClass cls, int energy) {
  switch (cls) {
  case CharacterClass::CLASS_DW:
    return energy / 9;
  case CharacterClass::CLASS_DK: // DK actually has magic scaling internally
    return energy / 9;
  case CharacterClass::CLASS_ELF:
    return energy / 9;
  case CharacterClass::CLASS_MG:
    return energy / 9;
  }
  return 1;
}

int CalculateMaxMagicDamage(CharacterClass cls, int energy) {
  switch (cls) {
  case CharacterClass::CLASS_DW:
    return energy / 4;
  case CharacterClass::CLASS_DK:
    return energy / 4;
  case CharacterClass::CLASS_ELF:
    return energy / 4;
  case CharacterClass::CLASS_MG:
    return energy / 4;
  }
  return 1;
}

int CalculateDefense(CharacterClass cls, int dexterity) {
  switch (cls) {
  case CharacterClass::CLASS_DW:
    return dexterity / 4;
  case CharacterClass::CLASS_DK:
    return dexterity / 3;
  case CharacterClass::CLASS_ELF:
    return dexterity / 10;
  case CharacterClass::CLASS_MG:
    return dexterity / 4;
  }
  return 1;
}

int CalculateAttackRate(int level, int dexterity, int strength) {
  // Same across all 4 base classes for PvM
  return (level * 5) + (int)(dexterity * 1.5f) + (strength / 4);
}

int CalculateDefenseRate(CharacterClass cls, int dexterity) {
  switch (cls) {
  case CharacterClass::CLASS_DW:
    return dexterity / 3;
  case CharacterClass::CLASS_DK:
    return dexterity / 3;
  case CharacterClass::CLASS_ELF:
    return dexterity / 4;
  case CharacterClass::CLASS_MG:
    return dexterity / 3;
  }
  return 1;
}

int CalculateAttackSpeed(CharacterClass cls, int dexterity, bool hasBow) {
  switch (cls) {
  case CharacterClass::CLASS_DW:
    return dexterity / 20;
  case CharacterClass::CLASS_DK:
    return dexterity / 15;
  case CharacterClass::CLASS_ELF:
    return dexterity / 50;
  case CharacterClass::CLASS_MG:
    return dexterity / 15;
  }
  return 1;
}

int CalculateMagicSpeed(CharacterClass cls, int dexterity) {
  switch (cls) {
  case CharacterClass::CLASS_DW:
    return dexterity / 10;
  case CharacterClass::CLASS_DK:
    return dexterity / 20;
  case CharacterClass::CLASS_ELF:
    return dexterity / 50;
  case CharacterClass::CLASS_MG:
    return dexterity / 20;
  }
  return 1;
}

int CalculateMaxAG(int strength, int dexterity, int vitality, int energy) {
  // OpenMU ClassDarkKnight.cs: ENE*1.0 + VIT*0.3 + DEX*0.2 + STR*0.15
  return (int)(energy * 1.0f + vitality * 0.3f + dexterity * 0.2f +
               strength * 0.15f);
}

int CalculateMaxManaOrAG(CharacterClass cls, int level, int strength,
                         int dexterity, int vitality, int energy) {
  if (cls == CharacterClass::CLASS_DK)
    return CalculateMaxAG(strength, dexterity, vitality, energy);
  return CalculateMaxMP(cls, level, energy);
}

int GetLevelUpPoints(CharacterClass cls) {
  if (cls == CharacterClass::CLASS_MG) {
    return 7;
  }
  return 5;
}

} // namespace StatCalculator
