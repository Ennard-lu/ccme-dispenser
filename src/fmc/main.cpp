#include "fmc/fmc_controller.h"
#include "dbus/ccme_dbus.h"

#include <sdbus-c++/sdbus-c++.h>

#include <iostream>
#include <memory>
#include <string>

int main() {
    try {
        const auto& service = ccme::dbus::kFmc;

        auto connection = ccme::dbus::Connect(service);
        auto fmc = std::make_unique<ccme::fmc::FmcController>();

        auto connect_result = fmc->Connect();
        if (!connect_result) {
            std::cerr << "[FMC] Warning: initial connection failed, "
                         "will retry on D-Bus Connect\n";
        }

        auto object = ccme::dbus::CreateObject(*connection, service);

        object->addVTable(
            sdbus::registerMethod("Connect").implementedAs([&fmc]() -> bool {
                std::cerr << "[FMC] D-Bus: Connect\n";
                auto result = fmc->Connect();
                if (!result) {
                    ccme::dbus::ThrowError(ccme::dbus::kFmc,
                                           "FMC connect failed");
                }
                return *result;
            }),
            sdbus::registerMethod("Home").implementedAs([&fmc]() -> bool {
                std::cerr << "[FMC] D-Bus: Home\n";
                auto result = fmc->Home();
                if (!result) {
                    ccme::dbus::ThrowError(ccme::dbus::kFmc,
                                           "FMC home failed");
                }
                return *result;
            }),
            sdbus::registerMethod("MoveTo").implementedAs(
                [&fmc](double x, double y) -> bool {
                    std::cerr << "[FMC] D-Bus: MoveTo(" << x << ", " << y
                              << ")\n";
                    auto result =
                        fmc->MoveTo(static_cast<float>(x),
                                    static_cast<float>(y));
                    if (!result) {
                        ccme::dbus::ThrowError(ccme::dbus::kFmc,
                                               "FMC move failed");
                    }
                    return *result;
                }),
            sdbus::registerMethod("MoveToVial").implementedAs(
                [&fmc](int index) -> bool {
                    std::cerr << "[FMC] D-Bus: MoveToVial(" << index << ")\n";
                    auto result = fmc->MoveToVial(index);
                    if (!result) {
                        ccme::dbus::ThrowError(ccme::dbus::kFmc,
                                               "FMC move to vial failed");
                    }
                    return *result;
                }),
            sdbus::registerMethod("IsMoving").implementedAs([&fmc]() -> bool {
                return fmc->IsMoving();
            }),
            sdbus::registerMethod("IsConnected").implementedAs(
                [&fmc]() -> bool {
                    return fmc->IsConnected();
                })
        ).forInterface(std::string(service.interface_name));

        std::cout << "FMC service started on D-Bus: " << service.bus_name
                  << "\n";
        connection->enterEventLoop();
    } catch (const std::exception& e) {
        std::cerr << "FMC service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
