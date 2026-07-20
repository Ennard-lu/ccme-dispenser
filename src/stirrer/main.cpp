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
            sdbus::registerMethod("IsRunning").implementedAs([&stirrer]() -> bool {
                return stirrer->IsRunning();
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
