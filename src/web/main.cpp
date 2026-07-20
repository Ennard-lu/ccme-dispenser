#include "web/http_server.h"
#include <sdbus-c++/sdbus-c++.h>
#include <iostream>
#include <memory>

namespace {

constexpr const char* kBusName = "org.ccme.Web";
constexpr const char* kObjectPath = "/org/ccme/web";
constexpr const char* kInterfaceName = "org.ccme.Web.Control";

}  // namespace

int main() {
    try {
        auto connection = sdbus::createSystemBusConnection(sdbus::ServiceName{kBusName});

        ccme::web::ServerConfig config;
        config.port = CCME_WEB_HTTP_PORT;
        config.listen_address = CCME_WEB_LISTEN_ADDRESS;

        auto server = std::make_unique<ccme::web::HttpServer>(std::move(config));

        auto start_result = server->Start();
        if (!start_result) {
            std::cerr << "Warning: HTTP server failed to start\n";
        }

        auto object = sdbus::createObject(*connection, sdbus::ObjectPath{kObjectPath});

        object->addVTable(
            sdbus::registerMethod("Start").implementedAs([&server]() -> bool {
                auto result = server->Start();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "HTTP server start failed"};
                return *result;
            }),
            sdbus::registerMethod("Stop").implementedAs([&server]() -> bool {
                auto result = server->Stop();
                if (!result)
                    throw sdbus::Error{sdbus::Error::Name{kInterfaceName}, "HTTP server stop failed"};
                return *result;
            }),
            sdbus::registerMethod("IsRunning").implementedAs([&server]() -> bool {
                return server->IsRunning();
            })
        ).forInterface(kInterfaceName);

        std::cout << "Web service started on D-Bus: " << kBusName << "\n";
        connection->enterEventLoop();

    } catch (const std::exception& e) {
        std::cerr << "Web service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
