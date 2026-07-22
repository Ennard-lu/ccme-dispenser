#include "pump/pump_controller.h"

#include <chrono>
#include <fstream>
#include <string>
#include <thread>

namespace ccme::pump {

namespace {

void WriteFile(const std::string& path, const std::string& value) {
    std::ofstream ofs(path);
    ofs << value;
}

void ExportGpio(int pin) {
    WriteFile("/sys/class/gpio/export", std::to_string(pin));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    WriteFile("/sys/class/gpio/gpio" + std::to_string(pin) + "/direction", "out");
}

void SetGpio(int pin, int value) {
    WriteFile("/sys/class/gpio/gpio" + std::to_string(pin) + "/value",
              std::to_string(value));
}

}  // namespace

struct PumpController::Impl {
    PumpId id;
    bool running{false};
    std::string pwm_path;
    int pwm_channel;
    int gpio_dir_pin;
    double flow_rate;
    int pwm_period_us;
    std::chrono::steady_clock::time_point start_time;
    std::thread timer_thread;

    Impl(PumpId pump_id)
        : id(pump_id) {
        if (id == PumpId::kPump1) {
            pwm_path = std::string(CCME_PUMP0_PWM_PATH) + "/pwm"
                       + std::to_string(CCME_PUMP0_PWM_CHANNEL);
            pwm_channel = CCME_PUMP0_PWM_CHANNEL;
            gpio_dir_pin = CCME_PUMP0_GPIO_DIR_PIN;
            flow_rate = CCME_PUMP0_FLOW_RATE;
            pwm_period_us = CCME_PUMP0_PWM_PERIOD_US;
        } else {
            pwm_path = std::string(CCME_PUMP1_PWM_PATH) + "/pwm"
                       + std::to_string(CCME_PUMP1_PWM_CHANNEL);
            pwm_channel = CCME_PUMP1_PWM_CHANNEL;
            gpio_dir_pin = CCME_PUMP1_GPIO_DIR_PIN;
            flow_rate = CCME_PUMP1_FLOW_RATE;
            pwm_period_us = CCME_PUMP1_PWM_PERIOD_US;
        }
    }

    void ExportPins() {
        const char* chip = (id == PumpId::kPump1) ? CCME_PUMP0_PWM_PATH
                                                   : CCME_PUMP1_PWM_PATH;
        WriteFile(std::string(chip) + "/export", std::to_string(pwm_channel));
        ExportGpio(gpio_dir_pin);
    }

    void StartPwm() {
        WriteFile(pwm_path + "/period", std::to_string(pwm_period_us));
        WriteFile(pwm_path + "/duty_cycle",
                  std::to_string(pwm_period_us / 2));
        WriteFile(pwm_path + "/enable", "1");
    }

    void StopPwm() {
        WriteFile(pwm_path + "/enable", "0");
    }
};

PumpController::PumpController(PumpId id)
    : impl_(std::make_unique<Impl>(id)) {}

PumpController::~PumpController() {
    if (impl_ && impl_->running) {
        (void)Stop();
    }
}

PumpController::PumpController(PumpController&&) noexcept = default;
PumpController& PumpController::operator=(PumpController&&) noexcept = default;

std::expected<bool, PumpError> PumpController::Start(double volume_ml) {
    if (impl_->running) {
        return std::unexpected(PumpError::kAlreadyRunning);
    }
    if (volume_ml <= 0.0) {
        return std::unexpected(PumpError::kInvalidVolume);
    }

    impl_->ExportPins();
    SetGpio(impl_->gpio_dir_pin, 1);
    impl_->StartPwm();

    impl_->running = true;
    impl_->start_time = std::chrono::steady_clock::now();

    double duration_s = volume_ml / impl_->flow_rate;
    auto duration_ms = std::chrono::milliseconds(
        static_cast<int>(duration_s * 1000.0));

    impl_->timer_thread = std::thread([this, duration_ms]() {
        std::this_thread::sleep_for(duration_ms);
        (void)Stop();
    });
    impl_->timer_thread.detach();

    return true;
}

std::expected<bool, PumpError> PumpController::Stop() {
    if (!impl_->running) {
        return std::unexpected(PumpError::kNotRunning);
    }

    impl_->StopPwm();
    SetGpio(impl_->gpio_dir_pin, 0);
    impl_->running = false;
    return true;
}

bool PumpController::IsRunning() const {
    return impl_->running;
}

PumpId PumpController::GetId() const {
    return impl_->id;
}

}  // namespace ccme::pump
