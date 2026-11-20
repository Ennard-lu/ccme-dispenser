#include "orchestrator/workflow.h"

#include "dbus/ccme_dbus.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace ccme::orchestrator {

namespace {

constexpr int kDissolutionPollMs = CCME_CAMERA_CHECK_INTERVAL_MS;
constexpr int kMaxConsecutiveCheckFailures = 5;
constexpr int kConnectAttempts = 3;
constexpr int kConnectRetryDelayMs = 500;
constexpr auto kFmcHomeTimeout = std::chrono::seconds(220);
constexpr auto kFmcMoveTimeout = std::chrono::seconds(620);
constexpr auto kCameraTimeout = std::chrono::seconds(CCME_AI_TIMEOUT_SEC + 30);

using DbusResult = std::expected<void, dbus::Error>;
using DbusBool = std::expected<bool, dbus::Error>;

[[nodiscard]] bool EnvFlag(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return false;
    }

    std::string text{value};
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return text == "y" || text == "yes" || text == "1" || text == "true" ||
           text == "on";
}

[[nodiscard]] std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Thin typed wrapper around the shared D-Bus client. All names come from
// ccme::dbus, so the orchestrator and services cannot disagree about them.
class DbusProxy {
public:
    DbusResult PumpStart(double volume_ml) {
        return client_.Call(dbus::kPump, "Start", volume_ml);
    }

    DbusResult PumpStop() {
        return client_.Call(dbus::kPump, "Stop");
    }

    DbusResult Pump2Start(double volume_ml) {
        return client_.Call(dbus::kPump, "Start2", volume_ml);
    }

    DbusResult Pump2Stop() {
        return client_.Call(dbus::kPump, "Stop2");
    }

    DbusResult StirrerStart(int rpm) {
        return client_.Call(dbus::kStirrer, "StartStir", rpm);
    }

    DbusResult StirrerStop() {
        return client_.Call(dbus::kStirrer, "StopStir");
    }

    DbusResult StirrerHeatStart(double temp_c) {
        return client_.Call(dbus::kStirrer, "StartHeat", temp_c);
    }

    DbusResult StirrerHeatStop() {
        return client_.Call(dbus::kStirrer, "StopHeat");
    }

    DbusBool CheckDissolution() {
        return client_.CallWithResultAndTimeout<bool>(
            dbus::kCamera, "CheckDissolution", kCameraTimeout);
    }

    DbusBool CameraIsAiConfigured() {
        return client_.CallWithResult<bool>(dbus::kCamera,
                                            "IsAiConfigured");
    }

    DbusBool CameraConnect() {
        return client_.CallWithResult<bool>(dbus::kCamera, "Connect");
    }

    void CameraDisconnect() {
        (void)client_.Call(dbus::kCamera, "Disconnect");
    }

    DbusResult FmcConnect() {
        return client_.Call(dbus::kFmc, "Connect");
    }

    DbusResult FmcReturnHome() {
        return client_.CallWithTimeout(dbus::kFmc, "Home", kFmcHomeTimeout);
    }

    DbusResult FmcMoveToVial(int index) {
        return client_.CallWithTimeout(dbus::kFmc, "MoveToVial",
                                       kFmcMoveTimeout, index);
    }

private:
    dbus::Client client_;
};

}  // namespace

std::string ToString(WorkflowState state) {
    switch (state) {
        case WorkflowState::kIdle: return "idle";
        case WorkflowState::kHoming: return "homing";
        case WorkflowState::kInjectingWater: return "injecting_water";
        case WorkflowState::kHeating: return "heating";
        case WorkflowState::kStirring: return "stirring";
        case WorkflowState::kCheckingDissolution: return "checking_dissolution";
        case WorkflowState::kDispensing: return "dispensing";
        case WorkflowState::kMovingToVial: return "moving_to_vial";
        case WorkflowState::kComplete: return "complete";
        case WorkflowState::kError: return "error";
    }
    return "unknown";
}

struct Workflow::Impl {
    std::atomic<WorkflowState> state{WorkflowState::kIdle};
    double volume_ml{0.0};
    int stir_speed_rpm{200};
    double heat_temp_c{50.0};
    double dispense_volume_ml{10.0};
    std::atomic<int> current_vial{0};
    int total_vials{CCME_VIAL_ROWS * CCME_VIAL_COLS};
    std::atomic<bool> stop_requested{false};
    std::thread workflow_thread;
    DbusProxy dbus;

    mutable std::mutex error_mutex;
    std::optional<WorkflowErrorInfo> last_error;
    bool skip_ai_check{false};

    Impl() {
        skip_ai_check = EnvFlag("CCME_SKIP_AI_CHECK");
        if (skip_ai_check) {
            std::cerr << "[ORCH] WARNING: CCME_SKIP_AI_CHECK is enabled; "
                         "AI dissolution detection will be skipped and the "
                         "workflow will proceed directly to dispensing.\n";
        }
    }

    void SetState(WorkflowState new_state) {
        state.store(new_state, std::memory_order_relaxed);
        std::cerr << "[ORCH] State -> " << ToString(new_state) << "\n";
    }

    void ClearErrorInfo() {
        std::lock_guard<std::mutex> lock(error_mutex);
        last_error.reset();
    }

    void SetError(WorkflowErrorInfo info) {
        std::lock_guard<std::mutex> lock(error_mutex);
        last_error = std::move(info);
    }

    [[nodiscard]] std::optional<WorkflowErrorInfo> ErrorCopy() const {
        std::lock_guard<std::mutex> lock(error_mutex);
        return last_error;
    }

    // Sleep in small slices so a Stop request is honored quickly.
    bool SleepUnlessStopped(int milliseconds) {
        constexpr int kSliceMs = 100;
        int remaining = milliseconds;
        while (remaining > 0) {
            if (stop_requested.load(std::memory_order_relaxed)) {
                return false;
            }
            const int slice = std::min(remaining, kSliceMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(slice));
            remaining -= slice;
        }
        return !stop_requested.load(std::memory_order_relaxed);
    }

    void StopDevices() {
        (void)dbus.StirrerHeatStop();
        (void)dbus.StirrerStop();
        (void)dbus.PumpStop();
        (void)dbus.Pump2Stop();
    }

    void Fail(std::string code, std::string message, std::string hint,
              std::string module, bool recoverable = true) {
        WorkflowErrorInfo info;
        info.code = std::move(code);
        info.message = std::move(message);
        info.hint = std::move(hint);
        info.module = std::move(module);
        info.stage = ToString(state.load(std::memory_order_relaxed));
        info.recoverable = recoverable;
        info.timestamp_ms = NowMs();

        std::cerr << "[ORCH] ERROR [" << info.code << "] " << info.message;
        if (!info.hint.empty()) {
            std::cerr << " | hint: " << info.hint;
        }
        std::cerr << "\n";

        SetError(std::move(info));
        StopDevices();
        dbus.CameraDisconnect();
        SetState(WorkflowState::kError);
    }

    // Retry opening the camera and FMC connections a few times. This makes a
    // just-restarted stream/controller recoverable without restarting the
    // whole orchestrator.
    bool ConnectCamera(std::string& error_message) {
        for (int attempt = 1; attempt <= kConnectAttempts; ++attempt) {
            auto result = dbus.CameraConnect();
            if (result && *result) {
                return true;
            }
            error_message = result
                                ? "camera service reported disconnected"
                                : result.error().message;
            std::cerr << "[ORCH] Camera connect attempt " << attempt
                      << " failed: " << error_message << "\n";
            if (attempt < kConnectAttempts &&
                !SleepUnlessStopped(kConnectRetryDelayMs)) {
                return false;
            }
        }
        return false;
    }

    bool ConnectFmc(std::string& error_message) {
        for (int attempt = 1; attempt <= kConnectAttempts; ++attempt) {
            auto result = dbus.FmcConnect();
            if (result) {
                return true;
            }
            error_message = result.error().message;
            std::cerr << "[ORCH] FMC connect attempt " << attempt
                      << " failed: " << error_message << "\n";
            if (attempt < kConnectAttempts &&
                !SleepUnlessStopped(kConnectRetryDelayMs)) {
                return false;
            }
        }
        return false;
    }

    void Run() {
        if (!skip_ai_check) {
            auto ai_configured = dbus.CameraIsAiConfigured();
            if (!ai_configured) {
                Fail("camera_unavailable",
                     "无法连接视觉检测服务: " + ai_configured.error().message,
                     "确认 ccme-camera 正在运行，并检查 D-Bus 权限", "camera");
                return;
            }
            if (!*ai_configured) {
                Fail("ai_not_configured", "云端视觉模型尚未配置",
                     "设置 CCME_AI_API_KEY、CCME_AI_MODEL 和 CCME_AI_API_URL "
                     "后重启 ccme-camera",
                     "camera");
                return;
            }

            std::string camera_error;
            if (!ConnectCamera(camera_error)) {
                if (stop_requested.load(std::memory_order_relaxed)) {
                    return;
                }
                Fail("camera_stream_unavailable",
                     "摄像头视频流不可用: " + camera_error,
                     "检查摄像头连接和 GStreamer 推流脚本", "camera");
                return;
            }
            if (stop_requested.load(std::memory_order_relaxed)) {
                return;
            }
        } else {
            std::cerr << "[ORCH] AI/camera preflight skipped\n";
        }

        std::string fmc_error;
        if (!ConnectFmc(fmc_error)) {
            if (stop_requested.load(std::memory_order_relaxed)) {
                return;
            }
            Fail("fmc_unavailable", "FMC 控制器连接失败: " + fmc_error,
                 "检查控制器电源、网线和 IP 配置", "fmc");
            return;
        }
        if (stop_requested.load(std::memory_order_relaxed)) {
            return;
        }

        SetState(WorkflowState::kHoming);
        if (auto result = dbus.FmcReturnHome(); !result) {
            if (stop_requested.load(std::memory_order_relaxed)) {
                return;
            }
            Fail("fmc_home_failed",
                 "三轴回零失败: " + result.error().message,
                 "检查限位开关、机械结构和 FMC 状态", "fmc");
            return;
        }
        if (stop_requested.load(std::memory_order_relaxed)) {
            return;
        }

        SetState(WorkflowState::kInjectingWater);
        if (auto result = dbus.PumpStart(volume_ml); !result) {
            Fail("pump_start_failed",
                 "注水泵启动失败: " + result.error().message,
                 "检查蠕动泵 PWM/GPIO 接线、供电和权限", "pump");
            return;
        }
        const int fill_ms = static_cast<int>(
            volume_ml / CCME_PUMP0_FLOW_RATE * 1000.0) + 500;
        if (!SleepUnlessStopped(fill_ms)) {
            return;
        }

        SetState(WorkflowState::kHeating);
        if (auto result = dbus.StirrerHeatStart(heat_temp_c); !result) {
            Fail("heater_start_failed",
                 "加热启动失败: " + result.error().message,
                 "检查磁力搅拌器串口连接和电源", "stirrer");
            return;
        }

        SetState(WorkflowState::kStirring);
        if (auto result = dbus.StirrerStart(stir_speed_rpm); !result) {
            Fail("stirrer_start_failed",
                 "搅拌启动失败: " + result.error().message,
                 "检查磁力搅拌器串口连接和转子", "stirrer");
            return;
        }

        SetState(WorkflowState::kCheckingDissolution);
        bool dissolved = false;

        if (skip_ai_check) {
            std::cerr << "[ORCH] AI dissolution check skipped; "
                         "treating solution as ready for dispensing\n";
            dissolved = true;
        } else {
            bool timed_out = false;
            bool check_failed = false;
            std::string last_check_error;
            int consecutive_failures = 0;
            const auto check_started = std::chrono::steady_clock::now();

            while (!stop_requested.load(std::memory_order_relaxed)) {
                if (CCME_DISSOLUTION_TIMEOUT_SEC > 0 &&
                    std::chrono::steady_clock::now() - check_started >=
                        std::chrono::seconds(CCME_DISSOLUTION_TIMEOUT_SEC)) {
                    timed_out = true;
                    break;
                }

                auto result = dbus.CheckDissolution();
                if (!result) {
                    last_check_error = result.error().message;
                    std::cerr << "[ORCH] CheckDissolution failed: "
                              << last_check_error << "\n";
                    if (++consecutive_failures >=
                        kMaxConsecutiveCheckFailures) {
                        check_failed = true;
                        break;
                    }
                } else {
                    consecutive_failures = 0;
                    if (*result) {
                        dissolved = true;
                        break;
                    }
                }

                if (!SleepUnlessStopped(kDissolutionPollMs)) {
                    break;
                }
            }

            dbus.CameraDisconnect();

            if (stop_requested.load(std::memory_order_relaxed)) {
                StopDevices();
                return;
            }
            if (!dissolved) {
                if (timed_out) {
                    Fail("dissolution_timeout",
                         "等待溶解完成超时（" +
                             std::to_string(CCME_DISSOLUTION_TIMEOUT_SEC) +
                             " 秒）",
                         "检查加热和搅拌是否正常，或提高 "
                         "CCME_DISSOLUTION_TIMEOUT_SEC",
                         "camera");
                } else if (check_failed) {
                    Fail("vision_check_failed",
                         "连续视觉检测失败: " + last_check_error,
                         "检查云端 AI 网络、API Key/模型配置和摄像头画面",
                         "camera");
                } else {
                    Fail("dissolution_not_confirmed", "未能确认溶解完成",
                         "检查视觉识别结果和烧杯状态", "camera");
                }
                return;
            }
        }

        if (stop_requested.load(std::memory_order_relaxed)) {
            StopDevices();
            return;
        }

        std::cerr << "[ORCH] Dissolution complete, stopping stirrer & heater\n";
        (void)dbus.StirrerHeatStop();
        (void)dbus.StirrerStop();

        while (current_vial.load(std::memory_order_relaxed) < total_vials &&
               !stop_requested.load(std::memory_order_relaxed)) {
            const int vial = current_vial.load(std::memory_order_relaxed);
            std::cerr << "[ORCH] --- Vial " << (vial + 1)
                      << "/" << total_vials << " ---\n";

            SetState(WorkflowState::kMovingToVial);
            if (auto result = dbus.FmcMoveToVial(vial); !result) {
                Fail("fmc_move_failed",
                     "移动平台到 " + std::to_string(vial + 1) +
                         " 号小瓶失败: " + result.error().message,
                     "检查 FMC 状态、限位开关和机械结构", "fmc");
                return;
            }
            if (!SleepUnlessStopped(500)) {
                return;
            }

            SetState(WorkflowState::kDispensing);
            if (auto result = dbus.Pump2Start(dispense_volume_ml); !result) {
                Fail("dispense_pump_failed",
                     "分装泵启动失败: " + result.error().message,
                     "检查分装泵 PWM/GPIO 接线、供电和权限", "pump2");
                return;
            }
            const int dispense_ms = static_cast<int>(
                dispense_volume_ml / CCME_PUMP1_FLOW_RATE * 1000.0) + 500;
            if (!SleepUnlessStopped(dispense_ms)) {
                return;
            }

            current_vial.fetch_add(1, std::memory_order_relaxed);
        }

        if (stop_requested.load(std::memory_order_relaxed)) {
            return;
        }
        SetState(WorkflowState::kComplete);
        std::cerr << "[ORCH] Workflow finished\n";
    }
};

Workflow::Workflow()
    : impl_(std::make_unique<Impl>()) {}

Workflow::~Workflow() {
    if (!impl_) {
        return;
    }

    if (impl_->workflow_thread.joinable()) {
        impl_->stop_requested.store(true, std::memory_order_relaxed);
        (void)impl_->dbus.StirrerHeatStop();
        (void)impl_->dbus.StirrerStop();
        (void)impl_->dbus.PumpStop();
        (void)impl_->dbus.Pump2Stop();
        impl_->dbus.CameraDisconnect();
        impl_->workflow_thread.join();
    }
}

Workflow::Workflow(Workflow&&) noexcept = default;
Workflow& Workflow::operator=(Workflow&&) noexcept = default;

std::expected<bool, WorkflowError> Workflow::Start(
    double volume_ml, int stir_speed_rpm, double heat_temp_c,
    double dispense_volume_ml) {
    const auto current = impl_->state.load(std::memory_order_relaxed);
    if (current != WorkflowState::kIdle &&
        current != WorkflowState::kComplete &&
        current != WorkflowState::kError) {
        std::cerr << "[ORCH] Start rejected: state="
                  << ToString(current) << "\n";
        return std::unexpected(WorkflowError::kAlreadyRunning);
    }

    std::cerr << "[ORCH] Start: volume=" << volume_ml
              << "ml stir=" << stir_speed_rpm
              << "RPM temp=" << heat_temp_c
              << "C dispense=" << dispense_volume_ml << "ml\n";

    if (impl_->workflow_thread.joinable()) {
        impl_->workflow_thread.join();
    }

    impl_->volume_ml = volume_ml;
    impl_->stir_speed_rpm = stir_speed_rpm;
    impl_->heat_temp_c = heat_temp_c;
    impl_->dispense_volume_ml = dispense_volume_ml;
    impl_->current_vial.store(0, std::memory_order_relaxed);
    impl_->stop_requested.store(false, std::memory_order_relaxed);
    impl_->ClearErrorInfo();
    impl_->state.store(WorkflowState::kHoming, std::memory_order_relaxed);

    impl_->workflow_thread = std::thread([this]() { impl_->Run(); });
    return true;
}

std::expected<bool, WorkflowError> Workflow::Stop() {
    if (impl_->state.load(std::memory_order_relaxed) ==
        WorkflowState::kIdle) {
        std::cerr << "[ORCH] Stop rejected: already idle\n";
        return std::unexpected(WorkflowError::kNotRunning);
    }

    std::cerr << "[ORCH] Stop requested (state=" << GetStateString() << ")\n";

    impl_->stop_requested.store(true, std::memory_order_relaxed);

    (void)impl_->dbus.StirrerHeatStop();
    (void)impl_->dbus.StirrerStop();
    (void)impl_->dbus.PumpStop();
    (void)impl_->dbus.Pump2Stop();
    impl_->dbus.CameraDisconnect();

    if (impl_->workflow_thread.joinable()) {
        impl_->workflow_thread.join();
    }

    impl_->state.store(WorkflowState::kIdle, std::memory_order_relaxed);
    impl_->current_vial.store(0, std::memory_order_relaxed);
    impl_->ClearErrorInfo();
    std::cerr << "[ORCH] Stopped\n";
    return true;
}

std::expected<bool, WorkflowError> Workflow::ClearError() {
    const auto current = impl_->state.load(std::memory_order_relaxed);
    if (current == WorkflowState::kError ||
        current == WorkflowState::kComplete) {
        return Stop();
    }
    if (current == WorkflowState::kIdle) {
        impl_->ClearErrorInfo();
        std::cerr << "[ORCH] Cleared stale error\n";
        return true;
    }
    return std::unexpected(WorkflowError::kNotInError);
}

WorkflowState Workflow::GetState() const {
    return impl_->state.load(std::memory_order_relaxed);
}

std::string Workflow::GetStateString() const {
    return ToString(GetState());
}

int Workflow::GetCurrentVial() const {
    return impl_->current_vial.load(std::memory_order_relaxed);
}

int Workflow::GetTotalVials() const {
    return impl_->total_vials;
}

WorkflowSnapshot Workflow::GetStatus() const {
    WorkflowSnapshot snapshot;
    snapshot.state = impl_->state.load(std::memory_order_relaxed);
    snapshot.current_vial =
        impl_->current_vial.load(std::memory_order_relaxed);
    snapshot.total_vials = impl_->total_vials;
    snapshot.ai_check_skipped = impl_->skip_ai_check;
    snapshot.error = impl_->ErrorCopy();
    return snapshot;
}

std::optional<WorkflowErrorInfo> Workflow::GetLastError() const {
    return impl_->ErrorCopy();
}

}  // namespace ccme::orchestrator
