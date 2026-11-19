#include "orchestrator/workflow.h"

#include "dbus/ccme_dbus.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>
#include <utility>

namespace ccme::orchestrator {

namespace {

constexpr int kDissolutionPollMs = CCME_CAMERA_CHECK_INTERVAL_MS;
constexpr int kMaxConsecutiveCheckFailures = 5;
constexpr auto kFmcHomeTimeout = std::chrono::seconds(220);
constexpr auto kFmcMoveTimeout = std::chrono::seconds(620);
constexpr auto kCameraTimeout = std::chrono::seconds(CCME_AI_TIMEOUT_SEC + 30);

[[nodiscard]] const char* StateToString(WorkflowState state) {
    switch (state) {
        case WorkflowState::kIdle: return "idle";
        case WorkflowState::kHoming: return "homing";
        case WorkflowState::kInjectingWater: return "injecting_water";
        case WorkflowState::kHeating: return "heating";
        case WorkflowState::kStirring: return "stirring";
        case WorkflowState::kCheckingDissolution: return "checking_dissolution";
        case WorkflowState::kDispensing: return "dispensing";
        case WorkflowState::kMovingToVial: return "moving_to_vial";
        case WorkflowState::kComplete: return "complete";
        case WorkflowState::kError: return "error";
    }
    return "unknown";
}

// Thin typed wrapper around the shared D-Bus client. All names come from
// ccme::dbus, so the orchestrator and services cannot disagree about them.
class DbusProxy {
public:
    bool PumpStart(double volume_ml) {
        return Call(dbus::kPump, "Start", volume_ml);
    }

    bool PumpStop() {
        return Call(dbus::kPump, "Stop");
    }

    bool Pump2Start(double volume_ml) {
        return Call(dbus::kPump, "Start2", volume_ml);
    }

    bool Pump2Stop() {
        return Call(dbus::kPump, "Stop2");
    }

    bool StirrerStart(int rpm) {
        return Call(dbus::kStirrer, "StartStir", rpm);
    }

    bool StirrerStop() {
        return Call(dbus::kStirrer, "StopStir");
    }

    bool StirrerHeatStart(double temp_c) {
        return Call(dbus::kStirrer, "StartHeat", temp_c);
    }

    bool StirrerHeatStop() {
        return Call(dbus::kStirrer, "StopHeat");
    }

    std::expected<bool, dbus::Error> CheckDissolution() {
        return client_.CallWithResultAndTimeout<bool>(
            dbus::kCamera, "CheckDissolution", kCameraTimeout);
    }

    bool CameraConnect() {
        return Call(dbus::kCamera, "Connect");
    }

    std::expected<bool, dbus::Error> CameraIsAiConfigured() {
        return client_.CallWithResult<bool>(dbus::kCamera, "IsAiConfigured");
    }

    void CameraDisconnect() {
        (void)Call(dbus::kCamera, "Disconnect");
    }

    bool FmcReturnHome() {
        return CallWithTimeout(dbus::kFmc, "Home", kFmcHomeTimeout);
    }

    bool FmcMoveToVial(int index) {
        return CallWithTimeout(dbus::kFmc, "MoveToVial", kFmcMoveTimeout, index);
    }

private:
    template <typename... Args>
    bool Call(const dbus::Service& service, std::string_view method,
              Args&&... args) {
        auto result = client_.Call(service, method,
                                   std::forward<Args>(args)...);
        return Report(service, method, result);
    }

    template <typename... Args>
    bool CallWithTimeout(const dbus::Service& service, std::string_view method,
                         std::chrono::microseconds timeout, Args&&... args) {
        auto result = client_.CallWithTimeout(
            service, method, timeout, std::forward<Args>(args)...);
        return Report(service, method, result);
    }

    static bool Report(const dbus::Service& service, std::string_view method,
                       const std::expected<void, dbus::Error>& result) {
        if (!result) {
            std::cerr << "[ORCH] " << service.bus_name << "." << method
                      << " failed: " << result.error().message << "\n";
            return false;
        }
        return true;
    }

    dbus::Client client_;
};

}  // namespace

struct Workflow::Impl {
    std::atomic<WorkflowState> state{WorkflowState::kIdle};
    double volume_ml{0.0};
    int stir_speed_rpm{200};
    double heat_temp_c{50.0};
    double dispense_volume_ml{10.0};
    std::atomic<int> current_vial{0};
    int total_vials{CCME_VIAL_ROWS * CCME_VIAL_COLS};
    std::atomic<bool> stop_requested{false};
    std::thread workflow_thread;
    DbusProxy dbus;

    void SetState(WorkflowState new_state) {
        state.store(new_state, std::memory_order_relaxed);
        std::cerr << "[ORCH] State -> " << StateToString(new_state) << "\n";
    }

    // Sleep in small slices so a Stop request is honored quickly.
    bool SleepUnlessStopped(int milliseconds) {
        constexpr int kSliceMs = 100;
        int remaining = milliseconds;
        while (remaining > 0) {
            if (stop_requested.load(std::memory_order_relaxed)) {
                return false;
            }
            const int slice = std::min(remaining, kSliceMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(slice));
            remaining -= slice;
        }
        return !stop_requested.load(std::memory_order_relaxed);
    }

    void StopDevices() {
        (void)dbus.StirrerHeatStop();
        (void)dbus.StirrerStop();
        (void)dbus.PumpStop();
        (void)dbus.Pump2Stop();
    }

    void Fail(std::string_view message) {
        std::cerr << "[ORCH] ERROR: " << message << "\n";
        StopDevices();
        dbus.CameraDisconnect();
        SetState(WorkflowState::kError);
    }

    void Run() {
        if (auto configured = dbus.CameraIsAiConfigured();
            !configured || !*configured) {
            std::cerr << "[ORCH] ERROR: cloud vision API is not configured"
                      << (configured ? "" : " (camera service unavailable)")
                      << "\n";
            SetState(WorkflowState::kError);
            return;
        }
        if (!dbus.CameraConnect()) {
            std::cerr << "[ORCH] ERROR: camera stream is unavailable\n";
            SetState(WorkflowState::kError);
            return;
        }

        SetState(WorkflowState::kHoming);
        if (!dbus.FmcReturnHome()) {
            Fail("failed to return the axis platform home");
            return;
        }
        if (stop_requested.load(std::memory_order_relaxed)) {
            return;
        }

        SetState(WorkflowState::kInjectingWater);
        if (!dbus.PumpStart(volume_ml)) {
            Fail("failed to start water pump");
            return;
        }
        const int fill_ms = static_cast<int>(
            volume_ml / CCME_PUMP0_FLOW_RATE * 1000.0) + 500;
        if (!SleepUnlessStopped(fill_ms)) {
            return;
        }

        SetState(WorkflowState::kHeating);
        if (!dbus.StirrerHeatStart(heat_temp_c)) {
            Fail("failed to start heater");
            return;
        }

        SetState(WorkflowState::kStirring);
        if (!dbus.StirrerStart(stir_speed_rpm)) {
            Fail("failed to start stirrer");
            return;
        }

        SetState(WorkflowState::kCheckingDissolution);
        if (!dbus.CameraConnect()) {
            std::cerr << "[ORCH] Warning: camera connect failed, "
                         "dissolution checks may fail\n";
        }

        bool dissolved = false;
        int consecutive_failures = 0;
        const auto check_started = std::chrono::steady_clock::now();

        while (!stop_requested.load(std::memory_order_relaxed)) {
            if (CCME_DISSOLUTION_TIMEOUT_SEC > 0 &&
                std::chrono::steady_clock::now() - check_started >=
                    std::chrono::seconds(CCME_DISSOLUTION_TIMEOUT_SEC)) {
                std::cerr << "[ORCH] Dissolution check timed out after "
                          << CCME_DISSOLUTION_TIMEOUT_SEC << "s\n";
                break;
            }

            auto result = dbus.CheckDissolution();
            if (!result) {
                std::cerr << "[ORCH] CheckDissolution failed: "
                          << result.error().message << "\n";
                if (++consecutive_failures >= kMaxConsecutiveCheckFailures) {
                    std::cerr << "[ORCH] Too many consecutive dissolution "
                                 "check failures\n";
                    break;
                }
            } else {
                consecutive_failures = 0;
                if (*result) {
                    dissolved = true;
                    break;
                }
            }

            if (!SleepUnlessStopped(kDissolutionPollMs)) {
                break;
            }
        }

        dbus.CameraDisconnect();

        if (stop_requested.load(std::memory_order_relaxed)) {
            StopDevices();
            return;
        }
        if (!dissolved) {
            Fail("dissolution could not be confirmed");
            return;
        }

        std::cerr << "[ORCH] Dissolution complete, stopping stirrer & heater\n";
        (void)dbus.StirrerHeatStop();
        (void)dbus.StirrerStop();

        while (current_vial.load(std::memory_order_relaxed) < total_vials &&
               !stop_requested.load(std::memory_order_relaxed)) {
            const int vial = current_vial.load(std::memory_order_relaxed);
            std::cerr << "[ORCH] --- Vial " << (vial + 1)
                      << "/" << total_vials << " ---\n";

            SetState(WorkflowState::kMovingToVial);
            if (!dbus.FmcMoveToVial(vial)) {
                Fail("failed to move platform to vial");
                return;
            }
            if (!SleepUnlessStopped(500)) {
                return;
            }

            SetState(WorkflowState::kDispensing);
            if (!dbus.Pump2Start(dispense_volume_ml)) {
                Fail("failed to start dispense pump");
                return;
            }
            const int dispense_ms = static_cast<int>(
                dispense_volume_ml / CCME_PUMP1_FLOW_RATE * 1000.0) + 500;
            if (!SleepUnlessStopped(dispense_ms)) {
                return;
            }

            current_vial.fetch_add(1, std::memory_order_relaxed);
        }

        if (stop_requested.load(std::memory_order_relaxed)) {
            return;
        }
        SetState(WorkflowState::kComplete);
        std::cerr << "[ORCH] Workflow finished\n";
    }
};

Workflow::Workflow()
    : impl_(std::make_unique<Impl>()) {}

Workflow::~Workflow() {
    if (!impl_) {
        return;
    }

    if (impl_->workflow_thread.joinable()) {
        impl_->stop_requested.store(true, std::memory_order_relaxed);
        impl_->dbus.StirrerHeatStop();
        impl_->dbus.StirrerStop();
        impl_->dbus.PumpStop();
        impl_->dbus.Pump2Stop();
        impl_->dbus.CameraDisconnect();
        impl_->workflow_thread.join();
    }
}

Workflow::Workflow(Workflow&&) noexcept = default;
Workflow& Workflow::operator=(Workflow&&) noexcept = default;

std::expected<bool, WorkflowError> Workflow::Start(
    double volume_ml, int stir_speed_rpm, double heat_temp_c,
    double dispense_volume_ml) {
    const auto current = impl_->state.load(std::memory_order_relaxed);
    if (current != WorkflowState::kIdle &&
        current != WorkflowState::kComplete &&
        current != WorkflowState::kError) {
        std::cerr << "[ORCH] Start rejected: state="
                  << StateToString(current) << "\n";
        return std::unexpected(WorkflowError::kAlreadyRunning);
    }

    std::cerr << "[ORCH] Start: volume=" << volume_ml
              << "ml stir=" << stir_speed_rpm
              << "RPM temp=" << heat_temp_c
              << "C dispense=" << dispense_volume_ml << "ml\n";

    if (impl_->workflow_thread.joinable()) {
        impl_->workflow_thread.join();
    }

    impl_->volume_ml = volume_ml;
    impl_->stir_speed_rpm = stir_speed_rpm;
    impl_->heat_temp_c = heat_temp_c;
    impl_->dispense_volume_ml = dispense_volume_ml;
    impl_->current_vial.store(0, std::memory_order_relaxed);
    impl_->stop_requested.store(false, std::memory_order_relaxed);
    impl_->state.store(WorkflowState::kHoming, std::memory_order_relaxed);

    impl_->workflow_thread = std::thread([this]() { impl_->Run(); });
    return true;
}

std::expected<bool, WorkflowError> Workflow::Stop() {
    if (impl_->state.load(std::memory_order_relaxed) ==
        WorkflowState::kIdle) {
        std::cerr << "[ORCH] Stop rejected: already idle\n";
        return std::unexpected(WorkflowError::kNotRunning);
    }

    std::cerr << "[ORCH] Stop requested (state=" << GetStateString() << ")\n";

    impl_->stop_requested.store(true, std::memory_order_relaxed);

    impl_->dbus.StirrerHeatStop();
    impl_->dbus.StirrerStop();
    impl_->dbus.PumpStop();
    impl_->dbus.Pump2Stop();
    impl_->dbus.CameraDisconnect();

    if (impl_->workflow_thread.joinable()) {
        impl_->workflow_thread.join();
    }

    impl_->state.store(WorkflowState::kIdle, std::memory_order_relaxed);
    impl_->current_vial.store(0, std::memory_order_relaxed);
    std::cerr << "[ORCH] Stopped\n";
    return true;
}

WorkflowState Workflow::GetState() const {
    return impl_->state.load(std::memory_order_relaxed);
}

std::string Workflow::GetStateString() const {
    return StateToString(GetState());
}

int Workflow::GetCurrentVial() const {
    return impl_->current_vial.load(std::memory_order_relaxed);
}

int Workflow::GetTotalVials() const {
    return impl_->total_vials;
}

}  // namespace ccme::orchestrator
