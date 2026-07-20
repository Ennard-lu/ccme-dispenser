#include "camera/dissolution_detector.h"
#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <memory>

namespace {

constexpr const char* kBusName = "org.ccme.Camera";
constexpr const char* kObjectPath = "/org/ccme/camera";
constexpr const char* kInterfaceName = "org.ccme.Camera.Control";

}  // namespace

int main() {
    try {
        auto connection = sdbus::createSystemBusConnection(sdbus::ServiceName{kBusName});

        auto detector = std::make_unique<ccme::camera::DissolutionDetector>();

        auto connect_result = detector->Connect();
        if (!connect_result) {
            std::cerr << "Warning: failed to connect to camera stream\n";
        }

        auto object = sdbus::createObject(*connection, sdbus::ObjectPath{kObjectPath});

        object->addVTable(
            sdbus::registerMethod("CheckDissolution").implementedAs([&detector]() -> bool {
                auto result = detector->CheckDissolution();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Dissolution check failed"};
                return *result;
            }),
            sdbus::registerMethod("Connect").implementedAs([&detector]() -> bool {
                auto result = detector->Connect();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Camera connect failed"};
                return *result;
            }),
            sdbus::registerMethod("Disconnect").implementedAs([&detector]() {
                detector->Disconnect();
            })
        ).forInterface(kInterfaceName);

        std::cout << "Camera service started on D-Bus: " << kBusName << "\n";
        connection->enterEventLoop();

    } catch (const std::exception& e) {
        std::cerr << "Camera service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
