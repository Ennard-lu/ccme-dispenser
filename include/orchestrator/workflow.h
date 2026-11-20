#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

namespace ccme::orchestrator {

enum class WorkflowState {
    kIdle,
    kHoming,
    kInjectingWater,
    kHeating,
    kStirring,
    kCheckingDissolution,
    kDispensing,
    kMovingToVial,
    kComplete,
    kError,
};

enum class WorkflowError {
    kAlreadyRunning,
    kNotRunning,
    kNotInError,
    kModuleUnavailable,
    kDispenseFailed,
    kDissolutionFailed,
};

struct WorkflowErrorInfo {
    // Machine-readable identifier, e.g. "fmc_home_failed".
    std::string code;
    // Human-readable description safe to display in the frontend.
    std::string message;
    // Suggested remediation.
    std::string hint;
    // Originating module: pump, pump2, stirrer, camera, fmc, orchestrator.
    std::string module;
    // Workflow stage at which the error happened.
    std::string stage;
    bool recoverable{true};
    // Unix epoch milliseconds.
    std::int64_t timestamp_ms{0};
};

struct WorkflowSnapshot {
    WorkflowState state{WorkflowState::kIdle};
    int current_vial{0};
    int total_vials{0};
    bool ai_check_skipped{false};
    std::optional<WorkflowErrorInfo> error;
};

[[nodiscard]] std::string ToString(WorkflowState state);

class Workflow {
public:
    Workflow();
    ~Workflow();

    Workflow(const Workflow&) = delete;
    Workflow& operator=(const Workflow&) = delete;
    Workflow(Workflow&&) noexcept;
    Workflow& operator=(Workflow&&) noexcept;

    std::expected<bool, WorkflowError> Start(double volume_ml, int stir_speed_rpm,
                                              double heat_temp_c, double dispense_volume_ml);
    std::expected<bool, WorkflowError> Stop();
    // Stop the machine if needed, clear the latched error and return to idle.
    // Only valid when idle (clears a stale error) or in the error/complete state.
    std::expected<bool, WorkflowError> ClearError();

    [[nodiscard]] WorkflowState GetState() const;
    [[nodiscard]] std::string GetStateString() const;
    [[nodiscard]] int GetCurrentVial() const;
    [[nodiscard]] int GetTotalVials() const;
    [[nodiscard]] WorkflowSnapshot GetStatus() const;
    [[nodiscard]] std::optional<WorkflowErrorInfo> GetLastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ccme::orchestrator
