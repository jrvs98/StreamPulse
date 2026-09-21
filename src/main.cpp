#include "metrics.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

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

std::string json_metrics(const streampulse::MetricsSnapshot& metrics) {
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

std::string read_dashboard() {
#ifdef STREAMPULSE_WEB_ROOT
    std::ifstream file(std::string(STREAMPULSE_WEB_ROOT) + "/index.html");
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
#else
    return "<h1>Dashboard files are not configured</h1>";
#endif
}

class HttpServer {
public:
    HttpServer(streampulse::MetricsEngine& metrics, int port)
        : metrics_(metrics), port_(port) {}

    void run() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            std::cerr << "could not create HTTP socket\n";
            return;
        }
        int reuse = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(static_cast<uint16_t>(port_));
        if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
            listen(listen_fd_, 8) < 0) {
            std::cerr << "could not bind HTTP server on port " << port_ << "\n";
            close(listen_fd_);
            listen_fd_ = -1;
            return;
        }
        while (running) {
            const int client = accept(listen_fd_, nullptr, nullptr);
            if (client < 0) {
                continue;
            }
            handle(client);
            close(client);
        }
    }

    void stop() {
        if (listen_fd_ >= 0) {
            shutdown(listen_fd_, SHUT_RDWR);
            close(listen_fd_);
            listen_fd_ = -1;
        }
    }

private:
    void handle(int client) {
        char request[1024]{};
        const auto count = recv(client, request, sizeof(request) - 1, 0);
        if (count <= 0) return;
        const std::string request_line(request, static_cast<std::size_t>(count));
        const bool api = request_line.rfind("GET /api/metrics", 0) == 0;
        const std::string body = api ? json_metrics(metrics_.snapshot()) : read_dashboard();
        const std::string content_type = api ? "application/json" : "text/html; charset=utf-8";
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\nContent-Type: " << content_type
                 << "\r\nCache-Control: no-store\r\nContent-Length: " << body.size()
                 << "\r\nConnection: close\r\n\r\n" << body;
        const auto message = response.str();
        send(client, message.data(), message.size(), 0);
    }

    streampulse::MetricsEngine& metrics_;
    int port_;
    int listen_fd_ = -1;
};

bool open_source(cv::VideoCapture& capture, const std::string& source) {
    try {
        std::size_t consumed = 0;
        const int camera_index = std::stoi(source, &consumed);
        if (consumed == source.size()) return capture.open(camera_index);
    } catch (const std::exception&) {
    }
    return capture.open(source);
}
}

int main(int argc, char** argv) {
    std::string source = "0";
    int port = 8080;
    if (argc > 1) source = argv[1];
    if (argc > 2) port = std::stoi(argv[2]);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    cv::VideoCapture capture;
    if (!open_source(capture, source)) {
        std::cerr << "could not open source: " << source << "\n"
                  << "usage: streampulse [camera-index|video-file|stream-url] [port]\n";
        return 1;
    }

    streampulse::MetricsEngine metrics;
    HttpServer server(metrics, port);
    std::thread server_thread([&server] { server.run(); });
    std::thread capture_thread([&] {
        std::uint64_t sequence = 0;
        int consecutive_read_errors = 0;
        while (running) {
            const auto captured_at = streampulse::Clock::now();
            cv::Mat frame;
            if (!capture.read(frame) || frame.empty()) {
                metrics.add_read_error();
                if (++consecutive_read_errors >= 10 || !capture.isOpened()) break;
                continue;
            }
            consecutive_read_errors = 0;
            std::vector<unsigned char> encoded;
            if (!cv::imencode(".jpg", frame, encoded)) {
                metrics.add_read_error();
                continue;
            }
            metrics.add_frame({captured_at, streampulse::Clock::now(), encoded.size(), sequence++});
        }
        running = false;
    });

    std::cout << "StreamPulse listening at http://localhost:" << port << "\n";
    std::cout << "source: " << source << " (Ctrl-C to stop)\n";
    while (running) std::this_thread::yield();
    capture.release();
    server.stop();
    if (capture_thread.joinable()) capture_thread.join();
    if (server_thread.joinable()) server_thread.join();
    return 0;
}
