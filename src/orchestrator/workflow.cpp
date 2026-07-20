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
constexpr int kDissolutionPollMs = 1000;

class DbusProxy {
public:
    DbusProxy() {
        try {
            conn_ = sdbus::createSystemBusConnection();
        } catch (const std::exception& e) {
            std::cerr << "D-Bus connection failed: " << e.what() << "\n";
        }
    }

    bool PumpStart(double volume_ml) {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kPumpBusName},
                sdbus::ObjectPath{kPumpObjectPath});
            proxy->callMethod("Start").onInterface(kPumpInterface).withArguments(volume_ml);
            return true;
        } catch (...) { return false; }
    }

    bool PumpStop() {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kPumpBusName},
                sdbus::ObjectPath{kPumpObjectPath});
            proxy->callMethod("Stop").onInterface(kPumpInterface);
            return true;
        } catch (...) { return false; }
    }

    bool StirrerStart(int rpm) {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kStirrerBusName},
                sdbus::ObjectPath{kStirrerObjectPath});
            proxy->callMethod("StartStir").onInterface(kStirrerInterface).withArguments(rpm);
            return true;
        } catch (...) { return false; }
    }

    bool StirrerStop() {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kStirrerBusName},
                sdbus::ObjectPath{kStirrerObjectPath});
            proxy->callMethod("StopStir").onInterface(kStirrerInterface);
            return true;
        } catch (...) { return false; }
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
            return result;
        } catch (...) { return false; }
    }

    bool FmcMoveToVial(int index) {
        if (!conn_) return false;
        try {
            auto proxy = sdbus::createProxy(*conn_,
                sdbus::ServiceName{kFmcBusName},
                sdbus::ObjectPath{kFmcObjectPath});
            proxy->callMethod("MoveToVial").onInterface(kFmcInterface).withArguments(index);
            return true;
        } catch (...) { return false; }
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
        while (current_vial < total_vials && !stop_requested) {
            state = WorkflowState::kInjectingWater;
            if (!dbus.PumpStart(volume_ml)) {
                state = WorkflowState::kError;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<int>(volume_ml / CCME_PUMP_FLOW_RATE * 1000) + 500));

            state = WorkflowState::kStirring;
            if (!dbus.StirrerStart(kStirSpeedRpm)) {
                state = WorkflowState::kError;
                return;
            }

            state = WorkflowState::kCheckingDissolution;
            while (!stop_requested) {
                if (dbus.CheckDissolution()) break;
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(kDissolutionPollMs));
            }

            if (stop_requested) break;

            dbus.StirrerStop();

            state = WorkflowState::kMovingToVial;
            if (!dbus.FmcMoveToVial(current_vial)) {
                state = WorkflowState::kError;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            state = WorkflowState::kDispensing;
            if (!dbus.PumpStart(volume_ml)) {
                state = WorkflowState::kError;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<int>(volume_ml / CCME_PUMP_FLOW_RATE * 1000) + 500));

            current_vial++;
        }

        state = stop_requested ? WorkflowState::kIdle
                               : WorkflowState::kComplete;
    }
};

Workflow::Workflow()
    : impl_(std::make_unique<Impl>()) {}

Workflow::~Workflow() {
    if (impl_ && impl_->state != WorkflowState::kIdle &&
        impl_->state != WorkflowState::kComplete) {
        Stop();
    }
}

Workflow::Workflow(Workflow&&) noexcept = default;
Workflow& Workflow::operator=(Workflow&&) noexcept = default;

std::expected<bool, WorkflowError> Workflow::Start(double volume_ml) {
    if (impl_->state != WorkflowState::kIdle &&
        impl_->state != WorkflowState::kComplete) {
        return std::unexpected(WorkflowError::kAlreadyRunning);
    }

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
        return std::unexpected(WorkflowError::kNotRunning);
    }

    impl_->stop_requested = true;

    impl_->dbus.StirrerStop();
    impl_->dbus.PumpStop();

    if (impl_->workflow_thread.joinable()) {
        impl_->workflow_thread.join();
    }

    impl_->state = WorkflowState::kIdle;
    impl_->current_vial = 0;
    return true;
}

WorkflowState Workflow::GetState() const {
    return impl_->state;
}

std::string Workflow::GetStateString() const {
    switch (impl_->state) {
        case WorkflowState::kIdle:                return "idle";
        case WorkflowState::kInjectingWater:      return "injecting_water";
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
