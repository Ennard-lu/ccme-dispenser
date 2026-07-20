#include "fmc/fmc_controller.h"
#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <memory>

namespace {

constexpr const char* kBusName = "org.ccme.FMC";
constexpr const char* kObjectPath = "/org/ccme/fmc";
constexpr const char* kInterfaceName = "org.ccme.FMC.Control";

}  // namespace

int main() {
    try {
        auto connection = sdbus::createSystemBusConnection(sdbus::ServiceName{kBusName});

        auto fmc = std::make_unique<ccme::fmc::FmcController>();

        auto connect_result = fmc->Connect();
        if (!connect_result) {
            std::cerr << "Warning: failed to connect to FMC4030 controller\n";
        }

        auto object = sdbus::createObject(*connection, sdbus::ObjectPath{kObjectPath});

        object->addVTable(
            sdbus::registerMethod("Connect").implementedAs([&fmc]() -> bool {
                auto result = fmc->Connect();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "FMC connect failed"};
                return *result;
            }),
            sdbus::registerMethod("Home").implementedAs([&fmc]() -> bool {
                auto result = fmc->Home();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "FMC home failed"};
                return *result;
            }),
            sdbus::registerMethod("MoveTo").implementedAs([&fmc](double x, double y) -> bool {
                auto result = fmc->MoveTo(static_cast<float>(x), static_cast<float>(y));
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "FMC move failed"};
                return *result;
            }),
            sdbus::registerMethod("MoveToVial").implementedAs([&fmc](int index) -> bool {
                auto result = fmc->MoveToVial(index);
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "FMC move to vial failed"};
                return *result;
            }),
            sdbus::registerMethod("IsMoving").implementedAs([&fmc]() -> bool {
                return fmc->IsMoving();
            })
        ).forInterface(kInterfaceName);

        std::cout << "FMC service started on D-Bus: " << kBusName << "\n";
        connection->enterEventLoop();

    } catch (const std::exception& e) {
        std::cerr << "FMC service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
