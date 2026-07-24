#include "stirrer/stirrer_controller.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <string>
#include <array>

namespace ccme::stirrer {

namespace {

constexpr int kReadTimeoutMs = 2000;
constexpr int kInterCharMs = 50;

}  // namespace

struct StirrerController::Impl {
    bool stirring{false};
    bool heating{false};
    int speed_rpm{0};
    double set_temp{0.0};
    std::string serial_port;
    int baud_rate;
    int fd{-1};

    Impl()
        : serial_port(CCME_STIRRER_SERIAL_PORT),
          baud_rate(CCME_STIRRER_BAUD_RATE) {}

    bool OpenPort() {
        if (fd >= 0) return true;
        fd = open(serial_port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd < 0) return false;

        struct termios tty{};
        tcgetattr(fd, &tty);
        cfsetispeed(&tty, static_cast<speed_t>(baud_rate));
        cfsetospeed(&tty, static_cast<speed_t>(baud_rate));

        // 7 data bits, even parity, 1 stop bit (7E1)
        tty.c_cflag = (tty.c_cflag & ~static_cast<tcflag_t>(CSIZE)) | CS7;
        tty.c_cflag |= PARENB;
        tty.c_cflag &= ~(CSTOPB | CRTSCTS);
        tty.c_cflag |= CLOCAL | CREAD;

        tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT |
                          PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
        tty.c_oflag &= ~OPOST;

        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 10;  // 1 second read timeout

        tcsetattr(fd, TCSANOW, &tty);
        return true;
    }

    // Send a NAMUR text command with terminator: " \r \n"
    bool SendText(const std::string& cmd) {
        if (fd < 0) return false;
        // Terminator per IKA NAMUR protocol: space + CR + space + LF
        const char terminator[] = " \r \n";
        std::string frame = cmd + terminator;
        ssize_t written = write(fd, frame.c_str(), frame.size());
        return written == static_cast<ssize_t>(frame.size());
    }

    // Send a text command and read the response line, stripping CR/LF
    std::expected<std::string, StirrerError> SendRead(const std::string& cmd) {
        if (fd < 0) return std::unexpected(StirrerError::kSerialOpenFailed);

        if (!SendText(cmd)) {
            return std::unexpected(StirrerError::kWriteFailed);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kInterCharMs));

        // Read response bytes with timeout
        std::string resp;
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(kReadTimeoutMs);
        std::array<char, 128> buf{};
        while (std::chrono::steady_clock::now() < deadline) {
            ssize_t n = read(fd, buf.data(), buf.size() - 1);
            if (n > 0) {
                buf[n] = '\0';
                resp.append(buf.data(), static_cast<size_t>(n));
                // If we got a newline, response is likely complete
                if (resp.find('\n') != std::string::npos) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Strip trailing CR/LF/spaces
        while (!resp.empty() && (resp.back() == '\r' || resp.back() == '\n' || resp.back() == ' ')) {
            resp.pop_back();
        }
        // Strip leading spaces
        size_t start = resp.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return std::unexpected(StirrerError::kReadFailed);
        resp = resp.substr(start);

        if (resp.empty()) return std::unexpected(StirrerError::kReadFailed);
        return resp;
    }

    // Parse integer from response
    std::expected<int, StirrerError> ReadInt(const std::string& cmd) {
        auto resp = SendRead(cmd);
        if (!resp) return std::unexpected(resp.error());
        try {
            return std::stoi(*resp);
        } catch (...) {
            return std::unexpected(StirrerError::kReadFailed);
        }
    }

    // Parse double from response
    std::expected<double, StirrerError> ReadDouble(const std::string& cmd) {
        auto resp = SendRead(cmd);
        if (!resp) return std::unexpected(resp.error());
        try {
            return std::stod(*resp);
        } catch (...) {
            return std::unexpected(StirrerError::kReadFailed);
        }
    }

    // Send a control command (no response expected)
    bool SendControl(const std::string& cmd) {
        return SendText(cmd);
    }
};

StirrerController::StirrerController()
    : impl_(std::make_unique<Impl>()) {}

StirrerController::~StirrerController() {
    if (impl_ && impl_->heating) {
        (void)StopHeat();
    }
    if (impl_ && impl_->stirring) {
        (void)StopStir();
    }
    if (impl_ && impl_->fd >= 0) {
        close(impl_->fd);
    }
}

StirrerController::StirrerController(StirrerController&&) noexcept = default;
StirrerController& StirrerController::operator=(StirrerController&&) noexcept = default;

std::expected<bool, StirrerError> StirrerController::StartStir(int speed_rpm) {
    if (impl_->stirring) {
        return std::unexpected(StirrerError::kAlreadyRunning);
    }

    if (!impl_->OpenPort()) {
        return std::unexpected(StirrerError::kSerialOpenFailed);
    }

    if (!impl_->SendControl("OUT_SP_4 " + std::to_string(speed_rpm))) {
        return std::unexpected(StirrerError::kWriteFailed);
    }

    if (!impl_->SendControl("START_4")) {
        return std::unexpected(StirrerError::kWriteFailed);
    }

    impl_->stirring = true;
    impl_->speed_rpm = speed_rpm;
    return true;
}

std::expected<bool, StirrerError> StirrerController::StopStir() {
    if (!impl_->stirring) {
        return std::unexpected(StirrerError::kNotRunning);
    }

    if (!impl_->SendControl("STOP_4")) {
        return std::unexpected(StirrerError::kWriteFailed);
    }

    impl_->stirring = false;
    impl_->speed_rpm = 0;
    return true;
}

bool StirrerController::IsStirring() const {
    return impl_->stirring;
}

std::expected<bool, StirrerError> StirrerController::StartHeat(double temp_c) {
    if (impl_->heating) {
        return std::unexpected(StirrerError::kAlreadyRunning);
    }

    if (!impl_->OpenPort()) {
        return std::unexpected(StirrerError::kSerialOpenFailed);
    }

    if (!impl_->SendControl("OUT_SP_1 " + std::to_string(static_cast<int>(temp_c)))) {
        return std::unexpected(StirrerError::kWriteFailed);
    }

    if (!impl_->SendControl("START_1")) {
        return std::unexpected(StirrerError::kWriteFailed);
    }

    impl_->heating = true;
    impl_->set_temp = temp_c;
    return true;
}

std::expected<bool, StirrerError> StirrerController::StopHeat() {
    if (!impl_->heating) {
        return std::unexpected(StirrerError::kNotRunning);
    }

    if (!impl_->SendControl("STOP_1")) {
        return std::unexpected(StirrerError::kWriteFailed);
    }

    impl_->heating = false;
    return true;
}

bool StirrerController::IsHeating() const {
    return impl_->heating;
}

int StirrerController::GetSpeed() const {
    return impl_->speed_rpm;
}

std::expected<int, StirrerError> StirrerController::GetActualSpeed() {
    if (!impl_->OpenPort()) {
        return std::unexpected(StirrerError::kSerialOpenFailed);
    }
    return impl_->ReadInt("IN_PV_4");
}

double StirrerController::GetSetTemp() const {
    return impl_->set_temp;
}

std::expected<double, StirrerError> StirrerController::GetActualTemp() {
    if (!impl_->OpenPort()) {
        return std::unexpected(StirrerError::kSerialOpenFailed);
    }
    return impl_->ReadDouble("IN_PV_2");
}

std::expected<double, StirrerError> StirrerController::GetExternalTemp() {
    if (!impl_->OpenPort()) {
        return std::unexpected(StirrerError::kSerialOpenFailed);
    }
    return impl_->ReadDouble("IN_PV_1");
}

}  // namespace ccme::stirrer
