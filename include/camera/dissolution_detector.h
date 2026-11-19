#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace ccme::camera {

enum class CameraError {
    kStreamNotFound,
    kConnectionFailed,
    kFrameCaptureFailed,
    kProcessingFailed,
    kAiNotConfigured,
    kAiRequestFailed,
    kAiResponseInvalid,
};

struct DetectionResult {
    bool dissolved{false};
    double confidence{0.0};
    std::string reasoning;
};

class DissolutionDetector {
public:
    DissolutionDetector();
    ~DissolutionDetector();

    DissolutionDetector(const DissolutionDetector&) = delete;
    DissolutionDetector& operator=(const DissolutionDetector&) = delete;
    DissolutionDetector(DissolutionDetector&&) noexcept;
    DissolutionDetector& operator=(DissolutionDetector&&) noexcept;

    // Capture a few frames from the camera stream and ask the configured
    // OpenAI-compatible vision model whether the solute is fully dissolved.
    std::expected<DetectionResult, CameraError> CheckDissolution();
    std::expected<bool, CameraError> Connect();
    void Disconnect();
    [[nodiscard]] bool IsConnected() const;
    [[nodiscard]] bool IsAiConfigured() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] const char* ToString(CameraError error) noexcept;

}  // namespace ccme::camera
