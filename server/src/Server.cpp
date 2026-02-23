#include "Server.hpp"
#include "PacketDefs.hpp"
#include "PacketHandler.hpp"
#include "StatCalculator.hpp"
#include "handlers/CharacterHandler.hpp"
#include "handlers/InventoryHandler.hpp"
#include "handlers/WorldHandler.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile bool g_sigint = false;
static void sigHandler(int) { g_sigint = true; }

bool Server::Start(uint16_t port) {
  // Open database
  if (!m_db.Open("mu_server.db")) {
    printf("[Server] Failed to open database\n");
    return false;
  }
  m_db.CreateDefaultAccount();
  m_db.SeedNpcSpawns();
  m_db.SeedMonsterSpawns();
  m_db.SeedItemDefinitions();

  // No longer seeding default equipment by name; use DB status

  // Load terrain attributes for walkability checks (monster AI)
  // CMake symlinks client/Data/ into server/build/Data/
  if (!m_world.LoadTerrainAttributes("Data/World1/EncTerrain1.att")) {
    printf("[Server] Failed to load terrain attributes\n");
  }

  // Load NPC and monster data from database
  m_world.LoadNpcsFromDB(m_db, 0); // map 0 = Lorencia
  m_world.LoadMonstersFromDB(m_db, 0);

  // Create listen socket
  m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (m_listenFd < 0) {
    perror("[Server] socket");
    return false;
  }

  int opt = 1;
  setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // Set non-blocking
  int flags = fcntl(m_listenFd, F_GETFL, 0);
  fcntl(m_listenFd, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(m_listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    perror("[Server] bind");
    close(m_listenFd);
    return false;
  }

  if (listen(m_listenFd, 16) < 0) {
    perror("[Server] listen");
    close(m_listenFd);
    return false;
  }

  printf("[Server] Listening on port %d\n", port);
  m_running = true;
  return true;
}

void Server::Run() {
  signal(SIGINT, sigHandler);
  signal(SIGPIPE, SIG_IGN); // Ignore broken pipe

  srand(static_cast<unsigned int>(time(NULL)));

  auto lastTick = std::chrono::steady_clock::now();
  float autosaveTimer = 0.0f;
  static constexpr float AUTOSAVE_INTERVAL =
      60.0f; // Save all characters every 60s

  while (m_running && !g_sigint) {
    // Calculate delta time
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick).count();
    lastTick = now;

    // Game tick: update monster states, drop aging, wander AI, guard patrol
    std::vector<GameWorld::MonsterMoveUpdate> wanderMoves;
    std::vector<GameWorld::NpcMoveUpdate> npcMoves;
    m_world.Update(
        dt,
        [this](uint16_t dropIndex) {
          // Drop expired — broadcast removal to all clients
          PMSG_DROP_REMOVE_SEND pkt{};
          pkt.h = MakeC1Header(sizeof(pkt), Opcode::DROP_REMOVE);
          pkt.dropIndex = dropIndex;
          Broadcast(&pkt, sizeof(pkt));
        },
        &wanderMoves, &npcMoves,
        [this](uint16_t monsterIndex) {
          // Guard killed a monster — broadcast death (no XP reward)
          PMSG_MONSTER_DEATH_SEND deathPkt{};
          deathPkt.h = MakeC1Header(sizeof(deathPkt), Opcode::MON_DEATH);
          deathPkt.monsterIndex = monsterIndex;
          deathPkt.killerCharId = 0; // Guard kill, no player credit
          deathPkt.xpReward = 0;
          Broadcast(&deathPkt, sizeof(deathPkt));
        });

    // Broadcast wander moves to all clients
    for (auto &mv : wanderMoves) {
      PMSG_MONSTER_MOVE_SEND movePkt{};
      movePkt.h = MakeC1Header(sizeof(movePkt), Opcode::MON_MOVE);
      movePkt.monsterIndex = mv.monsterIndex;
      movePkt.targetX = mv.targetX;
      movePkt.targetY = mv.targetY;
      movePkt.chasing = mv.chasing;
      Broadcast(&movePkt, sizeof(movePkt));
    }

    // Broadcast guard patrol moves to all clients
    for (auto &mv : npcMoves) {
      PMSG_NPC_MOVE_SEND movePkt{};
      movePkt.h = MakeC1Header(sizeof(movePkt), Opcode::NPC_MOVE);
      movePkt.npcIndex = mv.npcIndex;
      movePkt.targetX = mv.targetX;
      movePkt.targetY = mv.targetY;
      Broadcast(&movePkt, sizeof(movePkt));
    }

    // Check for monster respawns and broadcast them
    for (auto &mon : const_cast<std::vector<MonsterInstance> &>(
             m_world.GetMonsterInstances())) {
      if (mon.justRespawned) {
        mon.justRespawned = false;
        PMSG_MONSTER_RESPAWN_SEND pkt{};
        pkt.h = MakeC1Header(sizeof(pkt), Opcode::MON_RESPAWN);
        pkt.monsterIndex = mon.index;
        pkt.x = mon.gridX;
        pkt.y = mon.gridY;
        pkt.hp = static_cast<uint16_t>(mon.hp);
        Broadcast(&pkt, sizeof(pkt));
      }
    }

    // Monster AI: aggro + attack players
    {
      std::vector<GameWorld::PlayerTarget> targets;
      for (auto &s : m_sessions) {
        if (!s->IsAlive() || !s->inWorld)
          continue;
        GameWorld::PlayerTarget pt;
        pt.fd = s->GetFd();
        pt.worldX = s->worldX;
        pt.worldZ = s->worldZ;
        pt.gridX = static_cast<uint8_t>(s->worldZ / 100.0f);
        pt.gridY = static_cast<uint8_t>(s->worldX / 100.0f);
        CharacterClass cls = static_cast<CharacterClass>(s->classCode);
        pt.defense = StatCalculator::CalculateDefense(cls, s->dexterity) +
                     s->totalDefense;
        pt.defenseRate =
            StatCalculator::CalculateDefenseRate(cls, s->dexterity);
        pt.life = s->hp;
        pt.dead = s->dead;
        targets.push_back(pt);
      }
      std::vector<GameWorld::MonsterMoveUpdate> moves;
      auto attacks = m_world.ProcessMonsterAI(dt, targets, moves);

      // Broadcast monster target updates to all clients (event-driven)
      for (auto &mv : moves) {
        PMSG_MONSTER_MOVE_SEND movePkt{};
        movePkt.h = MakeC1Header(sizeof(movePkt), Opcode::MON_MOVE);
        movePkt.monsterIndex = mv.monsterIndex;
        movePkt.targetX = mv.targetX;
        movePkt.targetY = mv.targetY;
        movePkt.chasing = mv.chasing;
        Broadcast(&movePkt, sizeof(movePkt));
      }

      for (auto &atk : attacks) {
        // Find the target session and apply damage server-side
        for (auto &s : m_sessions) {
          if (s->GetFd() == atk.targetFd && s->IsAlive()) {
            // Subtract damage from server-tracked HP
            s->hp -= atk.damage;
            if (s->hp <= 0) {
              s->hp = 0;
              s->dead = true;
              // Player died: drop aggro and return to spawn (keep current HP)
              auto *mon = m_world.FindMonster(atk.monsterIndex);
              if (mon) {
                mon->aggroTargetFd = -1;
                mon->aiState = MonsterInstance::AIState::RETURNING;
                mon->currentPath.clear();
                mon->pathStep = 0;
                printf("[Combat] Mon %d killed player fd=%d (mon HP=%d/%d)\n",
                       mon->index, s->GetFd(), mon->hp, mon->maxHp);
              }
            }

            // Send monster attack packet to client
            PMSG_MONSTER_ATTACK_SEND pkt{};
            pkt.h = MakeC1Header(sizeof(pkt), Opcode::MON_ATTACK);
            pkt.monsterIndex = atk.monsterIndex;
            pkt.damage = atk.damage;
            pkt.remainingHp = static_cast<float>(s->hp);
            s->Send(&pkt, sizeof(pkt));
            break;
          }
        }
      }
    }

    // Periodic autosave (every 60 seconds)
    autosaveTimer += dt;
    if (autosaveTimer >= AUTOSAVE_INTERVAL) {
      autosaveTimer = 0.0f;
      int saved = 0;
      for (auto &s : m_sessions) {
        if (s->IsAlive() && s->inWorld) {
          SaveSession(*s);
          saved++;
        }
      }
      if (saved > 0)
        printf("[Server] Autosave: saved %d character(s)\n", saved);
    }

    // Build poll fd array: listen socket + all sessions
    std::vector<struct pollfd> fds;
    fds.push_back({m_listenFd, POLLIN, 0});
    for (auto &s : m_sessions) {
      short events = POLLIN;
      events |= POLLOUT;
      fds.push_back({s->GetFd(), events, 0});
    }

    int ret = poll(fds.data(), static_cast<nfds_t>(fds.size()),
                   16); // 16ms for ~60Hz tick
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      perror("[Server] poll");
      break;
    }

    // Check listen socket
    if (fds[0].revents & POLLIN) {
      AcceptNewClients();
    }

    // Process sessions
    for (size_t i = 0; i < m_sessions.size(); i++) {
      auto &session = m_sessions[i];
      auto &pfd = fds[i + 1];

      // Tick cooldowns
      if (session->potionCooldown > 0.0f) {
        session->potionCooldown -= dt;
        if (session->potionCooldown < 0.0f)
          session->potionCooldown = 0.0f;
      }

      // Safe Zone HP Regeneration (~2% per second)
      if (session->inWorld && !session->dead && session->hp < session->maxHp) {
        if (m_world.IsSafeZone(session->worldX, session->worldZ)) {
          session->hpRemainder += 0.02f * (float)session->maxHp * dt;
          float threshold = std::max(1.0f, 0.02f * (float)session->maxHp);
          if (session->hpRemainder >= threshold) {
            int gain = (int)session->hpRemainder;
            session->hp = std::min(session->hp + gain, (int)session->maxHp);
            session->hpRemainder -= (float)gain;
            printf("[Regen] FD=%d Healed +%d HP in SafeZone. New HP: %d/%d\n",
                   session->GetFd(), gain, session->hp, session->maxHp);
            // Sync updated HP to client (session-only, don't reload from DB!)
            CharacterHandler::SendCharStats(*session);
            // Persist to DB
            SaveSession(*session);
          }
        } else {
          session->hpRemainder = 0.0f;
        }
      }

      // AG/Mana recovery logic
      if (session->inWorld && !session->dead) {
        bool isDK = session->classCode == 16;

        // Mana recovery (existing logic)
        if (session->mana < session->maxMana) {
          if (isDK || m_world.IsSafeZone(session->worldX, session->worldZ)) {
            float rate = isDK ? 0.05f : 0.02f;
            int gain = (int)(rate * session->maxMana * dt);
            if (gain >= 1) {
              session->mana = std::min(session->mana + gain, session->maxMana);
              CharacterHandler::SendCharStats(*session);
            }
          }
        }

        // AG (Ability Gauge) recovery every 3 seconds
        if (session->ag < session->maxAg) {
          session->agRegenTimer += dt;
          if (session->agRegenTimer >= 3.0f) {
            session->agRegenTimer = 0.0f;

            // TotalRate: Base (DK=5%, others=3%) + Idle Bonus (3% if idle >5s)
            float totalRate = isDK ? 5.0f : 3.0f;
            uint32_t nowMs = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());

            if (nowMs - session->lastAgUseTime >= 5000) {
              totalRate += 3.0f;
            }

            int gain = static_cast<int>((session->maxAg * totalRate) / 100.0f);
            if (gain < 1)
              gain = 1;

            session->ag = std::min(session->ag + gain, session->maxAg);
            printf("[Regen] FD=%d AG +%d (%d/%d) Rate: %.0f%%\n",
                   session->GetFd(), gain, session->ag, session->maxAg,
                   totalRate);
            CharacterHandler::SendCharStats(*session);
          }
        }
      }

      if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        session->Kill();
        continue;
      }

      if (pfd.revents & POLLIN) {
        auto packets = session->ReadPackets();
        for (auto &pkt : packets) {
          HandlePacket(*session, pkt);
        }
      }

      if (pfd.revents & POLLOUT) {
        session->FlushSend();
      }
    }

    // Remove dead sessions (save before removing)
    m_sessions.erase(
        std::remove_if(m_sessions.begin(), m_sessions.end(),
                       [this](const auto &s) {
                         if (!s->IsAlive()) {
                           if (s->inWorld)
                             SaveSession(*s);
                           printf("[Server] Client fd=%d disconnected\n",
                                  s->GetFd());
                           return true;
                         }
                         return false;
                       }),
        m_sessions.end());
  }

  // Save all sessions before shutdown
  for (auto &s : m_sessions) {
    if (s->IsAlive() && s->inWorld)
      SaveSession(*s);
  }
  printf("[Server] Shutting down...\n");
}

void Server::SaveSession(Session &session) {
  if (session.characterId <= 0)
    return;

  // Convert world position back to grid coordinates
  uint8_t posX = static_cast<uint8_t>(session.worldZ / 100.0f);
  uint8_t posY = static_cast<uint8_t>(session.worldX / 100.0f);

  // Save stats, HP, mana, position, money
  m_db.SaveCharacterFull(session.characterId, session.level, session.strength,
                         session.dexterity, session.vitality, session.energy,
                         static_cast<uint16_t>(std::max(session.hp, 0)),
                         static_cast<uint16_t>(session.maxHp),
                         static_cast<uint16_t>(std::max(session.mana, 0)),
                         static_cast<uint16_t>(session.maxMana),
                         static_cast<uint16_t>(std::max(session.ag, 0)),
                         static_cast<uint16_t>(session.maxAg),
                         session.levelUpPoints, session.experience, session.zen,
                         posX, posY, session.skillBar,
                         session.potionBar, session.rmcSkillId);

  // Save full inventory (clear + rewrite all occupied slots)
  m_db.DeleteCharacterInventoryAll(session.characterId);
  for (int i = 0; i < 64; i++) {
    auto &item = session.bag[i];
    if (item.primary && item.defIndex >= 0) {
      m_db.SaveCharacterInventory(session.characterId, item.defIndex,
                                  item.quantity, item.itemLevel,
                                  static_cast<uint8_t>(i));
    }
  }

  // Save only occupied equipment slots (skip empty 0xFF to reduce DB writes)
  for (int i = 0; i < Session::NUM_EQUIP_SLOTS; i++) {
    auto &eq = session.equipment[i];
    if (eq.category != 0xFF) {
      m_db.UpdateEquipment(session.characterId, static_cast<uint8_t>(i),
                           eq.category, eq.itemIndex, eq.itemLevel);
    }
  }
}

void Server::Stop() {
  m_running = false;
  if (m_listenFd >= 0) {
    close(m_listenFd);
    m_listenFd = -1;
  }
  m_sessions.clear();
  m_db.Close();
}

void Server::AcceptNewClients() {
  while (true) {
    struct sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);
    int clientFd =
        accept(m_listenFd, reinterpret_cast<sockaddr *>(&clientAddr), &addrLen);
    if (clientFd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      perror("[Server] accept");
      break;
    }

    // Set non-blocking
    int flags = fcntl(clientFd, F_GETFL, 0);
    fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

    // Disable Nagle's algorithm for low latency
    int tcpNoDelay = 1;
    setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &tcpNoDelay,
               sizeof(tcpNoDelay));

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
    printf("[Server] New client from %s:%d (fd=%d)\n", ip,
           ntohs(clientAddr.sin_port), clientFd);

    auto session = std::make_unique<Session>(clientFd);
    OnClientConnected(*session);
    m_sessions.push_back(std::move(session));
  }
}

void Server::OnClientConnected(Session &session) {
  // Send welcome immediately
  WorldHandler::SendWelcome(session);

  // For our remaster client: send NPCs + monsters right away (no login needed)
  WorldHandler::SendNpcViewport(session, m_world);

  // Send v2 monster viewport with HP/state/index (C2 header supports >255
  // bytes)
  auto v2pkt = m_world.BuildMonsterViewportV2Packet();
  if (!v2pkt.empty())
    session.Send(v2pkt.data(), v2pkt.size());

  // Find the first character for our account
  int accountId = 1; // Default for remaster
  auto charList = m_db.GetCharacterList(accountId);
  CharacterData c;
  if (!charList.empty()) {
    c = charList[0];

    session.characterId = c.id;
    // Load equipment into session cache and send to client
    {
      auto equip = m_db.GetCharacterEquipment(c.id);
      for (auto &e : equip) {
        if (e.slot < Session::NUM_EQUIP_SLOTS) {
          session.equipment[e.slot].category = e.category;
          session.equipment[e.slot].itemIndex = e.itemIndex;
          session.equipment[e.slot].itemLevel = e.itemLevel;
        }
      }
    }
    CharacterHandler::SendEquipment(session, m_db, c.id);
    // Send character stats (level, STR/DEX/VIT/ENE, XP, stat points)
    CharacterHandler::SendCharStats(session, m_db, c.id);

    // Initial inventory sync - Load from DB first
    session.zen = c.money;
    InventoryHandler::LoadInventory(session, m_db, c.id);
    InventoryHandler::SendInventorySync(session);

    // Load and send learned skills
    session.learnedSkills = m_db.GetCharacterSkills(c.id);
    CharacterHandler::SendSkillList(session);

    printf("[Server] FD=%d initialized with character '%s' (ID:%d)\n",
           session.GetFd(), c.name.c_str(), c.id);
  } else {
    printf("[Server] WARNING: Default character 'RealPlayer' not found for "
           "FD=%d\n",
           session.GetFd());
  }

  // Cache combat stats for default character (no login flow)
  // The character 'c' is already fetched above.
  if (c.id > 0) { // Check if character was found
    session.inWorld = true;
    session.level = c.level;
    session.charClass = c.charClass;
    session.classCode = c.charClass;
    session.vitality = c.vitality;
    session.energy = c.energy;
    session.strength = c.strength;
    session.dexterity = c.dexterity;
    session.worldX = c.posY * 100.0f;
    session.worldZ = c.posX * 100.0f;
    session.maxHp = StatCalculator::CalculateMaxHP(
        static_cast<CharacterClass>(c.charClass), c.level, c.vitality);
    session.maxMana = StatCalculator::CalculateMaxManaOrAG(
        static_cast<CharacterClass>(c.charClass), c.level, c.strength,
        c.dexterity, c.vitality, c.energy);
    session.hp = std::min(static_cast<int>(c.life), session.maxHp);
    session.mana = std::min(static_cast<int>(c.mana), session.maxMana);
    session.experience = c.experience;
    session.levelUpPoints = c.levelUpPoints;
    session.dead = false;
    memcpy(session.skillBar, c.skillBar, 10);
    memcpy(session.potionBar, c.potionBar, 8); // 4 × int16_t
    session.rmcSkillId = c.rmcSkillId;
  }
  CharacterHandler::RefreshCombatStats(session, m_db, c.id);
  {
    CharacterClass cls = static_cast<CharacterClass>(session.classCode);
    int baseDef = StatCalculator::CalculateDefense(cls, session.dexterity);
    printf("[Server] Combat stats: STR=%d weapon=%d-%d baseDef=%d equipDef=%d "
           "totalDef=%d\n",
           session.strength, session.weaponDamageMin, session.weaponDamageMax,
           baseDef, session.totalDefense, baseDef + session.totalDefense);
  }

  // Send existing ground drops so late-joining clients see them
  for (auto &drop : m_world.GetDrops()) {
    PMSG_DROP_SPAWN_SEND dpkt{};
    dpkt.h = MakeC1Header(sizeof(dpkt), Opcode::DROP_SPAWN);
    dpkt.dropIndex = drop.index;
    dpkt.defIndex = drop.defIndex;
    dpkt.quantity = drop.quantity;
    dpkt.itemLevel = drop.itemLevel;
    dpkt.worldX = drop.worldX;
    dpkt.worldZ = drop.worldZ;
    session.Send(&dpkt, sizeof(dpkt));
  }
}

void Server::HandlePacket(Session &session,
                          const std::vector<uint8_t> &packet) {
  PacketHandler::Handle(session, packet, m_db, m_world, *this);
}

void Server::Broadcast(const void *data, size_t len) {
  for (auto &s : m_sessions) {
    if (s->IsAlive() && s->inWorld) {
      s->Send(data, len);
    }
  }
}

void Server::BroadcastExcept(int excludeFd, const void *data, size_t len) {
  for (auto &s : m_sessions) {
    if (s->IsAlive() && s->inWorld && s->GetFd() != excludeFd) {
      s->Send(data, len);
    }
  }
}
