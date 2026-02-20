#ifndef MU_SERVER_CONNECTION_HPP
#define MU_SERVER_CONNECTION_HPP

#include "NetworkClient.hpp"
#include <cstdint>
#include <functional>

// Typed send API wrapping NetworkClient.
// Replaces scattered raw g_net.Send() calls with named methods that
// build the correct packet struct internally.
class ServerConnection {
public:
  bool Connect(const char *host, uint16_t port);
  void Poll();
  void Flush();
  void Disconnect();
  bool IsConnected() const;

  // Typed sends (build packet internally)
  void SendPrecisePosition(float worldX, float worldZ);
  void SendAttack(uint16_t monsterIndex);
  void SendPickup(uint16_t dropIndex);
  void SendCharSave(uint16_t charId, uint16_t level, uint16_t str,
                    uint16_t dex, uint16_t vit, uint16_t ene, uint16_t life,
                    uint16_t maxLife, uint16_t levelUpPoints,
                    uint64_t experience, int16_t quickSlotDefIndex);
  void SendEquip(uint16_t charId, uint8_t slot, uint8_t cat, uint8_t idx,
                 uint8_t lvl);
  void SendUnequip(uint16_t charId, uint8_t slot);
  void SendStatAlloc(uint8_t statType);
  void SendInventoryMove(uint8_t from, uint8_t to);
  void SendItemUse(uint8_t slot);
  void SendGridMove(uint8_t gridX, uint8_t gridY);

  // Packet receive callback (set by caller)
  std::function<void(const uint8_t *, int)> onPacket;

private:
  NetworkClient m_client;
};

#endif // MU_SERVER_CONNECTION_HPP
