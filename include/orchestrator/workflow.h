#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace ccme::orchestrator {

enum class WorkflowState {
    kIdle,
    kInjectingWater,
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
    kModuleUnavailable,
    kDispenseFailed,
    kDissolutionFailed,
};

class Workflow {
public:
    Workflow();
    ~Workflow();

    Workflow(const Workflow&) = delete;
    Workflow& operator=(const Workflow&) = delete;
    Workflow(Workflow&&) noexcept;
    Workflow& operator=(Workflow&&) noexcept;

    std::expected<bool, WorkflowError> Start(double volume_ml);
    std::expected<bool, WorkflowError> Stop();
    [[nodiscard]] WorkflowState GetState() const;
    [[nodiscard]] std::string GetStateString() const;
    [[nodiscard]] int GetCurrentVial() const;
    [[nodiscard]] int GetTotalVials() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ccme::orchestrator
