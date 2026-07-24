#include "orchestrator/workflow.h"

#include <sdbus-c++/sdbus-c++.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <iostream>

namespace ccme::orchestrator {

namespace {

constexpr const char* kPumpBusName = "org.ccme.Pump";
constexpr const char* kPumpObjectPath = "/org/ccme/pump";
constexpr const char* kPumpInterface = "org.ccme.Pump.Control";

constexpr const char* kStirrerBusName = "org.ccme.Stirrer";
constexpr const char* kStirrerObjectPath = "/org/ccme/stirrer";
constexpr const char* kStirrerInterface = "org.ccme.Stirrer.Control";

constexpr const char* kCameraBusName = "org.ccme.Camera";
constexpr const char* kCameraObjectPath = "/org/ccme/camera";
constexpr const char* kCameraInterface = "org.ccme.Camera.Control";

constexpr const char* kFmcBusName = "org.ccme.FMC";
constexpr const char* kFmcObjectPath = "/org/ccme/fmc";
constexpr const char* kFmcInterface = "org.ccme.FMC.Control";

constexpr int kStirSpeedRpm = 200;
constexpr double kHeatTempC = CCME_STIRRER_HEAT_TEMP;
constexpr int kDissolutionPollMs = 1000;

class DbusProxy {
public:
    DbusProxy() {
        try {
            conn_ = sdbus::createSystemBusConnection();
            std::cerr << "[ORCH] D-Bus proxy connected\n";
        } catch (const std::exception& e) {
            std::cerr << "[ORCH] D-Bus connection failed: " << e.what() << "\n";
        }
    }

    bool PumpStart(double volume_ml) {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kPumpBusName},
                sdbus::ObjectPath{kPumpObjectPath});
            proxy->callMethod("Start").onInterface(kPumpInterface).withArguments(volume_ml);
            std::cerr << "[ORCH] PumpStart(" << volume_ml << ") ok\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ORCH] PumpStart failed: " << e.what() << "\n";
            return false;
        }
    }

    bool PumpStop() {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kPumpBusName},
                sdbus::ObjectPath{kPumpObjectPath});
            proxy->callMethod("Stop").onInterface(kPumpInterface);
            std::cerr << "[ORCH] PumpStop ok\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ORCH] PumpStop failed: " << e.what() << "\n";
            return false;
        }
    }

    bool Pump2Start(double volume_ml) {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kPumpBusName},
                sdbus::ObjectPath{kPumpObjectPath});
            proxy->callMethod("Start2").onInterface(kPumpInterface).withArguments(volume_ml);
            std::cerr << "[ORCH] Pump2Start(" << volume_ml << ") ok\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ORCH] Pump2Start failed: " << e.what() << "\n";
            return false;
        }
    }

    bool Pump2Stop() {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kPumpBusName},
                sdbus::ObjectPath{kPumpObjectPath});
            proxy->callMethod("Stop2").onInterface(kPumpInterface);
            std::cerr << "[ORCH] Pump2Stop ok\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ORCH] Pump2Stop failed: " << e.what() << "\n";
            return false;
        }
    }

    bool StirrerStart(int rpm) {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kStirrerBusName},
                sdbus::ObjectPath{kStirrerObjectPath});
            proxy->callMethod("StartStir").onInterface(kStirrerInterface).withArguments(rpm);
            std::cerr << "[ORCH] StirrerStart(" << rpm << ") ok\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ORCH] StirrerStart failed: " << e.what() << "\n";
            return false;
        }
    }

    bool StirrerStop() {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kStirrerBusName},
                sdbus::ObjectPath{kStirrerObjectPath});
            proxy->callMethod("StopStir").onInterface(kStirrerInterface);
            std::cerr << "[ORCH] StirrerStop ok\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ORCH] StirrerStop failed: " << e.what() << "\n";
            return false;
        }
    }

    bool StirrerHeatStart(double temp_c) {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kStirrerBusName},
                sdbus::ObjectPath{kStirrerObjectPath});
            proxy->callMethod("StartHeat").onInterface(kStirrerInterface).withArguments(temp_c);
            std::cerr << "[ORCH] StirrerHeatStart(" << temp_c << ") ok\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ORCH] StirrerHeatStart failed: " << e.what() << "\n";
            return false;
        }
    }

    bool StirrerHeatStop() {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kStirrerBusName},
                sdbus::ObjectPath{kStirrerObjectPath});
            proxy->callMethod("StopHeat").onInterface(kStirrerInterface);
            std::cerr << "[ORCH] StirrerHeatStop ok\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ORCH] StirrerHeatStop failed: " << e.what() << "\n";
            return false;
        }
    }

    bool CheckDissolution() {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kCameraBusName},
                sdbus::ObjectPath{kCameraObjectPath});
            bool result = false;
            proxy->callMethod("CheckDissolution")
                 .onInterface(kCameraInterface)
                 .storeResultsTo(result);
            std::cerr << "[ORCH] CheckDissolution -> " << (result ? "dissolved" : "not yet") << "\n";
            return result;
        } catch (const std::exception& e) {
            std::cerr << "[ORCH] CheckDissolution failed: " << e.what() << "\n";
            return false;
        }
    }

    bool FmcMoveToVial(int index) {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kFmcBusName},
                sdbus::ObjectPath{kFmcObjectPath});
            proxy->callMethod("MoveToVial").onInterface(kFmcInterface).withArguments(index);
            std::cerr << "[ORCH] FmcMoveToVial(" << index << ") ok\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ORCH] FmcMoveToVial failed: " << e.what() << "\n";
            return false;
        }
    }

private:
    std::unique_ptr<sdbus::IConnection> conn_;
};

}  // namespace

struct Workflow::Impl {
    WorkflowState state{WorkflowState::kIdle};
    double volume_ml{0.0};
    int current_vial{0};
    int total_vials{CCME_VIAL_ROWS * CCME_VIAL_COLS};
    bool stop_requested{false};
    std::thread workflow_thread;
    DbusProxy dbus;

    Impl() {}

    void Run() {
        std::cerr << "[ORCH] Workflow started: volume=" << volume_ml
                  << "ml total_vials=" << total_vials << "\n";

        while (current_vial < total_vials && !stop_requested) {
            std::cerr << "[ORCH] --- Vial " << (current_vial + 1)
                      << "/" << total_vials << " ---\n";

            state = WorkflowState::kInjectingWater;
            std::cerr << "[ORCH] State -> injecting_water\n";
            if (!dbus.PumpStart(volume_ml)) {
                std::cerr << "[ORCH] PumpStart failed\n";
                state = WorkflowState::kError;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<int>(volume_ml / CCME_PUMP0_FLOW_RATE * 1000) + 500));

            state = WorkflowState::kHeating;
            std::cerr << "[ORCH] State -> heating\n";
            if (!dbus.StirrerHeatStart(kHeatTempC)) {
                std::cerr << "[ORCH] StirrerHeatStart failed\n";
                state = WorkflowState::kError;
                return;
            }

            state = WorkflowState::kStirring;
            std::cerr << "[ORCH] State -> stirring\n";
            if (!dbus.StirrerStart(kStirSpeedRpm)) {
                std::cerr << "[ORCH] StirrerStart failed\n";
                state = WorkflowState::kError;
                return;
            }

            state = WorkflowState::kCheckingDissolution;
            std::cerr << "[ORCH] State -> checking_dissolution\n";
            while (!stop_requested) {
                if (dbus.CheckDissolution()) break;
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(kDissolutionPollMs));
            }

            if (stop_requested) break;

            std::cerr << "[ORCH] Dissolution complete, stopping stirrer & heater\n";
            dbus.StirrerHeatStop();
            dbus.StirrerStop();

            state = WorkflowState::kMovingToVial;
            std::cerr << "[ORCH] State -> moving_to_vial\n";
            if (!dbus.FmcMoveToVial(current_vial)) {
                std::cerr << "[ORCH] FmcMoveToVial failed\n";
                state = WorkflowState::kError;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            state = WorkflowState::kDispensing;
            std::cerr << "[ORCH] State -> dispensing\n";
            if (!dbus.Pump2Start(volume_ml)) {
                std::cerr << "[ORCH] Pump2Start (dispense) failed\n";
                state = WorkflowState::kError;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<int>(volume_ml / CCME_PUMP1_FLOW_RATE * 1000) + 500));

            current_vial++;
        }

        state = stop_requested ? WorkflowState::kIdle
                               : WorkflowState::kComplete;
        std::cerr << "[ORCH] Workflow finished: state="
                  << (stop_requested ? "idle" : "complete") << "\n";
    }
};

Workflow::Workflow()
    : impl_(std::make_unique<Impl>()) {}

Workflow::~Workflow() {
    if (impl_ && impl_->state != WorkflowState::kIdle &&
        impl_->state != WorkflowState::kComplete) {
        (void)Stop();
    }
}

Workflow::Workflow(Workflow&&) noexcept = default;
Workflow& Workflow::operator=(Workflow&&) noexcept = default;

std::expected<bool, WorkflowError> Workflow::Start(double volume_ml) {
    if (impl_->state != WorkflowState::kIdle &&
        impl_->state != WorkflowState::kComplete) {
        std::cerr << "[ORCH] Start rejected: state="
                  << GetStateString() << "\n";
        return std::unexpected(WorkflowError::kAlreadyRunning);
    }

    std::cerr << "[ORCH] Start: volume=" << volume_ml << "ml\n";

    impl_->volume_ml = volume_ml;
    impl_->current_vial = 0;
    impl_->stop_requested = false;

    if (impl_->workflow_thread.joinable()) {
        impl_->workflow_thread.join();
    }

    impl_->workflow_thread = std::thread([this]() { impl_->Run(); });

    return true;
}

std::expected<bool, WorkflowError> Workflow::Stop() {
    if (impl_->state == WorkflowState::kIdle) {
        std::cerr << "[ORCH] Stop rejected: already idle\n";
        return std::unexpected(WorkflowError::kNotRunning);
    }

    std::cerr << "[ORCH] Stop requested (state=" << GetStateString() << ")\n";

    impl_->stop_requested = true;

    impl_->dbus.StirrerStop();
    impl_->dbus.PumpStop();
    impl_->dbus.Pump2Stop();

    if (impl_->workflow_thread.joinable()) {
        impl_->workflow_thread.join();
    }

    impl_->state = WorkflowState::kIdle;
    impl_->current_vial = 0;
    std::cerr << "[ORCH] Stopped\n";
    return true;
}

WorkflowState Workflow::GetState() const {
    return impl_->state;
}

std::string Workflow::GetStateString() const {
    switch (impl_->state) {
        case WorkflowState::kIdle:                return "idle";
        case WorkflowState::kInjectingWater:      return "injecting_water";
        case WorkflowState::kHeating:             return "heating";
        case WorkflowState::kStirring:            return "stirring";
        case WorkflowState::kCheckingDissolution:  return "checking_dissolution";
        case WorkflowState::kDispensing:          return "dispensing";
        case WorkflowState::kMovingToVial:        return "moving_to_vial";
        case WorkflowState::kComplete:            return "complete";
        case WorkflowState::kError:               return "error";
    }
    return "unknown";
}

int Workflow::GetCurrentVial() const {
    return impl_->current_vial;
}

int Workflow::GetTotalVials() const {
    return impl_->total_vials;
}

}  // namespace ccme::orchestrator
