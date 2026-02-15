#include "Server.hpp"
#include "PacketHandler.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <csignal>

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
    m_db.SeedItemDefinitions();

    // Seed default equipment for character 1 (TestDK)
    m_db.SeedDefaultEquipment(1);

    // Load NPC data from database
    m_world.LoadNpcsFromDB(m_db, 0); // map 0 = Lorencia

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

    while (m_running && !g_sigint) {
        // Build poll fd array: listen socket + all sessions
        std::vector<struct pollfd> fds;
        fds.push_back({m_listenFd, POLLIN, 0});
        for (auto &s : m_sessions) {
            short events = POLLIN;
            events |= POLLOUT; // Always check if we can write
            fds.push_back({s->GetFd(), events, 0});
        }

        int ret = poll(fds.data(), static_cast<nfds_t>(fds.size()), 100); // 100ms timeout
        if (ret < 0) {
            if (errno == EINTR) continue;
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
            auto &pfd = fds[i + 1]; // +1 because [0] is listen socket

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
                                   printf("[Server] Client fd=%d disconnected\n", s->GetFd());
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
        int clientFd = accept(m_listenFd, reinterpret_cast<sockaddr *>(&clientAddr), &addrLen);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("[Server] accept");
            break;
        }

        // Set non-blocking
        int flags = fcntl(clientFd, F_GETFL, 0);
        fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
        printf("[Server] New client from %s:%d (fd=%d)\n",
               ip, ntohs(clientAddr.sin_port), clientFd);

        auto session = std::make_unique<Session>(clientFd);
        OnClientConnected(*session);
        m_sessions.push_back(std::move(session));
    }
}

void Server::OnClientConnected(Session &session) {
    // Send welcome immediately
    PacketHandler::SendWelcome(session);

    // For our remaster client: send NPCs right away (no login needed)
    PacketHandler::SendNpcViewport(session, m_world);

    // Send default character equipment from database
    PacketHandler::SendEquipment(session, m_db, 1); // characterId=1 (TestDK)
}

void Server::HandlePacket(Session &session, const std::vector<uint8_t> &packet) {
    PacketHandler::Handle(session, packet, m_db, m_world);
}
