#include "MockData.hpp"

MockData MockData::CreateDK50() {
  MockData d;
  d.charName = "DarkKnight";
  d.className = "Dark Knight";
  d.classId = 16;
  d.level = 50;
  d.levelUpPoints = 5;

  d.hp = 850;
  d.maxHp = 1200;
  d.mp = 320;
  d.maxMp = 500;
  d.ag = 180;
  d.maxAg = 250;

  d.xp = 3500000;
  d.prevLevelXp = 2000000;
  d.nextLevelXp = 5000000;

  d.strength = 150;
  d.agility = 80;
  d.vitality = 100;
  d.energy = 50;

  d.attackDmgMin = 85;
  d.attackDmgMax = 120;
  d.attackRate = 210;
  d.defense = 95;
  d.defenseRate = 68;
  d.attackSpeed = 15;

  d.gold = 125000;
  return d;
}
