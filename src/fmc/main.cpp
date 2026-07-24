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
            std::cerr << "[FMC] Warning: initial connection failed, will retry on D-Bus Connect\n";
        }

        auto object = sdbus::createObject(*connection, sdbus::ObjectPath{kObjectPath});

        object->addVTable(
            sdbus::registerMethod("Connect").implementedAs([&fmc]() -> bool {
                std::cerr << "[FMC] D-Bus: Connect\n";
                auto result = fmc->Connect();
                if (!result) {
                    std::cerr << "[FMC] D-Bus: Connect failed\n";
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "FMC connect failed"};
                }
                return *result;
            }),
            sdbus::registerMethod("Home").implementedAs([&fmc]() -> bool {
                std::cerr << "[FMC] D-Bus: Home\n";
                auto result = fmc->Home();
                if (!result) {
                    std::cerr << "[FMC] D-Bus: Home failed\n";
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "FMC home failed"};
                }
                return *result;
            }),
            sdbus::registerMethod("MoveTo").implementedAs([&fmc](double x, double y) -> bool {
                std::cerr << "[FMC] D-Bus: MoveTo(" << x << ", " << y << ")\n";
                auto result = fmc->MoveTo(static_cast<float>(x), static_cast<float>(y));
                if (!result) {
                    std::cerr << "[FMC] D-Bus: MoveTo failed\n";
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "FMC move failed"};
                }
                return *result;
            }),
            sdbus::registerMethod("MoveToVial").implementedAs([&fmc](int index) -> bool {
                std::cerr << "[FMC] D-Bus: MoveToVial(" << index << ")\n";
                auto result = fmc->MoveToVial(index);
                if (!result) {
                    std::cerr << "[FMC] D-Bus: MoveToVial failed\n";
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "FMC move to vial failed"};
                }
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
