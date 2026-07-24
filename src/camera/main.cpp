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
            std::cerr << "[CAMERA] Warning: initial connection failed, will retry on D-Bus Connect\n";
        }

        auto object = sdbus::createObject(*connection, sdbus::ObjectPath{kObjectPath});

        object->addVTable(
            sdbus::registerMethod("CheckDissolution").implementedAs([&detector]() -> bool {
                std::cerr << "[CAMERA] D-Bus: CheckDissolution\n";
                auto result = detector->CheckDissolution();
                if (!result) {
                    std::cerr << "[CAMERA] D-Bus: CheckDissolution failed\n";
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Dissolution check failed"};
                }
                return *result;
            }),
            sdbus::registerMethod("Connect").implementedAs([&detector]() -> bool {
                std::cerr << "[CAMERA] D-Bus: Connect\n";
                auto result = detector->Connect();
                if (!result) {
                    std::cerr << "[CAMERA] D-Bus: Connect failed\n";
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "Camera connect failed"};
                }
                return *result;
            }),
            sdbus::registerMethod("Disconnect").implementedAs([&detector]() {
                std::cerr << "[CAMERA] D-Bus: Disconnect\n";
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
