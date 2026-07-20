#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <memory>

namespace ccme::pump {

enum class PumpError {
    kDeviceNotFound,
    kWriteFailed,
    kNotRunning,
    kAlreadyRunning,
    kInvalidVolume,
};

enum class PumpId : std::uint8_t {
    kPump1 = 0,
    kPump2 = 1,
};

class PumpController {
public:
    explicit PumpController(PumpId id);
    ~PumpController();

    PumpController(const PumpController&) = delete;
    PumpController& operator=(const PumpController&) = delete;
    PumpController(PumpController&&) noexcept;
    PumpController& operator=(PumpController&&) noexcept;

    std::expected<bool, PumpError> Start(double volume_ml);
    std::expected<bool, PumpError> Stop();
    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] PumpId GetId() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ccme::pump
