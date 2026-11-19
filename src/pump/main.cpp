#include "pump/pump_controller.h"
#include "dbus/ccme_dbus.h"

#include <sdbus-c++/sdbus-c++.h>

#include <iostream>
#include <memory>
#include <string>

int main() {
    try {
        const auto& service = ccme::dbus::kPump;

        auto connection = ccme::dbus::Connect(service);

        auto pump1 = std::make_unique<ccme::pump::PumpController>(
            ccme::pump::PumpId::kPump1);
        auto pump2 = std::make_unique<ccme::pump::PumpController>(
            ccme::pump::PumpId::kPump2);

        auto object = ccme::dbus::CreateObject(*connection, service);

        object->addVTable(
            sdbus::registerMethod("Start").implementedAs(
                [&pump1](double volume_ml) -> bool {
                    std::cerr << "[PUMP1] D-Bus: Start(" << volume_ml << ")\n";
                    auto result = pump1->Start(volume_ml);
                    if (!result) {
                        ccme::dbus::ThrowError(ccme::dbus::kPump,
                                               "Failed to start pump");
                    }
                    return *result;
                }),
            sdbus::registerMethod("Stop").implementedAs([&pump1]() -> bool {
                std::cerr << "[PUMP1] D-Bus: Stop\n";
                auto result = pump1->Stop();
                if (!result) {
                    ccme::dbus::ThrowError(ccme::dbus::kPump,
                                           "Failed to stop pump");
                }
                return *result;
            }),
            sdbus::registerMethod("IsRunning").implementedAs([&pump1]() -> bool {
                return pump1->IsRunning();
            }),
            sdbus::registerMethod("Start2").implementedAs(
                [&pump2](double volume_ml) -> bool {
                    std::cerr << "[PUMP2] D-Bus: Start2(" << volume_ml << ")\n";
                    auto result = pump2->Start(volume_ml);
                    if (!result) {
                        ccme::dbus::ThrowError(ccme::dbus::kPump,
                                               "Failed to start pump 2");
                    }
                    return *result;
                }),
            sdbus::registerMethod("Stop2").implementedAs([&pump2]() -> bool {
                std::cerr << "[PUMP2] D-Bus: Stop2\n";
                auto result = pump2->Stop();
                if (!result) {
                    ccme::dbus::ThrowError(ccme::dbus::kPump,
                                           "Failed to stop pump 2");
                }
                return *result;
            }),
            sdbus::registerMethod("IsRunning2").implementedAs(
                [&pump2]() -> bool {
                    return pump2->IsRunning();
                })
        ).forInterface(std::string(service.interface_name));

        std::cout << "Pump service started on D-Bus: " << service.bus_name
                  << "\n";
        connection->enterEventLoop();
    } catch (const std::exception& e) {
        std::cerr << "Pump service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
