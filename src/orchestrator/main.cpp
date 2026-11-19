#include "orchestrator/workflow.h"
#include "dbus/ccme_dbus.h"

#include <sdbus-c++/sdbus-c++.h>

#include <iostream>
#include <memory>
#include <string>

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
                        ccme::dbus::ThrowError(
                            ccme::dbus::kOrchestrator,
                            "Workflow start failed");
                    }
                    return *result;
                }),
            sdbus::registerMethod("Stop").implementedAs([&workflow]() -> bool {
                std::cerr << "[ORCH] D-Bus: Stop\n";
                auto result = workflow->Stop();
                if (!result) {
                    ccme::dbus::ThrowError(ccme::dbus::kOrchestrator,
                                           "Workflow stop failed");
                }
                return *result;
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
