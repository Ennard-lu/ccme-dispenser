#pragma once

// Central definitions for the CCME D-Bus architecture.
//
// Every backend module owns exactly one well-known name on the system bus:
//
//   org.ccme.<Module>          (bus name)
//   /org/ccme/<module>         (object path)
//   org.ccme.<Module>.Control  (interface name)
//   org.ccme.<Module>.Control.Error (error name)
//
// Both service implementations (registration) and clients (method calls) are
// expected to use the constants in this header so that names and signatures
// cannot drift apart between modules.

#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <expected>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace ccme::dbus {

struct Service {
    std::string_view bus_name;
    std::string_view object_path;
    std::string_view interface_name;

    [[nodiscard]] std::string error_name() const {
        return std::string(interface_name) + ".Error";
    }
};

inline constexpr Service kPump{
    "org.ccme.Pump",
    "/org/ccme/pump",
    "org.ccme.Pump.Control",
};

inline constexpr Service kStirrer{
    "org.ccme.Stirrer",
    "/org/ccme/stirrer",
    "org.ccme.Stirrer.Control",
};

inline constexpr Service kCamera{
    "org.ccme.Camera",
    "/org/ccme/camera",
    "org.ccme.Camera.Control",
};

inline constexpr Service kFmc{
    "org.ccme.FMC",
    "/org/ccme/fmc",
    "org.ccme.FMC.Control",
};

inline constexpr Service kOrchestrator{
    "org.ccme.Orchestrator",
    "/org/ccme/orchestrator",
    "org.ccme.Orchestrator.Control",
};

// Throw a D-Bus error with the canonical error name for a service.
// Used inside D-Bus method handlers to report failures to the caller.
[[noreturn]] inline void ThrowError(const Service& service,
                                    std::string_view message) {
    throw sdbus::Error{sdbus::Error::Name{service.error_name()},
                       std::string(message)};
}

enum class ErrorCode {
    kNotConnected,
    kCallFailed,
};

struct Error {
    ErrorCode code{ErrorCode::kCallFailed};
    std::string message;
};

// Create a connection that owns the well-known name of a CCME service.
inline std::unique_ptr<sdbus::IConnection> Connect(const Service& service) {
    return sdbus::createSystemBusConnection(
        sdbus::ServiceName{std::string(service.bus_name)});
}

// Create the canonical object exported by a CCME service.
inline std::unique_ptr<sdbus::IObject> CreateObject(
    sdbus::IConnection& connection, const Service& service) {
    return sdbus::createObject(
        connection, sdbus::ObjectPath{std::string(service.object_path)});
}

// Small synchronous D-Bus client with an expected-based interface.
//
// Each call creates a short-lived proxy, so a Client can safely be used from
// multiple threads (sdbus-c++ connections themselves are thread-safe for
// synchronous method calls).
class Client {
public:
    Client() noexcept {
        try {
            owned_ = sdbus::createSystemBusConnection();
            connection_ = owned_.get();
        } catch (const std::exception& e) {
            std::cerr << "[DBUS] Failed to connect to system bus: "
                      << e.what() << "\n";
        }
    }

    explicit Client(sdbus::IConnection& connection) noexcept
        : connection_(&connection) {}

    ~Client() = default;

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    Client(Client&& other) noexcept
        : owned_(std::move(other.owned_)), connection_(other.connection_) {
        other.connection_ = nullptr;
    }

    Client& operator=(Client&& other) noexcept {
        if (this != &other) {
            owned_ = std::move(other.owned_);
            connection_ = other.connection_;
            other.connection_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] bool IsConnected() const noexcept {
        return connection_ != nullptr;
    }

    template <typename... Args>
    std::expected<void, Error> Call(const Service& service,
                                    std::string_view method,
                                    Args&&... args) const {
        auto proxy = CreateProxy(service);
        if (!proxy) {
            return std::unexpected(proxy.error());
        }

        try {
            if constexpr (sizeof...(Args) == 0) {
                (*proxy)->callMethod(std::string(method))
                    .onInterface(std::string(service.interface_name));
            } else {
                (*proxy)->callMethod(std::string(method))
                    .onInterface(std::string(service.interface_name))
                    .withArguments(std::forward<Args>(args)...);
            }
            return {};
        } catch (const std::exception& e) {
            return std::unexpected(Error{ErrorCode::kCallFailed, e.what()});
        }
    }

    template <typename... Args>
    std::expected<void, Error> CallWithTimeout(
        const Service& service, std::string_view method,
        std::chrono::microseconds timeout, Args&&... args) const {
        auto proxy = CreateProxy(service);
        if (!proxy) {
            return std::unexpected(proxy.error());
        }

        try {
            if constexpr (sizeof...(Args) == 0) {
                (*proxy)->callMethod(std::string(method))
                    .onInterface(std::string(service.interface_name))
                    .withTimeout(timeout);
            } else {
                (*proxy)->callMethod(std::string(method))
                    .onInterface(std::string(service.interface_name))
                    .withTimeout(timeout)
                    .withArguments(std::forward<Args>(args)...);
            }
            return {};
        } catch (const std::exception& e) {
            return std::unexpected(Error{ErrorCode::kCallFailed, e.what()});
        }
    }

    template <typename Result, typename... Args>
    std::expected<Result, Error> CallWithResult(
        const Service& service, std::string_view method, Args&&... args) const {
        auto proxy = CreateProxy(service);
        if (!proxy) {
            return std::unexpected(proxy.error());
        }

        try {
            Result result{};
            if constexpr (sizeof...(Args) == 0) {
                (*proxy)->callMethod(std::string(method))
                    .onInterface(std::string(service.interface_name))
                    .storeResultsTo(result);
            } else {
                (*proxy)->callMethod(std::string(method))
                    .onInterface(std::string(service.interface_name))
                    .withArguments(std::forward<Args>(args)...)
                    .storeResultsTo(result);
            }
            return result;
        } catch (const std::exception& e) {
            return std::unexpected(Error{ErrorCode::kCallFailed, e.what()});
        }
    }

    template <typename Result, typename... Args>
    std::expected<Result, Error> CallWithResultAndTimeout(
        const Service& service, std::string_view method,
        std::chrono::microseconds timeout, Args&&... args) const {
        auto proxy = CreateProxy(service);
        if (!proxy) {
            return std::unexpected(proxy.error());
        }

        try {
            Result result{};
            if constexpr (sizeof...(Args) == 0) {
                (*proxy)->callMethod(std::string(method))
                    .onInterface(std::string(service.interface_name))
                    .withTimeout(timeout)
                    .storeResultsTo(result);
            } else {
                (*proxy)->callMethod(std::string(method))
                    .onInterface(std::string(service.interface_name))
                    .withTimeout(timeout)
                    .withArguments(std::forward<Args>(args)...)
                    .storeResultsTo(result);
            }
            return result;
        } catch (const std::exception& e) {
            return std::unexpected(Error{ErrorCode::kCallFailed, e.what()});
        }
    }

private:
    std::expected<std::unique_ptr<sdbus::IProxy>, Error> CreateProxy(
        const Service& service) const {
        if (connection_ == nullptr) {
            return std::unexpected(Error{
                ErrorCode::kNotConnected,
                "D-Bus system bus is not connected",
            });
        }

        try {
            return sdbus::createProxy(
                *connection_,
                sdbus::ServiceName{std::string(service.bus_name)},
                sdbus::ObjectPath{std::string(service.object_path)});
        } catch (const std::exception& e) {
            return std::unexpected(Error{ErrorCode::kCallFailed, e.what()});
        }
    }

    std::unique_ptr<sdbus::IConnection> owned_;
    sdbus::IConnection* connection_{nullptr};
};

}  // namespace ccme::dbus
