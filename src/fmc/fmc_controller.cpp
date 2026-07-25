#include "fmc/fmc_controller.h"

#include <FMC4030.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <iostream>

namespace ccme::fmc {

namespace {

constexpr float kHomeSpeed = 50.0f;
constexpr float kHomeAcc = 100.0f;
constexpr float kHomeDec = 100.0f;
constexpr float kMoveSpeed = 80.0f;
constexpr float kMoveAcc = 200.0f;
constexpr float kMoveDec = 200.0f;
constexpr int kHomeDir = 1;
constexpr int kStopMode = 0;
constexpr int kMotionTimeoutMs = 200000;
constexpr int kPollIntervalMs = 50;
constexpr float kDownZPosition = -500;

}  // namespace

struct FmcController::Impl {
    int card_id{0};
    bool connected{false};
    int vial_rows{CCME_VIAL_ROWS};
    int vial_cols{CCME_VIAL_COLS};
    float home_position_x{};
    float home_position_y{};
    float spacing_x{static_cast<float>(CCME_VIAL_SPACING_X)};
    float spacing_y{static_cast<float>(CCME_VIAL_SPACING_Y)};
    float origin_x{static_cast<float>(CCME_VIAL_ORIGIN_X)};
    float origin_y{static_cast<float>(CCME_VIAL_ORIGIN_Y)};
    float origin_z{static_cast<float>(CCME_VIAL_ORIGIN_Z)};

    Impl() : card_id(CCME_FMC_CARD_ID) {
        std::cerr << "[FMC] Initialized: card=" << card_id
                  << " vials=" << vial_rows << "x" << vial_cols
                  << " origin=(" << origin_x << "," << origin_y << ")"
                  << " spacing=(" << spacing_x << "," << spacing_y << ")\n";
    }

    float VialX(int col) const {
        return home_position_x + origin_x + static_cast<float>(col) * spacing_x;
    }

    float VialY(int row) const {
        return home_position_y + origin_y + static_cast<float>(row) * spacing_y;
    }

    bool WaitForStop() {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMotionTimeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            machine_status para;
            FMC4030_Get_Machine_Status(card_id, (unsigned char*)&para);
            if (!(para.axisStatus[0] & MACHINE_RUNNING) && !(para.axisStatus[0] & MACHINE_HOME) &&
                !(para.axisStatus[1] & MACHINE_RUNNING) && !(para.axisStatus[1] & MACHINE_HOME) &&
                !(para.axisStatus[2] & MACHINE_RUNNING) && !(para.axisStatus[2] & MACHINE_HOME)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
        }
        std::cerr << "[FMC] Motion timeout after " << kMotionTimeoutMs << "ms\n";
        return false;
    }

    void UpZ() const {
        FMC4030_Home_Single_Axis(card_id, 2, 100, 100, 0.1, 1);
        std::cerr << "[FMC] Z axis lifted.\n";
    }

    void DownZ() const {
        FMC4030_Jog_Single_Axis(card_id, 2, kDownZPosition, 100, 100, 200, 1);
        std::cerr << "[FMC] Z axis down.\n";
    }
};

FmcController::FmcController()
    : impl_(std::make_unique<Impl>()) {}

FmcController::~FmcController() {
    Disconnect();
}

FmcController::FmcController(FmcController&&) noexcept = default;
FmcController& FmcController::operator=(FmcController&&) noexcept = default;

std::expected<bool, FmcError> FmcController::Connect() {
    if (impl_->connected) {
        std::cerr << "[FMC] Already connected\n";
        return true;
    }

    char ip[64];
    std::snprintf(ip, sizeof(ip), "%s", CCME_FMC_CONTROLLER_IP);
    std::cerr << "[FMC] Connecting to " << ip << ":" << CCME_FMC_CONTROLLER_PORT << "\n";

    int ret = FMC4030_Open_Device(impl_->card_id, ip, CCME_FMC_CONTROLLER_PORT);
    if (ret != 0) {
        std::cerr << "[FMC] Connection failed (error=" << ret << ")\n";
        return std::unexpected(FmcError::kConnectionFailed);
    }

    machine_device_para para;
    FMC4030_Get_Device_Para(impl_->card_id, (unsigned char*)&para);
    for (int axis = 0; axis < 3; axis++) {
        para.homeTime[axis] = 200000;
        para.softLimitMax[axis] = 6000;
        para.softLimitMin[axis] = 6000;
    }
    FMC4030_Set_Device_Para(impl_->card_id, (unsigned char*)&para);

    impl_->connected = true;
    std::cerr << "[FMC] Connected\n";
    return true;
}

void FmcController::Disconnect() {
    if (impl_->connected) {
        std::cerr << "[FMC] Disconnecting\n";
        FMC4030_Close_Device(impl_->card_id);
        impl_->connected = false;
        std::cerr << "[FMC] Disconnected\n";
    }
}

std::expected<bool, FmcError> FmcController::Home() {
    if (!impl_->connected) {
        std::cerr << "[FMC] Home rejected: not connected\n";
        return std::unexpected(FmcError::kConnectionFailed);
    }

    std::cerr << "[FMC] Homing axes...\n";

    for (int axis = 0; axis < 3; ++axis) {
        FMC4030_Home_Single_Axis(impl_->card_id, axis, kHomeSpeed, kHomeAcc, 0, kHomeDir);
    }

    if (!impl_->WaitForStop())
        return std::unexpected(FmcError::kHomeFailed);
    else
        return true;
}

std::expected<bool, FmcError> FmcController::MoveTo(float x, float y) {
    if (!impl_->connected) {
        std::cerr << "[FMC] MoveTo rejected: not connected\n";
        return std::unexpected(FmcError::kConnectionFailed);
    }

    std::cerr << "[FMC] Moving to (" << x << ", " << y << ")\n";

    FMC4030_Jog_Single_Axis(impl_->card_id, 0, x, kMoveSpeed, kMoveAcc, kMoveDec, 2);
    FMC4030_Jog_Single_Axis(impl_->card_id, 1, y, kMoveSpeed, kMoveAcc, kMoveDec, 2);

    if (!impl_->WaitForStop()) {
        return std::unexpected(FmcError::kMotionFailed);
    }

    std::cerr << "[FMC] Move completed\n";
    return true;
}

std::expected<bool, FmcError> FmcController::MoveToVial(int index) {
    if (index < 0 || index >= impl_->vial_rows * impl_->vial_cols) {
        std::cerr << "[FMC] MoveToVial rejected: invalid index " << index << "\n";
        return std::unexpected(FmcError::kMotionFailed);
    }

    int row = index / impl_->vial_cols;
    int col = index % impl_->vial_cols;
    std::cerr << "[FMC] MoveToVial index=" << index << " (row=" << row << " col=" << col << ")\n";
    impl_->UpZ();
    if (!impl_->WaitForStop()) {
        return std::unexpected(FmcError::kMotionFailed);
    }
    auto ret =MoveTo(impl_->VialX(col), impl_->VialY(row));
    if (!ret || !*ret) return ret;
    impl_->DownZ();
    if (!impl_->WaitForStop()) {
        return std::unexpected(FmcError::kMotionFailed);
    }
    return ret;
}

std::expected<bool, FmcError> FmcController::MoveToVial(int row, int col) {
    if (row < 0 || row >= impl_->vial_rows ||
        col < 0 || col >= impl_->vial_cols) {
        std::cerr << "[FMC] MoveToVial rejected: invalid pos (" << row << "," << col << ")\n";
        return std::unexpected(FmcError::kMotionFailed);
    }

    std::cerr << "[FMC] MoveToVial row=" << row << " col=" << col << "\n";
    impl_->UpZ();
    if (!impl_->WaitForStop()) {
        return std::unexpected(FmcError::kMotionFailed);
    }
    auto ret =MoveTo(impl_->VialX(col), impl_->VialY(row));
    if (!ret || !*ret) return ret;
    impl_->DownZ();
    if (!impl_->WaitForStop()) {
        return std::unexpected(FmcError::kMotionFailed);
    }
    return ret;
}

bool FmcController::IsMoving() const {
    if (!impl_->connected) {
        return false;
    }
    machine_status para;
    FMC4030_Get_Machine_Status(impl_->card_id, (unsigned char*)&para);
    return ((para.axisStatus[0] & MACHINE_RUNNING) || (para.axisStatus[0] & MACHINE_HOME) ||
            (para.axisStatus[1] & MACHINE_RUNNING) || (para.axisStatus[1] & MACHINE_HOME) ||
            (para.axisStatus[2] & MACHINE_RUNNING) || (para.axisStatus[2] & MACHINE_HOME));
}

}  // namespace ccme::fmc
