#include "stirrer/stirrer_controller.h"
#include "dbus/ccme_dbus.h"

#include <sdbus-c++/sdbus-c++.h>

#include <iostream>
#include <memory>
#include <string>

int main() {
    try {
        const auto& service = ccme::dbus::kStirrer;

        auto connection = ccme::dbus::Connect(service);
        auto stirrer = std::make_unique<ccme::stirrer::StirrerController>();
        auto object = ccme::dbus::CreateObject(*connection, service);

        object->addVTable(
            sdbus::registerMethod("StartStir").implementedAs(
                [&stirrer](int speed) -> bool {
                    std::cerr << "[STIRRER] D-Bus: StartStir(" << speed << ")\n";
                    auto result = stirrer->StartStir(speed);
                    if (!result) {
                        ccme::dbus::ThrowError(ccme::dbus::kStirrer,
                                               "Failed to start stirrer");
                    }
                    return *result;
                }),
            sdbus::registerMethod("StopStir").implementedAs(
                [&stirrer]() -> bool {
                    std::cerr << "[STIRRER] D-Bus: StopStir\n";
                    auto result = stirrer->StopStir();
                    if (!result) {
                        ccme::dbus::ThrowError(ccme::dbus::kStirrer,
                                               "Failed to stop stirrer");
                    }
                    return *result;
                }),
            sdbus::registerMethod("IsStirring").implementedAs(
                [&stirrer]() -> bool {
                    return stirrer->IsStirring();
                }),
            sdbus::registerMethod("StartHeat").implementedAs(
                [&stirrer](double temp_c) -> bool {
                    std::cerr << "[STIRRER] D-Bus: StartHeat(" << temp_c << ")\n";
                    auto result = stirrer->StartHeat(temp_c);
                    if (!result) {
                        ccme::dbus::ThrowError(ccme::dbus::kStirrer,
                                               "Failed to start heating");
                    }
                    return *result;
                }),
            sdbus::registerMethod("StopHeat").implementedAs(
                [&stirrer]() -> bool {
                    std::cerr << "[STIRRER] D-Bus: StopHeat\n";
                    auto result = stirrer->StopHeat();
                    if (!result) {
                        ccme::dbus::ThrowError(ccme::dbus::kStirrer,
                                               "Failed to stop heating");
                    }
                    return *result;
                }),
            sdbus::registerMethod("IsHeating").implementedAs(
                [&stirrer]() -> bool {
                    return stirrer->IsHeating();
                }),
            sdbus::registerMethod("GetSpeed").implementedAs(
                [&stirrer]() -> int {
                    return stirrer->GetSpeed();
                }),
            sdbus::registerMethod("GetSetTemp").implementedAs(
                [&stirrer]() -> double {
                    return stirrer->GetSetTemp();
                }),
            sdbus::registerMethod("GetActualSpeed").implementedAs(
                [&stirrer]() -> int {
                    std::cerr << "[STIRRER] D-Bus: GetActualSpeed\n";
                    auto result = stirrer->GetActualSpeed();
                    if (!result) {
                        ccme::dbus::ThrowError(
                            ccme::dbus::kStirrer,
                            "Failed to read actual speed");
                    }
                    return *result;
                }),
            sdbus::registerMethod("GetActualTemp").implementedAs(
                [&stirrer]() -> double {
                    std::cerr << "[STIRRER] D-Bus: GetActualTemp\n";
                    auto result = stirrer->GetActualTemp();
                    if (!result) {
                        ccme::dbus::ThrowError(
                            ccme::dbus::kStirrer,
                            "Failed to read actual temperature");
                    }
                    return *result;
                }),
            sdbus::registerMethod("GetExternalTemp").implementedAs(
                [&stirrer]() -> double {
                    std::cerr << "[STIRRER] D-Bus: GetExternalTemp\n";
                    auto result = stirrer->GetExternalTemp();
                    if (!result) {
                        ccme::dbus::ThrowError(
                            ccme::dbus::kStirrer,
                            "Failed to read external temperature");
                    }
                    return *result;
                })
        ).forInterface(std::string(service.interface_name));

        std::cout << "Stirrer service started on D-Bus: " << service.bus_name
                  << "\n";
        connection->enterEventLoop();
    } catch (const std::exception& e) {
        std::cerr << "Stirrer service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
