#include "web/http_server.h"
#include <sdbus-c++/sdbus-c++.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <csignal>

namespace {

constexpr const char* kBusName = "org.ccme.Web";
constexpr const char* kObjectPath = "/org/ccme/web";
constexpr const char* kInterfaceName = "org.ccme.Web.Control";

constexpr const char* kOrchestratorBus = "org.ccme.Orchestrator";
constexpr const char* kOrchestratorPath = "/org/ccme/orchestrator";
constexpr const char* kOrchestratorIface = "org.ccme.Orchestrator.Control";

constexpr const char* kPumpBus = "org.ccme.Pump";
constexpr const char* kPumpPath = "/org/ccme/pump";
constexpr const char* kPumpIface = "org.ccme.Pump.Control";

constexpr const char* kStirrerBus = "org.ccme.Stirrer";
constexpr const char* kStirrerPath = "/org/ccme/stirrer";
constexpr const char* kStirrerIface = "org.ccme.Stirrer.Control";

constexpr const char* kCameraBus = "org.ccme.Camera";
constexpr const char* kCameraPath = "/org/ccme/camera";
constexpr const char* kCameraIface = "org.ccme.Camera.Control";

constexpr const char* kFmcBus = "org.ccme.FMC";
constexpr const char* kFmcPath = "/org/ccme/fmc";
constexpr const char* kFmcIface = "org.ccme.FMC.Control";

std::atomic<bool> g_running{true};

void SignalHandler(int) {
    g_running = false;
}

}  // namespace

int main() {
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    try {
        auto connection = sdbus::createSystemBusConnection(
            sdbus::ServiceName{kBusName});

        std::mutex status_mutex;
        nlohmann::json cached_status = {
            {"state", "idle"},
            {"current_vial", 0},
            {"total_vials", CCME_VIAL_ROWS * CCME_VIAL_COLS},
            {"modules", {
                {"pump", {{"running", false}}},
                {"pump2", {{"running", false}}},
                {"stirrer", {{"running", false}}},
                {"camera", {{"connected", false}}},
                {"fmc", {{"connected", false}, {"moving", false}}}
            }}
        };

        ccme::web::ServerConfig config;
        config.port = CCME_WEB_HTTP_PORT;
        config.listen_address = CCME_WEB_LISTEN_ADDRESS;
        config.stream_url = CCME_CAMERA_STREAM_URL;

        auto server = std::make_unique<ccme::web::HttpServer>(config);

        server->SetStatusCallback([&]() {
            std::lock_guard<std::mutex> lock(status_mutex);
            return cached_status.dump();
        });

        server->SetStartCallback([&](double volume_ml) -> bool {
            try {
                auto proxy = sdbus::createProxy(
                    *connection,
                    sdbus::ServiceName{kOrchestratorBus},
                    sdbus::ObjectPath{kOrchestratorPath});
                proxy->callMethod("Start")
                    .onInterface(kOrchestratorIface)
                    .withArguments(volume_ml);
                return true;
            } catch (const std::exception& e) {
                std::cerr << "Start failed: " << e.what() << "\n";
                return false;
            }
        });

        server->SetStopCallback([&]() -> bool {
            try {
                auto proxy = sdbus::createProxy(
                    *connection,
                    sdbus::ServiceName{kOrchestratorBus},
                    sdbus::ObjectPath{kOrchestratorPath});
                proxy->callMethod("Stop")
                    .onInterface(kOrchestratorIface);
                return true;
            } catch (const std::exception& e) {
                std::cerr << "Stop failed: " << e.what() << "\n";
                return false;
            }
        });

        auto start_result = server->Start();
        if (!start_result) {
            std::cerr << "HTTP server failed to start\n";
            return 1;
        }

        std::thread poller_thread([&]() {
            auto poll_conn = sdbus::createSystemBusConnection();

            while (g_running) {
                nlohmann::json status;
                status["current_vial"] = 0;
                status["total_vials"] = CCME_VIAL_ROWS * CCME_VIAL_COLS;

                try {
                    auto proxy = sdbus::createProxy(
                        *poll_conn,
                        sdbus::ServiceName{kOrchestratorBus},
                        sdbus::ObjectPath{kOrchestratorPath});

                    std::string state;
                    proxy->callMethod("GetState")
                        .onInterface(kOrchestratorIface)
                        .storeResultsTo(state);
                    status["state"] = state;

                    int cv = 0;
                    proxy->callMethod("GetCurrentVial")
                        .onInterface(kOrchestratorIface)
                        .storeResultsTo(cv);
                    status["current_vial"] = cv;

                    int tv = 0;
                    proxy->callMethod("GetTotalVials")
                        .onInterface(kOrchestratorIface)
                        .storeResultsTo(tv);
                    status["total_vials"] = tv;
                } catch (...) {
                    status["state"] = "unknown";
                }

                try {
                    auto proxy = sdbus::createProxy(
                        *poll_conn,
                        sdbus::ServiceName{kPumpBus},
                        sdbus::ObjectPath{kPumpPath});
                    bool r1 = false;
                    proxy->callMethod("IsRunning")
                        .onInterface(kPumpIface)
                        .storeResultsTo(r1);
                    status["modules"]["pump"] = {{"running", r1}};

                    bool r2 = false;
                    proxy->callMethod("IsRunning2")
                        .onInterface(kPumpIface)
                        .storeResultsTo(r2);
                    status["modules"]["pump2"] = {{"running", r2}};
                } catch (...) {
                    status["modules"]["pump"] = {{"running", false}};
                    status["modules"]["pump2"] = {{"running", false}};
                }

                try {
                    auto proxy = sdbus::createProxy(
                        *poll_conn,
                        sdbus::ServiceName{kStirrerBus},
                        sdbus::ObjectPath{kStirrerPath});
                    bool r = false;
                    proxy->callMethod("IsRunning")
                        .onInterface(kStirrerIface)
                        .storeResultsTo(r);
                    status["modules"]["stirrer"] = {{"running", r}};
                } catch (...) {
                    status["modules"]["stirrer"] = {{"running", false}};
                }

                status["modules"]["camera"] = {{"connected", true}};

                try {
                    auto proxy = sdbus::createProxy(
                        *poll_conn,
                        sdbus::ServiceName{kFmcBus},
                        sdbus::ObjectPath{kFmcPath});
                    bool m = false;
                    proxy->callMethod("IsMoving")
                        .onInterface(kFmcIface)
                        .storeResultsTo(m);
                    status["modules"]["fmc"] = {
                        {"connected", true}, {"moving", m}};
                } catch (...) {
                    status["modules"]["fmc"] = {
                        {"connected", false}, {"moving", false}};
                }

                {
                    std::lock_guard<std::mutex> lock(status_mutex);
                    cached_status = status;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });

        std::thread dbus_thread([&connection]() {
            connection->enterEventLoop();
        });

        std::cout << "Web service started\n";
        std::cout << "  D-Bus: " << kBusName << "\n";
        std::cout << "  HTTP:  http://0.0.0.0:" << config.port << "\n";

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        server->Stop();
        g_running = false;

        if (poller_thread.joinable()) poller_thread.join();
        if (dbus_thread.joinable()) dbus_thread.join();

    } catch (const std::exception& e) {
        std::cerr << "Web service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
