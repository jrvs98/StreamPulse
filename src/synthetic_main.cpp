#include "metrics.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
std::atomic<bool> running{true};

void handle_signal(int) { running = false; }

std::string dashboard() {
#ifdef STREAMPULSE_WEB_ROOT
    std::ifstream file(std::string(STREAMPULSE_WEB_ROOT) + "/index.html");
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
#else
    return "<h1>Dashboard files are not configured</h1>";
#endif
}

std::string metrics_json(const streampulse::MetricsSnapshot& metrics) {
    std::ostringstream body;
    body << "{\"latency_ms\":" << metrics.latency_ms
         << ",\"average_latency_ms\":" << metrics.average_latency_ms
         << ",\"bitrate_mbps\":" << metrics.bitrate_mbps
         << ",\"bitrate_variance\":" << metrics.bitrate_variance
         << ",\"frames_seen\":" << metrics.frames_seen
         << ",\"dropped_frames\":" << metrics.dropped_frames
         << ",\"read_errors\":" << metrics.read_errors
         << ",\"last_encoded_bytes\":" << metrics.last_encoded_bytes << "}";
    return body.str();
}

class Server {
public:
    Server(streampulse::MetricsEngine& metrics, int port) : metrics_(metrics), port_(port) {}

    void run() {
        socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ < 0) return;
        int reuse = 1;
        setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(static_cast<uint16_t>(port_));
        if (bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
            listen(socket_, 8) < 0) {
            close(socket_);
            socket_ = -1;
            return;
        }
        while (running) {
            const int client = accept(socket_, nullptr, nullptr);
            if (client < 0) continue;
            char request[512]{};
            const auto count = recv(client, request, sizeof(request) - 1, 0);
            if (count > 0) {
                const std::string request_text(request, static_cast<std::size_t>(count));
                const bool api = request_text.rfind("GET /api/metrics", 0) == 0;
                const std::string body = api ? metrics_json(metrics_.snapshot()) : dashboard();
                const std::string type = api ? "application/json" : "text/html; charset=utf-8";
                std::ostringstream response;
                response << "HTTP/1.1 200 OK\r\nContent-Type: " << type
                         << "\r\nCache-Control: no-store\r\nContent-Length: " << body.size()
                         << "\r\nConnection: close\r\n\r\n" << body;
                const auto message = response.str();
                send(client, message.data(), message.size(), 0);
            }
            close(client);
        }
    }

    void stop() {
        if (socket_ >= 0) {
            shutdown(socket_, SHUT_RDWR);
            close(socket_);
            socket_ = -1;
        }
    }

private:
    streampulse::MetricsEngine& metrics_;
    int port_;
    int socket_ = -1;
};
}

int main(int argc, char** argv) {
    const int port = argc > 1 ? std::stoi(argv[1]) : 8080;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    streampulse::MetricsEngine metrics;
    Server server(metrics, port);
    std::thread server_thread([&server] { server.run(); });
    std::thread generator([&metrics] {
        std::uint64_t sequence = 0;
        while (running) {
            const auto captured_at = streampulse::Clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(28));
            const auto observed_at = streampulse::Clock::now();
            const auto encoded_bytes = static_cast<std::size_t>(42000 + (sequence * 7919) % 26000);
            metrics.add_frame({captured_at, observed_at, encoded_bytes, sequence});
            sequence += (sequence > 0 && sequence % 37 == 0) ? 2 : 1;
        }
    });

    std::cout << "StreamPulse synthetic monitor: http://localhost:" << port << "\n";
    std::cout << "Synthetic frames simulate a 35 FPS stream; press Ctrl-C to stop.\n";
    while (running) std::this_thread::yield();
    server.stop();
    if (generator.joinable()) generator.join();
    if (server_thread.joinable()) server_thread.join();
    return 0;
}
