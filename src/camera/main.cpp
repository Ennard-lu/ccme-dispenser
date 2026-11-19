#include "camera/dissolution_detector.h"
#include "dbus/ccme_dbus.h"

#include <sdbus-c++/sdbus-c++.h>

#include <iostream>
#include <memory>
#include <string>

int main() {
    try {
        const auto& service = ccme::dbus::kCamera;

        auto connection = ccme::dbus::Connect(service);
        auto detector = std::make_unique<ccme::camera::DissolutionDetector>();

        auto connect_result = detector->Connect();
        if (!connect_result) {
            std::cerr << "[CAMERA] Warning: initial stream connection failed ("
                      << ccme::camera::ToString(connect_result.error())
                      << "), will retry on D-Bus Connect / CheckDissolution\n";
        }

        auto object = ccme::dbus::CreateObject(*connection, service);

        object->addVTable(
            sdbus::registerMethod("CheckDissolution").implementedAs(
                [&detector]() -> bool {
                    std::cerr << "[CAMERA] D-Bus: CheckDissolution\n";
                    auto result = detector->CheckDissolution();
                    if (!result) {
                        std::cerr << "[CAMERA] D-Bus: CheckDissolution failed: "
                                  << ccme::camera::ToString(result.error())
                                  << "\n";
                        ccme::dbus::ThrowError(
                            ccme::dbus::kCamera,
                            std::string{"Dissolution check failed: "} +
                                ccme::camera::ToString(result.error()));
                    }
                    return result->dissolved;
                }),
            sdbus::registerMethod("Connect").implementedAs(
                [&detector]() -> bool {
                    std::cerr << "[CAMERA] D-Bus: Connect\n";
                    auto result = detector->Connect();
                    if (!result) {
                        ccme::dbus::ThrowError(
                            ccme::dbus::kCamera,
                            std::string{"Camera connect failed: "} +
                                ccme::camera::ToString(result.error()));
                    }
                    return *result;
                }),
            sdbus::registerMethod("Disconnect").implementedAs(
                [&detector]() {
                    std::cerr << "[CAMERA] D-Bus: Disconnect\n";
                    detector->Disconnect();
                }),
            sdbus::registerMethod("IsConnected").implementedAs(
                [&detector]() -> bool {
                    return detector->IsConnected();
                }),
            sdbus::registerMethod("IsAiConfigured").implementedAs(
                [&detector]() -> bool {
                    return detector->IsAiConfigured();
                })
        ).forInterface(std::string(service.interface_name));

        std::cout << "Camera service started on D-Bus: "
                  << service.bus_name << "\n";
        connection->enterEventLoop();
    } catch (const std::exception& e) {
        std::cerr << "Camera service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
