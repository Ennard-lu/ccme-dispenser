#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <memory>

namespace ccme::camera {

enum class CameraError {
    kStreamNotFound,
    kConnectionFailed,
    kFrameCaptureFailed,
    kProcessingFailed,
};

class DissolutionDetector {
public:
    DissolutionDetector();
    ~DissolutionDetector();

    DissolutionDetector(const DissolutionDetector&) = delete;
    DissolutionDetector& operator=(const DissolutionDetector&) = delete;
    DissolutionDetector(DissolutionDetector&&) noexcept;
    DissolutionDetector& operator=(DissolutionDetector&&) noexcept;

    std::expected<bool, CameraError> CheckDissolution();
    std::expected<bool, CameraError> Connect();
    void Disconnect();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ccme::camera
