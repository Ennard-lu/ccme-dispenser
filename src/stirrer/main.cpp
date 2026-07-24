#include "stirrer/stirrer_controller.h"
#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <memory>

namespace {

constexpr const char* kBusName = "org.ccme.Stirrer";
constexpr const char* kObjectPath = "/org/ccme/stirrer";
constexpr const char* kInterfaceName = "org.ccme.Stirrer.Control";

}  // namespace

int main() {
    try {
        auto connection = sdbus::createSystemBusConnection(sdbus::ServiceName{kBusName});

        auto stirrer = std::make_unique<ccme::stirrer::StirrerController>();

        auto object = sdbus::createObject(*connection, sdbus::ObjectPath{kObjectPath});

        object->addVTable(
            sdbus::registerMethod("StartStir").implementedAs([&stirrer](int speed) -> bool {
                auto result = stirrer->StartStir(speed);
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Failed to start stirrer"};
                return *result;
            }),
            sdbus::registerMethod("StopStir").implementedAs([&stirrer]() -> bool {
                auto result = stirrer->StopStir();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Failed to stop stirrer"};
                return *result;
            }),
            sdbus::registerMethod("IsStirring").implementedAs([&stirrer]() -> bool {
                return stirrer->IsStirring();
            }),
            sdbus::registerMethod("StartHeat").implementedAs([&stirrer](double temp_c) -> bool {
                auto result = stirrer->StartHeat(temp_c);
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Failed to start heating"};
                return *result;
            }),
            sdbus::registerMethod("StopHeat").implementedAs([&stirrer]() -> bool {
                auto result = stirrer->StopHeat();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Failed to stop heating"};
                return *result;
            }),
            sdbus::registerMethod("IsHeating").implementedAs([&stirrer]() -> bool {
                return stirrer->IsHeating();
            }),
            sdbus::registerMethod("GetSpeed").implementedAs([&stirrer]() -> int {
                return stirrer->GetSpeed();
            }),
            sdbus::registerMethod("GetSetTemp").implementedAs([&stirrer]() -> double {
                return stirrer->GetSetTemp();
            }),
            sdbus::registerMethod("GetActualSpeed").implementedAs([&stirrer]() -> int {
                auto result = stirrer->GetActualSpeed();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Failed to read actual speed"};
                return *result;
            }),
            sdbus::registerMethod("GetActualTemp").implementedAs([&stirrer]() -> double {
                auto result = stirrer->GetActualTemp();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Failed to read actual temperature"};
                return *result;
            }),
            sdbus::registerMethod("GetExternalTemp").implementedAs([&stirrer]() -> double {
                auto result = stirrer->GetExternalTemp();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Failed to read external temperature"};
                return *result;
            })
        ).forInterface(kInterfaceName);

        std::cout << "Stirrer service started on D-Bus: " << kBusName << "\n";
        connection->enterEventLoop();

    } catch (const std::exception& e) {
        std::cerr << "Stirrer service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
