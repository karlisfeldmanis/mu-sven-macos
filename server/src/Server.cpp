#include "Server.hpp"
#include "PacketDefs.hpp"
#include "PacketHandler.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile bool g_sigint = false;
static void sigHandler(int) { g_sigint = true; }

bool Server::Start(uint16_t port) {
  // Open database
  if (!m_db.Open("build/mu_server.db")) {
    printf("[Server] Failed to open database\n");
    return false;
  }
  m_db.CreateDefaultAccount();
  m_db.SeedNpcSpawns();
  m_db.SeedMonsterSpawns();
  m_db.SeedItemDefinitions();

  // Seed default equipment for character 1 (TestDK)
  m_db.SeedDefaultEquipment(1);

  // Load terrain attributes for walkability checks (monster AI)
  // Path relative to server build dir → client data dir
  m_world.LoadTerrainAttributes(
      "references/other/MuMain/src/bin/Data/World1/EncTerrain1.att");

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

  auto lastTick = std::chrono::steady_clock::now();

  while (m_running && !g_sigint) {
    // Calculate delta time
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick).count();
    lastTick = now;

    // Game tick: update monster states, drop aging, wander AI
    std::vector<GameWorld::MonsterMoveUpdate> wanderMoves;
    m_world.Update(
        dt,
        [this](uint16_t dropIndex) {
          // Drop expired — broadcast removal to all clients
          PMSG_DROP_REMOVE_SEND pkt{};
          pkt.h = MakeC1Header(sizeof(pkt), 0x2E);
          pkt.dropIndex = dropIndex;
          Broadcast(&pkt, sizeof(pkt));
        },
        &wanderMoves);

    // Broadcast wander moves to all clients
    for (auto &mv : wanderMoves) {
      PMSG_MONSTER_MOVE_SEND movePkt{};
      movePkt.h = MakeC1Header(sizeof(movePkt), 0x35);
      movePkt.monsterIndex = mv.monsterIndex;
      movePkt.targetX = mv.targetX;
      movePkt.targetY = mv.targetY;
      movePkt.chasing = mv.chasing;
      Broadcast(&movePkt, sizeof(movePkt));
    }

    // Check for monster respawns and broadcast them
    for (auto &mon : const_cast<std::vector<MonsterInstance> &>(
             m_world.GetMonsterInstances())) {
      if (mon.justRespawned) {
        mon.justRespawned = false;
        PMSG_MONSTER_RESPAWN_SEND pkt{};
        pkt.h = MakeC1Header(sizeof(pkt), 0x30);
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
        pt.defense = s->totalDefense;
        pt.defenseRate = (int)s->dexterity / 4; // simplified defense rate
        pt.dead = s->dead;
        targets.push_back(pt);
      }
      std::vector<GameWorld::MonsterMoveUpdate> moves;
      auto attacks = m_world.ProcessMonsterAI(dt, targets, moves);

      // Broadcast monster target updates to all clients (event-driven)
      for (auto &mv : moves) {
        PMSG_MONSTER_MOVE_SEND movePkt{};
        movePkt.h = MakeC1Header(sizeof(movePkt), 0x35);
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
            }

            // Send monster attack packet to client
            PMSG_MONSTER_ATTACK_SEND pkt{};
            pkt.h = MakeC1Header(sizeof(pkt), 0x2F);
            pkt.monsterIndex = atk.monsterIndex;
            pkt.damage = atk.damage;
            pkt.remainingHp = static_cast<uint16_t>(s->hp);
            s->Send(&pkt, sizeof(pkt));
            break;
          }
        }
      }
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

    // Remove dead sessions
    m_sessions.erase(
        std::remove_if(m_sessions.begin(), m_sessions.end(),
                       [](const auto &s) {
                         if (!s->IsAlive()) {
                           printf("[Server] Client fd=%d disconnected\n",
                                  s->GetFd());
                           return true;
                         }
                         return false;
                       }),
        m_sessions.end());
  }

  printf("[Server] Shutting down...\n");
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
  PacketHandler::SendWelcome(session);

  // For our remaster client: send NPCs + monsters right away (no login needed)
  PacketHandler::SendNpcViewport(session, m_world);

  // Send v2 monster viewport with HP/state/index (C2 header supports >255
  // bytes)
  auto v2pkt = m_world.BuildMonsterViewportV2Packet();
  if (!v2pkt.empty())
    session.Send(v2pkt.data(), v2pkt.size());

  // Find the default character for our simplified flow
  CharacterData c = m_db.GetCharacter("TestDK");
  if (c.id > 0) {
    session.characterId = c.id;
    // Send character equipment from database
    PacketHandler::SendEquipment(session, m_db, c.id);
    // Send character stats (level, STR/DEX/VIT/ENE, XP, stat points)
    PacketHandler::SendCharStats(session, m_db, c.id);

    // Initial inventory sync
    PacketHandler::SendInventorySync(session);

    printf("[Server] FD=%d initialized with character '%s' (ID:%d)\n",
           session.GetFd(), c.name.c_str(), c.id);
  } else {
    printf("[Server] WARNING: Default character 'TestDK' not found for FD=%d\n",
           session.GetFd());
  }

  // Cache combat stats for default character (no login flow)
  // The character 'c' is already fetched above.
  if (c.id > 0) { // Check if character was found
    session.inWorld = true;
    session.strength = c.strength;
    session.dexterity = c.dexterity;
    session.worldX = c.posY * 100.0f;
    session.worldZ = c.posX * 100.0f;
    session.hp = c.life;
    session.maxHp = c.maxLife;
    session.dead = false;
  }
  auto equip = m_db.GetCharacterEquipment(1);
  session.weaponDamageMin = 0;
  session.weaponDamageMax = 0;
  session.totalDefense = 0;
  for (auto &slot : equip) {
    auto itemDef = m_db.GetItemDefinition(slot.category, slot.itemIndex);
    if (itemDef.id > 0) {
      if (slot.slot == 0) {
        session.weaponDamageMin = itemDef.damageMin + slot.itemLevel * 3;
        session.weaponDamageMax = itemDef.damageMax + slot.itemLevel * 3;
      }
      session.totalDefense += itemDef.defense + slot.itemLevel * 2;
    }
  }
  printf("[Server] Default char combat stats: STR=%d weapon=%d-%d def=%d\n",
         session.strength, session.weaponDamageMin, session.weaponDamageMax,
         session.totalDefense);

  // Send existing ground drops so late-joining clients see them
  for (auto &drop : m_world.GetDrops()) {
    PMSG_DROP_SPAWN_SEND dpkt{};
    dpkt.h = MakeC1Header(sizeof(dpkt), 0x2B);
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
