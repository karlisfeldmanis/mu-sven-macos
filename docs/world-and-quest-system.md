# World & Quest System

The world system manages map transitions, environment rendering, and the linear progression of the character through quests.

## Maps & Environments

The engine supports multiple distinct map types with unique atmospheric effects and attribute masks.

| Map ID | Name | Atmosphere | Key Features |
|--------|------|------------|--------------|
| **0** | Lorencia | Plains | Grassy terrain, interactive birds/leaves, NPC hubs. |
| **1** | Dungeon | Underground | Dark fog, erratic bats, narrow corridors. |
| **2** | Devias | Snow | Snow particles, rift/void rendering, slippery bridge logic. |

## Quest Progression

The game features an **18-quest linear chain** that guides the player from Level 1 toward high-level bosses.

### Progression Chains
1. **Lorencia Chain (Quests 0-8)**: 
    - Focused on local threats (Spiders, Bull Fighters, Liches).
    - Introduced by Wardens (Kael, Brynn, Dorian, Aldric, Marcus).
    - Mix of **Kill** and **Travel** objectives.
2. **Dungeon Chain (Quests 9-11)**:
    - High-stakes missions into the Dungeon entrance and depths.
    - Targets include Skeletons, Larvae, and Cyclops.
3. **Devias Chain (Quests 12-17)**:
    - Arctic missions involving Worms, Assassins, and Yetis.
    - **Final Boss**: Quest 17 targets the **Ice Queen**.

### Reward Mechanics
- **Zen & Experience**: Flat rewards to accelerate leveling.
- **Item Rewards**: Guaranteed class-specific gear (e.g., Bronze Armor, Legendary staff parts).
- **Skill Discovery**: High-level quests reward Skill Orbs and Scrolls (e.g., Twisting Slash, Aqua Beam).

## Technical Implementation
- `WorldHandler.cpp`: Manages viewport syncing and map switching.
- `QuestHandler.cpp`: Server-side state machine for kill tracking and rewards.
- `Database.cpp`: Persistent storage of quest indices and kill counts per character.
