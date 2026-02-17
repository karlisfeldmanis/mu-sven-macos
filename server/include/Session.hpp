#ifndef MU_SESSION_HPP
#define MU_SESSION_HPP

#include <array>
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

    // Cached combat stats (populated on char select / equip change)
    uint16_t strength = 0;
    uint16_t dexterity = 0;
    int weaponDamageMin = 0;
    int weaponDamageMax = 0;
    int totalDefense = 0;

    // Server-authoritative HP tracking (monsters stop attacking dead players)
    int hp = 0;
    int maxHp = 0;
    bool dead = false;

    // Full character stats (for stat allocation validation)
    uint16_t vitality = 0;
    uint16_t energy = 0;
    uint16_t level = 1;
    uint16_t levelUpPoints = 0;
    uint64_t experience = 0;

    // Inventory bag (8x8 = 64 slots)
    struct InventoryItem {
        int8_t defIndex = -2;  // -2=empty
        uint8_t quantity = 0;
        uint8_t itemLevel = 0;
        bool occupied = false;
    };
    std::array<InventoryItem, 64> bag{};
    uint32_t zen = 0;

    // World position (updated from move packets, used for server AI aggro)
    float worldX = 0.0f;
    float worldZ = 0.0f;

private:
    int m_fd;
    bool m_alive = true;

    // Recv buffer — accumulates partial packets
    std::vector<uint8_t> m_recvBuf;

    // Send buffer — queued outgoing data
    std::vector<uint8_t> m_sendBuf;
};

#endif // MU_SESSION_HPP
