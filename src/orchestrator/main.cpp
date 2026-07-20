#include "orchestrator/workflow.h"
#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <memory>

namespace {

constexpr const char* kBusName = "org.ccme.Orchestrator";
constexpr const char* kObjectPath = "/org/ccme/orchestrator";
constexpr const char* kInterfaceName = "org.ccme.Orchestrator.Control";

}  // namespace

int main() {
    try {
        auto connection = sdbus::createSystemBusConnection(sdbus::ServiceName{kBusName});

        auto workflow = std::make_unique<ccme::orchestrator::Workflow>();

        auto object = sdbus::createObject(*connection, sdbus::ObjectPath{kObjectPath});

        object->addVTable(
            sdbus::registerMethod("Start").implementedAs([&workflow](double volume_ml) -> bool {
                auto result = workflow->Start(volume_ml);
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Workflow start failed"};
                return *result;
            }),
            sdbus::registerMethod("Stop").implementedAs([&workflow]() -> bool {
                auto result = workflow->Stop();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Workflow stop failed"};
                return *result;
            }),
            sdbus::registerMethod("GetState").implementedAs([&workflow]() -> std::string {
                return workflow->GetStateString();
            }),
            sdbus::registerMethod("GetCurrentVial").implementedAs([&workflow]() -> int {
                return workflow->GetCurrentVial();
            }),
            sdbus::registerMethod("GetTotalVials").implementedAs([&workflow]() -> int {
                return workflow->GetTotalVials();
            })
        ).forInterface(kInterfaceName);

        std::cout << "Orchestrator service started on D-Bus: " << kBusName << "\n";
        connection->enterEventLoop();

    } catch (const std::exception& e) {
        std::cerr << "Orchestrator service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
