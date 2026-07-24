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
constexpr int kMotionTimeoutMs = 30000;
constexpr int kPollIntervalMs = 50;

}  // namespace

struct FmcController::Impl {
    int card_id{0};
    bool connected{false};
    int vial_rows{CCME_VIAL_ROWS};
    int vial_cols{CCME_VIAL_COLS};
    float spacing_x{static_cast<float>(CCME_VIAL_SPACING_X)};
    float spacing_y{static_cast<float>(CCME_VIAL_SPACING_Y)};
    float origin_x{static_cast<float>(CCME_VIAL_ORIGIN_X)};
    float origin_y{static_cast<float>(CCME_VIAL_ORIGIN_Y)};

    Impl() : card_id(CCME_FMC_CARD_ID) {
        std::cerr << "[FMC] Initialized: card=" << card_id
                  << " vials=" << vial_rows << "x" << vial_cols
                  << " origin=(" << origin_x << "," << origin_y << ")"
                  << " spacing=(" << spacing_x << "," << spacing_y << ")\n";
    }

    float VialX(int col) const {
        return origin_x + static_cast<float>(col) * spacing_x;
    }

    float VialY(int row) const {
        return origin_y + static_cast<float>(row) * spacing_y;
    }

    bool WaitForStop() {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(kMotionTimeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            if (FMC4030_Check_Axis_Is_Stop(card_id, 0) &&
                FMC4030_Check_Axis_Is_Stop(card_id, 1)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
        }
        std::cerr << "[FMC] Motion timeout after " << kMotionTimeoutMs << "ms\n";
        return false;
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

    for (int axis = 0; axis < 2; ++axis) {
        FMC4030_Home_Single_Axis(impl_->card_id, axis,
                                 kHomeSpeed, kHomeAcc, kHomeDec, kHomeDir);
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(60000);
    while (std::chrono::steady_clock::now() < deadline) {
        bool done = true;
        for (int axis = 0; axis < 2; ++axis) {
            if (!FMC4030_Check_Axis_Is_Stop(impl_->card_id, axis)) {
                done = false;
                break;
            }
        }
        if (done) {
            std::cerr << "[FMC] Home completed\n";
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cerr << "[FMC] Home timeout\n";
    return std::unexpected(FmcError::kHomeFailed);
}

std::expected<bool, FmcError> FmcController::MoveTo(float x, float y) {
    if (!impl_->connected) {
        std::cerr << "[FMC] MoveTo rejected: not connected\n";
        return std::unexpected(FmcError::kConnectionFailed);
    }

    std::cerr << "[FMC] Moving to (" << x << ", " << y << ")\n";

    FMC4030_Line_2Axis(impl_->card_id, 0x03, x, y, kMoveSpeed, kMoveAcc, kMoveDec);

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
    return MoveTo(impl_->VialX(col), impl_->VialY(row));
}

std::expected<bool, FmcError> FmcController::MoveToVial(int row, int col) {
    if (row < 0 || row >= impl_->vial_rows ||
        col < 0 || col >= impl_->vial_cols) {
        std::cerr << "[FMC] MoveToVial rejected: invalid pos (" << row << "," << col << ")\n";
        return std::unexpected(FmcError::kMotionFailed);
    }

    std::cerr << "[FMC] MoveToVial row=" << row << " col=" << col << "\n";
    return MoveTo(impl_->VialX(col), impl_->VialY(row));
}

bool FmcController::IsMoving() const {
    if (!impl_->connected) {
        return false;
    }
    return !FMC4030_Check_Axis_Is_Stop(impl_->card_id, 0) ||
           !FMC4030_Check_Axis_Is_Stop(impl_->card_id, 1);
}

}  // namespace ccme::fmc
