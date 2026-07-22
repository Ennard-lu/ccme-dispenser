#include "stirrer/stirrer_controller.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>

namespace ccme::stirrer {

namespace {

constexpr uint8_t kModbusFunctionWriteSingle = 0x06;
constexpr uint8_t kIkaDeviceAddress = 1;

constexpr uint16_t kRegMotorOn = 0x0000;
constexpr uint16_t kRegSpeedTarget = 0x0001;
constexpr uint16_t kRegTempTarget = 0x0002;
constexpr uint16_t kRegHeaterOn = 0x0003;

uint16_t Crc16Modbus(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void AppendCrc(std::vector<uint8_t>& frame) {
    uint16_t crc = Crc16Modbus(frame.data(), frame.size());
    frame.push_back(crc & 0xFF);
    frame.push_back((crc >> 8) & 0xFF);
}

std::vector<uint8_t> BuildWriteRegister(uint16_t reg, uint16_t value) {
    std::vector<uint8_t> frame = {kIkaDeviceAddress, kModbusFunctionWriteSingle,
                                  static_cast<uint8_t>(reg >> 8),
                                  static_cast<uint8_t>(reg & 0xFF),
                                  static_cast<uint8_t>(value >> 8),
                                  static_cast<uint8_t>(value & 0xFF)};
    AppendCrc(frame);
    return frame;
}

}  // namespace

struct StirrerController::Impl {
    bool stirring{false};
    bool heating{false};
    int speed_rpm{0};
    double set_temp{0.0};
    double actual_temp{0.0};
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
        cfsetispeed(&tty, B9600);
        cfsetospeed(&tty, B9600);
        tty.c_cflag = (tty.c_cflag & ~static_cast<tcflag_t>(CSIZE)) | CS8;
        tty.c_cflag |= CLOCAL | CREAD;
        tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
        tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT |
                          PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
        tty.c_oflag &= ~OPOST;
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 10;
        tcsetattr(fd, TCSANOW, &tty);
        return true;
    }

    bool SendCommand(const std::vector<uint8_t>& cmd) {
        if (fd < 0) return false;
        if (write(fd, cmd.data(), cmd.size()) !=
            static_cast<ssize_t>(cmd.size())) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        uint8_t buf[64]{};
        read(fd, buf, sizeof(buf));
        return true;
    }

    bool SendStart() {
        auto cmd = BuildWriteRegister(kRegMotorOn, 0x0001);
        return SendCommand(cmd);
    }

    bool SendStop() {
        auto cmd = BuildWriteRegister(kRegMotorOn, 0x0000);
        return SendCommand(cmd);
    }

    bool SetSpeed(int rpm) {
        auto cmd = BuildWriteRegister(kRegSpeedTarget,
                                      static_cast<uint16_t>(rpm));
        return SendCommand(cmd);
    }

    bool SendHeatStart() {
        auto cmd = BuildWriteRegister(kRegHeaterOn, 0x0001);
        return SendCommand(cmd);
    }

    bool SendHeatStop() {
        auto cmd = BuildWriteRegister(kRegHeaterOn, 0x0000);
        return SendCommand(cmd);
    }

    bool SetTemp(double temp_c) {
        auto cmd = BuildWriteRegister(kRegTempTarget,
                                      static_cast<uint16_t>(temp_c * 10.0));
        return SendCommand(cmd);
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

    if (!impl_->SetSpeed(speed_rpm)) {
        return std::unexpected(StirrerError::kWriteFailed);
    }

    if (!impl_->SendStart()) {
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

    if (!impl_->SendStop()) {
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

    if (!impl_->SetTemp(temp_c)) {
        return std::unexpected(StirrerError::kWriteFailed);
    }

    if (!impl_->SendHeatStart()) {
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

    if (!impl_->SendHeatStop()) {
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

double StirrerController::GetSetTemp() const {
    return impl_->set_temp;
}

double StirrerController::GetActualTemp() const {
    return impl_->actual_temp;
}

}  // namespace ccme::stirrer
