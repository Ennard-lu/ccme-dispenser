#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ccme::web {

enum class WebError {
    kBindFailed,
    kListenFailed,
    kRunning,
    kNotRunning,
};

using StatusCallback = std::function<std::string()>;
using StartCallback = std::function<bool(double volume_ml)>;
using StopCallback = std::function<bool()>;

struct ServerConfig {
    std::string listen_address{"0.0.0.0"};
    std::uint16_t port{8080};
    std::string static_dir{"frontend"};
};

class HttpServer {
public:
    explicit HttpServer(ServerConfig config);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    HttpServer(HttpServer&&) noexcept;
    HttpServer& operator=(HttpServer&&) noexcept;

    void SetStatusCallback(StatusCallback cb);
    void SetStartCallback(StartCallback cb);
    void SetStopCallback(StopCallback cb);

    std::expected<bool, WebError> Start();
    std::expected<bool, WebError> Stop();
    [[nodiscard]] bool IsRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ccme::web
