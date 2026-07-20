#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <memory>

namespace ccme::fmc {

enum class FmcError {
    kConnectionFailed,
    kDeviceNotFound,
    kMotionFailed,
    kHomeFailed,
    kTimeout,
    kAxisNotReady,
};

class FmcController {
public:
    FmcController();
    ~FmcController();

    FmcController(const FmcController&) = delete;
    FmcController& operator=(const FmcController&) = delete;
    FmcController(FmcController&&) noexcept;
    FmcController& operator=(FmcController&&) noexcept;

    std::expected<bool, FmcError> Connect();
    void Disconnect();

    std::expected<bool, FmcError> Home();
    std::expected<bool, FmcError> MoveTo(float x, float y);
    std::expected<bool, FmcError> MoveToVial(int index);
    std::expected<bool, FmcError> MoveToVial(int row, int col);
    [[nodiscard]] bool IsMoving() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ccme::fmc
