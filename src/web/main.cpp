#include "web/http_server.h"
#include "dbus/ccme_dbus.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_running{true};

void SignalHandler(int) {
    std::cerr << "[WEB] Signal received, shutting down\n";
    g_running = false;
}

}  // namespace

int main() {
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    try {
        std::mutex status_mutex;
        nlohmann::json cached_status = {
            {"state", "idle"},
            {"current_vial", 0},
            {"total_vials", CCME_VIAL_ROWS * CCME_VIAL_COLS},
            {"modules", {
                {"pump", {{"running", false}}},
                {"pump2", {{"running", false}}},
                {"stirrer", {{"running", false}}},
                {"camera", {{"connected", false}, {"ai_configured", false}}},
                {"fmc", {{"connected", false}, {"moving", false}}},
            }},
        };

        auto control = std::make_shared<ccme::dbus::Client>();
        auto poller = std::make_shared<ccme::dbus::Client>();

        ccme::web::ServerConfig config;
        config.port = CCME_WEB_HTTP_PORT;
        config.listen_address = CCME_WEB_LISTEN_ADDRESS;
        config.stream_url = CCME_CAMERA_STREAM_URL;

        auto server = std::make_unique<ccme::web::HttpServer>(config);

        server->SetStatusCallback([&]() {
            std::lock_guard<std::mutex> lock(status_mutex);
            return cached_status.dump();
        });

        server->SetStartCallback(
            [control](const ccme::web::StartParams& params) -> bool {
                std::cerr << "[WEB] Start callback: volume=" << params.volume_ml
                          << "ml stir=" << params.stir_speed_rpm
                          << "RPM temp=" << params.heat_temp_c
                          << "C dispense=" << params.dispense_volume_ml
                          << "ml\n";
                auto result = control->Call(
                    ccme::dbus::kOrchestrator, "Start", params.volume_ml,
                    params.stir_speed_rpm, params.heat_temp_c,
                    params.dispense_volume_ml);
                if (!result) {
                    std::cerr << "[WEB] Start failed: "
                              << result.error().message << "\n";
                    return false;
                }
                std::cerr << "[WEB] Start forwarded to orchestrator\n";
                return true;
            });

        server->SetStopCallback([control]() -> bool {
            std::cerr << "[WEB] Stop callback\n";
            auto result =
                control->Call(ccme::dbus::kOrchestrator, "Stop");
            if (!result) {
                std::cerr << "[WEB] Stop failed: "
                          << result.error().message << "\n";
                return false;
            }
            std::cerr << "[WEB] Stop forwarded to orchestrator\n";
            return true;
        });

        auto start_result = server->Start();
        if (!start_result) {
            std::cerr << "[WEB] HTTP server failed to start\n";
            return 1;
        }

        std::thread poller_thread(
            [poller, &status_mutex, &cached_status]() {
                std::cerr << "[WEB] Status poller started\n";

                while (g_running) {
                    nlohmann::json status;
                    status["current_vial"] = 0;
                    status["total_vials"] =
                        CCME_VIAL_ROWS * CCME_VIAL_COLS;
                    status["state"] = "unknown";
                    status["modules"] = nlohmann::json::object();

                    if (auto state = poller->CallWithResult<std::string>(
                            ccme::dbus::kOrchestrator, "GetState")) {
                        status["state"] = *state;
                    }
                    if (auto current = poller->CallWithResult<int>(
                            ccme::dbus::kOrchestrator, "GetCurrentVial")) {
                        status["current_vial"] = *current;
                    }
                    if (auto total = poller->CallWithResult<int>(
                            ccme::dbus::kOrchestrator, "GetTotalVials")) {
                        status["total_vials"] = *total;
                    }

                    if (auto running = poller->CallWithResult<bool>(
                            ccme::dbus::kPump, "IsRunning")) {
                        status["modules"]["pump"] = {
                            {"running", *running}};
                    } else {
                        status["modules"]["pump"] = nullptr;
                    }
                    if (auto running = poller->CallWithResult<bool>(
                            ccme::dbus::kPump, "IsRunning2")) {
                        status["modules"]["pump2"] = {
                            {"running", *running}};
                    } else {
                        status["modules"]["pump2"] = nullptr;
                    }

                    auto stirring = poller->CallWithResult<bool>(
                        ccme::dbus::kStirrer, "IsStirring");
                    if (!stirring) {
                        status["modules"]["stirrer"] = nullptr;
                    } else {
                        const bool heating = poller
                            ->CallWithResult<bool>(
                                ccme::dbus::kStirrer, "IsHeating")
                            .value_or(false);
                        const int speed = poller
                            ->CallWithResult<int>(
                                ccme::dbus::kStirrer, "GetSpeed")
                            .value_or(0);
                        const double set_temp = poller
                            ->CallWithResult<double>(
                                ccme::dbus::kStirrer, "GetSetTemp")
                            .value_or(0.0);
                        const double temp = poller
                            ->CallWithResult<double>(
                                ccme::dbus::kStirrer, "GetActualTemp")
                            .value_or(0.0);
                        status["modules"]["stirrer"] = {
                            {"stirring", *stirring},
                            {"heating", heating},
                            {"speed", speed},
                            {"set_temp", set_temp},
                            {"temp", temp},
                        };
                    }

                    auto camera_connected = poller->CallWithResult<bool>(
                        ccme::dbus::kCamera, "IsConnected");
                    if (!camera_connected) {
                        status["modules"]["camera"] = nullptr;
                    } else {
                        const bool ai_configured = poller
                            ->CallWithResult<bool>(
                                ccme::dbus::kCamera, "IsAiConfigured")
                            .value_or(false);
                        status["modules"]["camera"] = {
                            {"connected", *camera_connected},
                            {"ai_configured", ai_configured},
                        };
                    }

                    if (auto moving = poller->CallWithResult<bool>(
                            ccme::dbus::kFmc, "IsMoving")) {
                        status["modules"]["fmc"] = {
                            {"connected", true}, {"moving", *moving}};
                    } else {
                        status["modules"]["fmc"] = nullptr;
                    }

                    {
                        std::lock_guard<std::mutex> lock(status_mutex);
                        cached_status = std::move(status);
                    }

                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(500));
                }
                std::cerr << "[WEB] Status poller stopped\n";
            });

        std::cout << "Web service started\n";
        std::cout << "  HTTP:  http://0.0.0.0:" << config.port << "\n";
        std::cerr << "[WEB] Service started: HTTP=0.0.0.0:" << config.port
                  << "\n";

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cerr << "[WEB] Shutting down...\n";
        (void)server->Stop();
        g_running = false;

        if (poller_thread.joinable()) {
            poller_thread.join();
        }

        std::cerr << "[WEB] Shutdown complete\n";
    } catch (const std::exception& e) {
        std::cerr << "Web service failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
