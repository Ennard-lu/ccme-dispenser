#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <memory>

namespace ccme::stirrer {

enum class StirrerError {
    kDeviceNotFound,
    kSerialOpenFailed,
    kWriteFailed,
    kReadFailed,
    kTimeout,
    kNotRunning,
    kAlreadyRunning,
};

class StirrerController {
public:
    StirrerController();
    ~StirrerController();

    StirrerController(const StirrerController&) = delete;
    StirrerController& operator=(const StirrerController&) = delete;
    StirrerController(StirrerController&&) noexcept;
    StirrerController& operator=(StirrerController&&) noexcept;

    std::expected<bool, StirrerError> StartStir(int speed_rpm);
    std::expected<bool, StirrerError> StopStir();
    [[nodiscard]] bool IsRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ccme::stirrer
