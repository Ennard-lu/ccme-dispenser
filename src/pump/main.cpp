#include "pump/pump_controller.h"
#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <memory>

namespace {

constexpr const char* kBusName = "org.ccme.Pump";
constexpr const char* kObjectPath = "/org/ccme/pump";
constexpr const char* kInterfaceName = "org.ccme.Pump.Control";

}  // namespace

int main() {
    try {
        auto connection = sdbus::createSystemBusConnection(sdbus::ServiceName{kBusName});

        auto pump1 = std::make_unique<ccme::pump::PumpController>(
            ccme::pump::PumpId::kPump1);
        auto pump2 = std::make_unique<ccme::pump::PumpController>(
            ccme::pump::PumpId::kPump2);

        auto object = sdbus::createObject(*connection, sdbus::ObjectPath{kObjectPath});

        object->addVTable(
            sdbus::registerMethod("Start").implementedAs([&pump1](double volume_ml) -> bool {
                auto result = pump1->Start(volume_ml);
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Failed to start pump"};
                return *result;
            }),
            sdbus::registerMethod("Stop").implementedAs([&pump1]() -> bool {
                auto result = pump1->Stop();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Failed to stop pump"};
                return *result;
            }),
            sdbus::registerMethod("IsRunning").implementedAs([&pump1]() -> bool {
                return pump1->IsRunning();
            }),
            sdbus::registerMethod("Start2").implementedAs([&pump2](double volume_ml) -> bool {
                auto result = pump2->Start(volume_ml);
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Failed to start pump 2"};
                return *result;
            }),
            sdbus::registerMethod("Stop2").implementedAs([&pump2]() -> bool {
                auto result = pump2->Stop();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Failed to stop pump 2"};
                return *result;
            }),
            sdbus::registerMethod("IsRunning2").implementedAs([&pump2]() -> bool {
                return pump2->IsRunning();
            })
        ).forInterface(kInterfaceName);

        std::cout << "Pump service started on D-Bus: " << kBusName << "\n";
        connection->enterEventLoop();

    } catch (const std::exception& e) {
        std::cerr << "Pump service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
