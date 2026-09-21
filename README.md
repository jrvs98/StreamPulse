# StreamPulse

StreamPulse is a small C++17 streaming quality monitor. It captures a webcam, local video file, or URL supported by OpenCV, measures capture-to-observation latency and encoded frame size, detects sequence gaps and read errors, and serves a live dashboard.

This is deliberately shaped like a small observability component: capture and measurement are separate from presentation, metrics use `std::chrono::steady_clock`, and the HTTP endpoint is plain JSON so another dashboard or collector can consume it.

## Build on macOS

Install the native dependencies with Homebrew:

```sh
brew install cmake opencv
```

Then configure and build:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run against the default camera:

```sh
./build/streampulse 0 8080
```

Open <http://localhost:8080>. A file path or OpenCV-supported stream URL can be passed instead:

```sh
./build/streampulse ./sample.mp4 8080
./build/streampulse rtsp://user:password@localhost:8554/test 8080
```

## Metrics

- **Latency:** elapsed time from immediately before `VideoCapture::read` to after JPEG encoding. Without source presentation timestamps, this is a capture and processing latency proxy rather than network glass-to-glass latency.
- **Dropped frames:** missing sequence numbers supplied by the capture loop. Read and encoding failures are reported separately and also shown in the dashboard's dropped count.
- **Bitrate:** rolling encoded-byte rate over the last 10 seconds. JPEG size is a bitrate proxy for this first version; a production H.264/AV1 integration should measure packet or encoded access-unit sizes.
- **Bitrate variance:** population variance of encoded bits in the same rolling window. It highlights unstable frame sizes even when average bitrate looks acceptable.

## Project layout

- `src/main.cpp`: OpenCV capture, worker threads, and dependency-free HTTP server
- `include/metrics.h`, `src/metrics.cpp`: thread-safe rolling metrics engine
- `dashboard/index.html`: polling dashboard
- `tests/metrics_tests.cpp`: deterministic timing and drop-count tests

## Follow-on work

For a production-grade monitor, add source PTS extraction, a bounded frame queue with backpressure counters, Prometheus/OpenTelemetry export, authentication and TLS on the HTTP layer, and integration tests using a deterministic test video or local RTSP fixture.
