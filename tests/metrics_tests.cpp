#include "metrics.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>

using namespace streampulse;
using namespace std::chrono_literals;

int main() {
    const auto start = Clock::time_point{};
    MetricsEngine metrics(10s);

    metrics.add_frame({start, start + 20ms, 1000, 0});
    metrics.add_frame({start + 100ms, start + 130ms, 2000, 1});
    metrics.add_frame({start + 200ms, start + 240ms, 3000, 3});
    metrics.add_read_error();

    const auto result = metrics.snapshot(start + 240ms);
    assert(result.frames_seen == 3);
    assert(result.dropped_frames == 1);
    assert(result.read_errors == 1);
    assert(result.last_encoded_bytes == 3000);
    assert(std::abs(result.latency_ms - 40.0) < 0.001);
    assert(std::abs(result.average_latency_ms - 30.0) < 0.001);
    assert(result.bitrate_mbps > 0.0);
    assert(result.bitrate_variance > 0.0);

    const auto expired = metrics.snapshot(start + 11s);
    assert(expired.frames_seen == 3);
    assert(expired.latency_ms == 0.0);
    assert(expired.bitrate_mbps == 0.0);
    std::cout << "metrics tests passed\n";
}
