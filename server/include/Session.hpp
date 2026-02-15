#ifndef MU_SESSION_HPP
#define MU_SESSION_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class Session {
public:
    explicit Session(int fd);
    ~Session();

    int GetFd() const { return m_fd; }
    bool IsAlive() const { return m_alive; }

    // Returns complete packets extracted from recv buffer
    // Each vector<uint8_t> is one complete MU packet
    std::vector<std::vector<uint8_t>> ReadPackets();

    // Queue data to send
    void Send(const void *data, size_t len);

    // Flush send buffer to socket. Returns false if connection lost.
    bool FlushSend();

    // Mark session for removal
    void Kill() { m_alive = false; }

    // Session state
    int accountId = 0;
    int characterId = 0;
    std::string characterName;
    bool inWorld = false;

private:
    int m_fd;
    bool m_alive = true;

    // Recv buffer — accumulates partial packets
    std::vector<uint8_t> m_recvBuf;

    // Send buffer — queued outgoing data
    std::vector<uint8_t> m_sendBuf;
};

#endif // MU_SESSION_HPP
