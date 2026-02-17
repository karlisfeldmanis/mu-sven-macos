#ifndef MU_SERVER_HPP
#define MU_SERVER_HPP

#include "Session.hpp"
#include "Database.hpp"
#include "GameWorld.hpp"
#include <cstdint>
#include <memory>
#include <vector>

class Server {
public:
    bool Start(uint16_t port);
    void Run(); // Main loop (blocks)
    void Stop();

    Database &GetDB() { return m_db; }
    GameWorld &GetWorld() { return m_world; }

    // Broadcast to all sessions that are inWorld
    void Broadcast(const void *data, size_t len);
    void BroadcastExcept(int excludeFd, const void *data, size_t len);

private:
    void AcceptNewClients();
    void ProcessSessions();
    void HandlePacket(Session &session, const std::vector<uint8_t> &packet);
    void OnClientConnected(Session &session);

    int m_listenFd = -1;
    bool m_running = false;

    std::vector<std::unique_ptr<Session>> m_sessions;
    Database m_db;
    GameWorld m_world;
};

#endif // MU_SERVER_HPP
