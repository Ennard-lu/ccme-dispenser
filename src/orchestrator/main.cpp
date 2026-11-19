#include "orchestrator/workflow.h"
#include "dbus/ccme_dbus.h"

#include <sdbus-c++/sdbus-c++.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <memory>
#include <string>

namespace {

[[nodiscard]] const char* WorkflowErrorToString(
    ccme::orchestrator::WorkflowError error) {
    using ccme::orchestrator::WorkflowError;
    switch (error) {
        case WorkflowError::kAlreadyRunning:
            return "workflow is already running";
        case WorkflowError::kNotRunning:
            return "workflow is not running";
        case WorkflowError::kNotInError:
            return "workflow is running; reset is only allowed in error/idle";
        case WorkflowError::kModuleUnavailable:
            return "required module is unavailable";
        case WorkflowError::kDispenseFailed:
            return "dispense failed";
        case WorkflowError::kDissolutionFailed:
            return "dissolution check failed";
    }
    return "unknown workflow error";
}

[[nodiscard]] std::string BuildStatusJson(
    const ccme::orchestrator::WorkflowSnapshot& status) {
    nlohmann::json json;
    json["state"] = ccme::orchestrator::ToString(status.state);
    json["current_vial"] = status.current_vial;
    json["total_vials"] = status.total_vials;

    if (status.error) {
        const auto& error = *status.error;
        json["error"] = {
            {"active", true},
            {"code", error.code},
            {"message", error.message},
            {"hint", error.hint},
            {"module", error.module},
            {"stage", error.stage},
            {"recoverable", error.recoverable},
            {"timestamp_ms", error.timestamp_ms},
        };
    } else {
        json["error"] = nullptr;
    }

    return json.dump();
}

}  // namespace

int main() {
    try {
        const auto& service = ccme::dbus::kOrchestrator;

        auto connection = ccme::dbus::Connect(service);
        auto workflow = std::make_unique<ccme::orchestrator::Workflow>();
        auto object = ccme::dbus::CreateObject(*connection, service);

        object->addVTable(
            sdbus::registerMethod("Start").implementedAs(
                [&workflow](double volume_ml, int stir_speed_rpm,
                            double heat_temp_c,
                            double dispense_volume_ml) -> bool {
                    std::cerr << "[ORCH] D-Bus: Start(" << volume_ml << ", "
                              << stir_speed_rpm << ", " << heat_temp_c << ", "
                              << dispense_volume_ml << ")\n";
                    auto result = workflow->Start(volume_ml, stir_speed_rpm,
                                                  heat_temp_c,
                                                  dispense_volume_ml);
                    if (!result) {
                        auto message = std::string{"Workflow start failed: "} +
                                       WorkflowErrorToString(result.error());
                        ccme::dbus::ThrowError(ccme::dbus::kOrchestrator,
                                               message);
                    }
                    return *result;
                }),
            sdbus::registerMethod("Stop").implementedAs([&workflow]() -> bool {
                std::cerr << "[ORCH] D-Bus: Stop\n";
                auto result = workflow->Stop();
                if (!result) {
                    auto message = std::string{"Workflow stop failed: "} +
                                   WorkflowErrorToString(result.error());
                    ccme::dbus::ThrowError(ccme::dbus::kOrchestrator,
                                           message);
                }
                return *result;
            }),
            sdbus::registerMethod("ClearError").implementedAs(
                [&workflow]() -> bool {
                    std::cerr << "[ORCH] D-Bus: ClearError\n";
                    auto result = workflow->ClearError();
                    if (!result) {
                        auto message =
                            std::string{"Workflow reset failed: "} +
                            WorkflowErrorToString(result.error());
                        ccme::dbus::ThrowError(ccme::dbus::kOrchestrator,
                                               message);
                    }
                    return *result;
                }),
            sdbus::registerMethod("GetStatus").implementedAs(
                [&workflow]() -> std::string {
                    return BuildStatusJson(workflow->GetStatus());
                }),
            sdbus::registerMethod("GetState").implementedAs(
                [&workflow]() -> std::string {
                    return workflow->GetStateString();
                }),
            sdbus::registerMethod("GetCurrentVial").implementedAs(
                [&workflow]() -> int {
                    return workflow->GetCurrentVial();
                }),
            sdbus::registerMethod("GetTotalVials").implementedAs(
                [&workflow]() -> int {
                    return workflow->GetTotalVials();
                })
        ).forInterface(std::string(service.interface_name));

        std::cout << "Orchestrator service started on D-Bus: "
                  << service.bus_name << "\n";
        connection->enterEventLoop();
    } catch (const std::exception& e) {
        std::cerr << "Orchestrator service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
