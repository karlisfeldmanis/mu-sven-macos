#include "Server.hpp"
#include <cstdio>

int main(int argc, char *argv[]) {
    setbuf(stdout, nullptr); // Disable buffering for log visibility
    printf("=== MU Online Server (Lorencia) ===\n");
    printf("0.97d compatible — minimal implementation\n\n");

    uint16_t port = 44405;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    Server server;
    if (!server.Start(port)) {
        printf("Failed to start server\n");
        return 1;
    }

    server.Run();
    server.Stop();

    printf("Server stopped.\n");
    return 0;
}
