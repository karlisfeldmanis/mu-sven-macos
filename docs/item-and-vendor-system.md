# Item & Vendor System

The project features a comprehensive item database and a server-authoritative shop system.

## Item Database

With over **900+ unique item definitions**, the system covers all legacy 0.97d equipment and consumables.

### Categories
| Category | Types | Key Items |
|----------|-------|-----------|
| **0-3** | Weapons | Swords (Kris, Blade of Destruction), Axes, Maces, Spears |
| **4** | Ranged | Bows, Crossbows, Arrows/Bolts |
| **5** | Magic | Staves (Legendary, Resurrection, Chaos Lighting) |
| **6-11** | Armor | Sets: Bronze, Dragon, Legendary, Pad, Bone, Leather, Scale, Sphinx, Plate |
| **12** | Wings/Orbs | Wings of Heaven/Spirit/Dragon, Skill Orbs (Twisting Slash, Death Stab) |
| **13** | Accessories | Rings (Ice, Poison), Pendants, Mounts (Uniria, Dinorant) |
| **14** | Consumables | Potions (HP/Mana), Jewels (Bless, Soul, Life, Chaos) |
| **15** | Scrolls | Wizardry Spells (Evil Spirit, Hellfire, Aqua Beam) |

### Item Properties
- **Requirements**: Level, Strength, Dexterity, Vitality, Energy.
- **Visuals**: Individual BMD models for all equipment; support for **Chrome Glow** at +7 and higher.
- **Inventory**: 64-slot grid with size-based placement (e.g., 1x1 Potions, 2x4 Spears).

## Vendor & NPC System

NPCs are server-spawned and handle interactions for shops, vaults, and quests.

### Key Merchants
| NPC Name | Role | Location |
|----------|------|----------|
| **Hanzo the Blacksmith** | Weapon/Armor Shop | Lorencia |
| **Pasi the Mage** | Wizardry Items & Scrolls | Lorencia |
| **Potion Girl Amy** | Consumables | Lorencia |
| **Lumen the Barmaid** | Basic Supplies | Lorencia |
| **Baz the Vault Keeper** | Storage Manager | Devias / Lorencia |
| **Zienna Arms Dealer** | Advanced Weapons | Devias |

### Shop Mechanics
- **Buying**: Server validates Zen balance and inventory space before processing purchase.
- **Selling**: Items can be sold back to merchants for Zen.
- **Persistence**: All transactions are mirrored to the SQLite `character_inventory` and `characters` tables.

## C++ Modules
- `ItemDatabase.cpp`: Client-side registry of all item properties and models.
- `ShopHandler.cpp`: Server-side logic for Buy/Sell/Open requests.
- `NpcManager.cpp`: Client-side rendering and animation of NPCs.
