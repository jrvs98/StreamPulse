#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace streampulse {

using Clock = std::chrono::steady_clock;

struct FrameSample {
    Clock::time_point captured_at;
    Clock::time_point observed_at;
    std::size_t encoded_bytes = 0;
    std::uint64_t sequence = 0;
};

struct MetricsSnapshot {
    double latency_ms = 0.0;
    double average_latency_ms = 0.0;
    double bitrate_mbps = 0.0;
    double bitrate_variance = 0.0;
    std::uint64_t frames_seen = 0;
    std::uint64_t dropped_frames = 0;
    std::uint64_t read_errors = 0;
    std::size_t last_encoded_bytes = 0;
};

class MetricsEngine {
public:
    explicit MetricsEngine(std::chrono::seconds window = std::chrono::seconds(10));

    void add_frame(const FrameSample& sample);
    void add_read_error();
    MetricsSnapshot snapshot(Clock::time_point now = Clock::now()) const;

private:
    struct TimedSample {
        Clock::time_point observed_at;
        double latency_ms;
        std::size_t encoded_bytes;
    };

    void trim_locked(Clock::time_point now) const;

    const std::chrono::seconds window_;
    mutable std::mutex mutex_;
    mutable std::deque<TimedSample> samples_;
    std::uint64_t frames_seen_ = 0;
    std::uint64_t dropped_frames_ = 0;
    std::uint64_t read_errors_ = 0;
    std::size_t last_encoded_bytes_ = 0;
    std::uint64_t last_sequence_ = 0;
    bool has_sequence_ = false;
};

}
