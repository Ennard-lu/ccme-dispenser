#include "web/http_server.h"
#include "dbus/ccme_dbus.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

std::atomic<bool> g_running{true};

void SignalHandler(int) {
    std::cerr << "[WEB] Signal received, shutting down\n";
    g_running = false;
}

[[nodiscard]] std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

[[nodiscard]] std::string ClassifyWorkflowError(
    const std::string& message, std::string_view fallback_code) {
    if (message.find("already running") != std::string::npos) {
        return "already_running";
    }
    if (message.find("not running") != std::string::npos) {
        return "not_running";
    }
    if (message.find("reset is only allowed") != std::string::npos) {
        return "not_in_error";
    }
    if (message.find("ServiceUnknown") != std::string::npos ||
        message.find("not provided by any .service files") !=
            std::string::npos ||
        message.find("system bus is not connected") != std::string::npos) {
        return "orchestrator_unavailable";
    }
    return std::string{fallback_code};
}

[[nodiscard]] nlohmann::json MakeErrorJson(std::string code,
                                           std::string message,
                                           std::string hint,
                                           std::string module,
                                           bool recoverable = true) {
    return {
        {"active", true},
        {"code", std::move(code)},
        {"message", std::move(message)},
        {"hint", std::move(hint)},
        {"module", std::move(module)},
        {"stage", ""},
        {"recoverable", recoverable},
        {"timestamp_ms", NowMs()},
    };
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
            {"error", nullptr},
            {"modules", {
                {"pump", {{"online", true}, {"running", false}, {"error", ""}}},
                {"pump2", {{"online", true}, {"running", false}, {"error", ""}}},
                {"stirrer", {{"online", true}, {"stirring", false},
                             {"heating", false}, {"speed", 0},
                             {"set_temp", 0.0}, {"temp", 0.0},
                             {"error", ""}}},
                {"camera", {{"online", true}, {"connected", false},
                            {"ai_configured", false}, {"error", ""}}},
                {"fmc", {{"online", true}, {"connected", false},
                         {"moving", false}, {"error", ""}}},
            }},
        };

        auto control = std::make_shared<ccme::dbus::Client>();
        auto poller = std::make_shared<ccme::dbus::Client>();

        ccme::web::ServerConfig config;
        config.port = CCME_WEB_HTTP_PORT;
        config.listen_address = CCME_WEB_LISTEN_ADDRESS;
        config.stream_url = CCME_CAMERA_STREAM_URL;

        auto server = std::make_unique<ccme::web::HttpServer>(config);

        const auto update_cached_control_state = [&](const char* state) {
            std::lock_guard<std::mutex> lock(status_mutex);
            cached_status["state"] = state;
            cached_status["error"] = nullptr;
            if (std::string_view{state} == "idle") {
                cached_status["current_vial"] = 0;
            }
        };

        server->SetStatusCallback([&]() {
            std::lock_guard<std::mutex> lock(status_mutex);
            return cached_status.dump();
        });

        server->SetStartCallback(
            [control, &update_cached_control_state](
                const ccme::web::StartParams& params)
                -> std::expected<void, ccme::web::ApiError> {
                std::cerr << "[WEB] Start callback: volume=" << params.volume_ml
                          << "ml stir=" << params.stir_speed_rpm
                          << "RPM temp=" << params.heat_temp_c
                          << "C dispense=" << params.dispense_volume_ml
                          << "ml\n";
                auto result =
                    control->Call(ccme::dbus::kOrchestrator, "Start",
                                  params.volume_ml, params.stir_speed_rpm,
                                  params.heat_temp_c,
                                  params.dispense_volume_ml);
                if (!result) {
                    const std::string message = result.error().message;
                    const std::string code =
                        ClassifyWorkflowError(message, "start_failed");
                    std::string hint =
                        "查看系统错误提示和 ccme-dispenser 日志";
                    if (code == "already_running") {
                        hint = "等待当前流程结束，或先点击停止";
                    } else if (code == "orchestrator_unavailable") {
                        hint = "检查 ccme-dispenser 是否运行以及 D-Bus 权限";
                    }
                    return std::unexpected(ccme::web::ApiError{
                        code, message, std::move(hint)});
                }
                std::cerr << "[WEB] Start forwarded to orchestrator\n";
                update_cached_control_state("homing");
                return {};
            });

        server->SetStopCallback(
            [control, &update_cached_control_state]()
                -> std::expected<void, ccme::web::ApiError> {
                std::cerr << "[WEB] Stop callback\n";
                auto result =
                    control->Call(ccme::dbus::kOrchestrator, "Stop");
                if (!result) {
                    const std::string message = result.error().message;
                    const std::string code =
                        ClassifyWorkflowError(message, "stop_failed");
                    std::string hint =
                        "查看系统错误提示和 ccme-dispenser 日志";
                    if (code == "not_running") {
                        hint = "当前没有正在运行的流程";
                    } else if (code == "orchestrator_unavailable") {
                        hint = "检查 ccme-dispenser 是否运行以及 D-Bus 权限";
                    }
                    return std::unexpected(ccme::web::ApiError{
                        code, message, std::move(hint)});
                }
                std::cerr << "[WEB] Stop forwarded to orchestrator\n";
                update_cached_control_state("idle");
                return {};
            });

        server->SetResetCallback(
            [control, &update_cached_control_state]()
                -> std::expected<void, ccme::web::ApiError> {
                std::cerr << "[WEB] Reset callback\n";
                auto result =
                    control->Call(ccme::dbus::kOrchestrator, "ClearError");
                if (!result) {
                    const std::string message = result.error().message;
                    const std::string code =
                        ClassifyWorkflowError(message, "reset_failed");
                    std::string hint =
                        "查看系统错误提示和 ccme-dispenser 日志";
                    if (code == "not_in_error") {
                        hint = "流程正在运行，请先停止后再重置";
                    } else if (code == "orchestrator_unavailable") {
                        hint = "检查 ccme-dispenser 是否运行以及 D-Bus 权限";
                    }
                    return std::unexpected(ccme::web::ApiError{
                        code, message, std::move(hint)});
                }
                std::cerr << "[WEB] Reset forwarded to orchestrator\n";
                update_cached_control_state("idle");
                return {};
            });

        auto start_result = server->Start();
        if (!start_result) {
            std::cerr << "[WEB] HTTP server failed to start\n";
            return 1;
        }

        std::thread poller_thread([poller, &status_mutex, &cached_status]() {
            std::cerr << "[WEB] Status poller started\n";

            while (g_running) {
                nlohmann::json status;
                status["state"] = "unknown";
                status["current_vial"] = 0;
                status["total_vials"] = CCME_VIAL_ROWS * CCME_VIAL_COLS;
                status["modules"] = nlohmann::json::object();

                auto orchestrator_status = poller->CallWithResult<std::string>(
                    ccme::dbus::kOrchestrator, "GetStatus");
                if (!orchestrator_status) {
                    status["error"] = MakeErrorJson(
                        "orchestrator_unavailable",
                        "无法连接编排器: " +
                            orchestrator_status.error().message,
                        "检查 ccme-dispenser 是否运行以及 D-Bus 权限",
                        "orchestrator");
                } else {
                    auto parsed = nlohmann::json::parse(
                        *orchestrator_status, nullptr, false);
                    if (parsed.is_discarded() || !parsed.is_object()) {
                        status["error"] = MakeErrorJson(
                            "invalid_orchestrator_status",
                            "编排器返回了无法解析的状态",
                            "查看 ccme-dispenser 日志", "orchestrator");
                    } else {
                        status = std::move(parsed);
                        if (!status.contains("current_vial")) {
                            status["current_vial"] = 0;
                        }
                        if (!status.contains("total_vials")) {
                            status["total_vials"] =
                                CCME_VIAL_ROWS * CCME_VIAL_COLS;
                        }
                        if (!status.contains("state")) {
                            status["state"] = "unknown";
                        }
                        if (!status.contains("error")) {
                            status["error"] = nullptr;
                        }
                    }
                }
                status["modules"] = nlohmann::json::object();

                if (auto running = poller->CallWithResult<bool>(
                        ccme::dbus::kPump, "IsRunning")) {
                    status["modules"]["pump"] = {
                        {"online", true}, {"running", *running}, {"error", ""}};
                } else {
                    status["modules"]["pump"] = {
                        {"online", false},
                        {"running", false},
                        {"error", running.error().message}};
                }

                if (auto running = poller->CallWithResult<bool>(
                        ccme::dbus::kPump, "IsRunning2")) {
                    status["modules"]["pump2"] = {
                        {"online", true}, {"running", *running}, {"error", ""}};
                } else {
                    status["modules"]["pump2"] = {
                        {"online", false},
                        {"running", false},
                        {"error", running.error().message}};
                }

                auto stirring = poller->CallWithResult<bool>(
                    ccme::dbus::kStirrer, "IsStirring");
                if (!stirring) {
                    status["modules"]["stirrer"] = {
                        {"online", false},
                        {"stirring", false},
                        {"heating", false},
                        {"speed", 0},
                        {"set_temp", 0.0},
                        {"temp", 0.0},
                        {"error", stirring.error().message}};
                } else {
                    std::string module_error;
                    auto heating = poller->CallWithResult<bool>(
                        ccme::dbus::kStirrer, "IsHeating");
                    if (!heating) {
                        module_error = heating.error().message;
                    }
                    auto speed = poller->CallWithResult<int>(
                        ccme::dbus::kStirrer, "GetSpeed");
                    if (!speed && module_error.empty()) {
                        module_error = speed.error().message;
                    }
                    auto set_temp = poller->CallWithResult<double>(
                        ccme::dbus::kStirrer, "GetSetTemp");
                    if (!set_temp && module_error.empty()) {
                        module_error = set_temp.error().message;
                    }
                    auto temp = poller->CallWithResult<double>(
                        ccme::dbus::kStirrer, "GetActualTemp");
                    if (!temp && module_error.empty()) {
                        module_error = temp.error().message;
                    }
                    status["modules"]["stirrer"] = {
                        {"online", true},
                        {"stirring", *stirring},
                        {"heating", heating.value_or(false)},
                        {"speed", speed.value_or(0)},
                        {"set_temp", set_temp.value_or(0.0)},
                        {"temp", temp.value_or(0.0)},
                        {"error", module_error},
                    };
                }

                auto camera_connected = poller->CallWithResult<bool>(
                    ccme::dbus::kCamera, "IsConnected");
                if (!camera_connected) {
                    status["modules"]["camera"] = {
                        {"online", false},
                        {"connected", false},
                        {"ai_configured", false},
                        {"error", camera_connected.error().message}};
                } else {
                    auto ai_configured = poller->CallWithResult<bool>(
                        ccme::dbus::kCamera, "IsAiConfigured");
                    status["modules"]["camera"] = {
                        {"online", true},
                        {"connected", *camera_connected},
                        {"ai_configured", ai_configured.value_or(false)},
                        {"error", ai_configured
                                      ? std::string{}
                                      : ai_configured.error().message},
                    };
                }

                auto fmc_connected = poller->CallWithResult<bool>(
                    ccme::dbus::kFmc, "IsConnected");
                if (!fmc_connected) {
                    status["modules"]["fmc"] = {
                        {"online", false},
                        {"connected", false},
                        {"moving", false},
                        {"error", fmc_connected.error().message}};
                } else if (!*fmc_connected) {
                    status["modules"]["fmc"] = {
                        {"online", true},
                        {"connected", false},
                        {"moving", false},
                        {"error", "FMC 控制器未连接"}};
                } else {
                    auto moving = poller->CallWithResult<bool>(
                        ccme::dbus::kFmc, "IsMoving");
                    if (moving) {
                        status["modules"]["fmc"] = {
                            {"online", true},
                            {"connected", true},
                            {"moving", *moving},
                            {"error", ""}};
                    } else {
                        status["modules"]["fmc"] = {
                            {"online", true},
                            {"connected", true},
                            {"moving", false},
                            {"error", moving.error().message}};
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(status_mutex);
                    cached_status = std::move(status);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(500));
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
