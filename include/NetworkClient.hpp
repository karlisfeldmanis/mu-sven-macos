#ifndef NETWORK_CLIENT_HPP
#define NETWORK_CLIENT_HPP

#include <cstdint>
#include <functional>
#include <vector>

class NetworkClient {
public:
    ~NetworkClient();

    bool Connect(const char *host, uint16_t port);
    void Disconnect();
    bool IsConnected() const { return m_connected; }

    // Call once per frame: reads available data, extracts complete MU packets
    void Poll();

    // Queue data to send
    void Send(const void *data, size_t len);

    // Flush queued send buffer (non-blocking)
    void Flush();

    // Packet callback: called for each complete MU packet received
    // Parameters: raw packet data, packet size
    std::function<void(const uint8_t *, int)> onPacket;

private:
    int m_fd = -1;
    bool m_connected = false;
    std::vector<uint8_t> m_recvBuf;
    std::vector<uint8_t> m_sendBuf;
};

#endif // NETWORK_CLIENT_HPP
