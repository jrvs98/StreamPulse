#include "metrics.h"

#include <algorithm>
#include <cmath>

namespace streampulse {

MetricsEngine::MetricsEngine(std::chrono::seconds window) : window_(window) {}

void MetricsEngine::add_frame(const FrameSample& sample) {
    const auto latency = std::chrono::duration<double, std::milli>(
        sample.observed_at - sample.captured_at).count();

    std::lock_guard<std::mutex> lock(mutex_);
    if (has_sequence_ && sample.sequence > last_sequence_ + 1) {
        dropped_frames_ += sample.sequence - last_sequence_ - 1;
    }
    last_sequence_ = sample.sequence;
    has_sequence_ = true;
    ++frames_seen_;
    last_encoded_bytes_ = sample.encoded_bytes;
    samples_.push_back({sample.observed_at, std::max(0.0, latency), sample.encoded_bytes});
    trim_locked(sample.observed_at);
}

void MetricsEngine::add_read_error() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++read_errors_;
}

void MetricsEngine::trim_locked(Clock::time_point now) const {
    while (!samples_.empty() && now - samples_.front().observed_at > window_) {
        samples_.pop_front();
    }
}

MetricsSnapshot MetricsEngine::snapshot(Clock::time_point now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    trim_locked(now);

    MetricsSnapshot result;
    result.frames_seen = frames_seen_;
    result.dropped_frames = dropped_frames_;
    result.read_errors = read_errors_;
    result.last_encoded_bytes = last_encoded_bytes_;
    if (samples_.empty()) {
        return result;
    }

    double latency_sum = 0.0;
    double bitrate_sum = 0.0;
    for (const auto& sample : samples_) {
        latency_sum += sample.latency_ms;
        bitrate_sum += static_cast<double>(sample.encoded_bytes) * 8.0;
    }
    result.latency_ms = samples_.back().latency_ms;
    result.average_latency_ms = latency_sum / samples_.size();

    double elapsed_seconds = std::chrono::duration<double>(
        samples_.back().observed_at - samples_.front().observed_at).count();
    if (elapsed_seconds <= 0.0) {
        elapsed_seconds = 1.0;
    }
    result.bitrate_mbps = bitrate_sum / elapsed_seconds / 1000000.0;

    const double mean_bits = bitrate_sum / samples_.size();
    for (const auto& sample : samples_) {
        const double bits = static_cast<double>(sample.encoded_bytes) * 8.0;
        const double difference = bits - mean_bits;
        result.bitrate_variance += difference * difference;
    }
    result.bitrate_variance /= samples_.size();
    return result;
}

}
