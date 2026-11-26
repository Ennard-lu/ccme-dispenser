#include "web/http_server.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <vector>

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#ifdef CCME_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace ccme::web {

namespace {

[[nodiscard]] int HttpStatusForError(const ApiError& error) {
    if (error.code == "already_running" || error.code == "not_running" ||
        error.code == "not_in_error") {
        return 409;
    }
    if (error.code == "orchestrator_unavailable" ||
        error.code == "service_unavailable") {
        return 503;
    }
    if (error.code == "invalid_request") {
        return 400;
    }
    return 500;
}

void SetJsonError(httplib::Response& res, const ApiError& error) {
    std::cerr << "[WEB] API error [" << error.code << "]: "
              << error.message << "\n";
    nlohmann::json json;
    json["success"] = false;
    json["error"] = {
        {"code", error.code},
        {"message", error.message},
        {"hint", error.hint},
    };
    res.status = HttpStatusForError(error);
    res.set_content(json.dump(), "application/json");
}

}  // namespace

struct HttpServer::Impl {
    ServerConfig config;
    bool running{false};
    StatusCallback status_cb;
    StartCallback start_cb;
    StopCallback stop_cb;
    ResetCallback reset_cb;
    std::unique_ptr<httplib::Server> svr;
    std::thread server_thread;

    explicit Impl(ServerConfig cfg) : config(std::move(cfg)) {}
};

HttpServer::HttpServer(ServerConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

HttpServer::~HttpServer() {
    if (impl_ && impl_->running) {
        (void)Stop();
    }
}

HttpServer::HttpServer(HttpServer&&) noexcept = default;
HttpServer& HttpServer::operator=(HttpServer&&) noexcept = default;

void HttpServer::SetStatusCallback(StatusCallback cb) {
    impl_->status_cb = std::move(cb);
}

void HttpServer::SetStartCallback(StartCallback cb) {
    impl_->start_cb = std::move(cb);
}

void HttpServer::SetStopCallback(StopCallback cb) {
    impl_->stop_cb = std::move(cb);
}

void HttpServer::SetResetCallback(ResetCallback cb) {
    impl_->reset_cb = std::move(cb);
}

std::expected<bool, WebError> HttpServer::Start() {
    if (impl_->running) {
        return std::unexpected(WebError::kRunning);
    }

    impl_->svr = std::make_unique<httplib::Server>();
    impl_->svr->set_read_timeout(30, 0);
    impl_->svr->set_write_timeout(30, 0);

    impl_->svr->Get("/api/status", [this](const httplib::Request&,
                                           httplib::Response& res) {
        std::cerr << "[WEB] GET /api/status\n";
        nlohmann::json j;
        if (impl_->status_cb) {
            try {
                j = nlohmann::json::parse(impl_->status_cb());
            } catch (...) {
                j["state"] = "unknown";
            }
        } else {
            j["state"] = "idle";
        }
        res.set_content(j.dump(), "application/json");
    });

    impl_->svr->Post("/api/start", [this](const httplib::Request& req,
                                           httplib::Response& res) {
        StartParams params;
        try {
            auto body = nlohmann::json::parse(req.body);
            params.volume_ml = body.value("volume_ml", 0.0);
            params.stir_speed_rpm = body.value("stir_speed_rpm", 200);
            params.heat_temp_c = body.value("heat_temp_c", 50.0);
            params.dispense_volume_ml = body.value("dispense_volume_ml", 10.0);
        } catch (const std::exception& e) {
            SetJsonError(res, ApiError{
                "invalid_request",
                std::string{"请求参数解析失败: "} + e.what(),
                "检查 JSON 字段和类型",
            });
            return;
        }

        std::cerr << "[WEB] POST /api/start volume=" << params.volume_ml
                  << "ml stir=" << params.stir_speed_rpm
                  << "RPM temp=" << params.heat_temp_c
                  << "C dispense=" << params.dispense_volume_ml << "ml\n";

        if (!impl_->start_cb) {
            SetJsonError(res, ApiError{
                "service_unavailable",
                "启动回调未注册",
                "检查 ccme-web 配置",
            });
            return;
        }

        auto result = impl_->start_cb(params);
        if (!result) {
            SetJsonError(res, result.error());
            return;
        }

        std::cerr << "[WEB] Start ok\n";
        res.status = 200;
        res.set_content(R"({"success":true})", "application/json");
    });

    impl_->svr->Post("/api/stop", [this](const httplib::Request&,
                                          httplib::Response& res) {
        std::cerr << "[WEB] POST /api/stop\n";
        if (!impl_->stop_cb) {
            SetJsonError(res, ApiError{
                "service_unavailable", "停止回调未注册", "检查 ccme-web 配置"});
            return;
        }

        auto result = impl_->stop_cb();
        if (!result) {
            SetJsonError(res, result.error());
            return;
        }

        std::cerr << "[WEB] Stop ok\n";
        res.status = 200;
        res.set_content(R"({"success":true})", "application/json");
    });

    impl_->svr->Post("/api/reset", [this](const httplib::Request&,
                                           httplib::Response& res) {
        std::cerr << "[WEB] POST /api/reset\n";
        if (!impl_->reset_cb) {
            SetJsonError(res, ApiError{
                "service_unavailable", "重置回调未注册", "检查 ccme-web 配置"});
            return;
        }

        auto result = impl_->reset_cb();
        if (!result) {
            SetJsonError(res, result.error());
            return;
        }

        std::cerr << "[WEB] Reset ok\n";
        res.status = 200;
        res.set_content(R"({"success":true})", "application/json");
    });

#ifdef CCME_HAS_OPENCV
    // MJPEG fallback endpoint (OpenCV decode + JPEG re-encode)
    if (!impl_->config.mpegts_url.empty() ||
        !impl_->config.camera_url.empty()) {
        const auto mpegts_url = impl_->config.mpegts_url;
        const auto camera_url = impl_->config.camera_url;
        impl_->svr->Get("/api/stream",
                        [mpegts_url, camera_url](const httplib::Request&,
                                                 httplib::Response& res) {
            std::cerr << "[WEB] GET /api/stream (MJPEG)\n";

            std::vector<std::string> sources;
            // Prefer the raw H.264 stream for MJPEG: it is the stream that
            // OpenCV has historically decoded on the target board. The
            // MPEG-TS source is only a fallback.
            if (!camera_url.empty()) {
                sources.push_back(camera_url);
            }
            if (!mpegts_url.empty() && mpegts_url != camera_url) {
                sources.push_back(mpegts_url);
            }

            std::shared_ptr<cv::VideoCapture> cap;
            std::string source_url;
            const std::vector<int> open_params{
                cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
                cv::CAP_PROP_READ_TIMEOUT_MSEC, 5000,
            };

            for (const auto& url : sources) {
                auto candidate = std::make_shared<cv::VideoCapture>();
                try {
                    std::cerr << "[WEB] MJPEG trying source: " << url << "\n";
                    if (candidate->open(url, cv::CAP_FFMPEG, open_params)) {
                        cap = std::move(candidate);
                        source_url = url;
                        break;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[WEB] MJPEG source exception (" << url
                              << "): " << e.what() << "\n";
                }
            }

            if (!cap) {
                std::cerr << "[WEB] MJPEG stream open failed for all sources\n";
                res.status = 503;
                res.set_content("Camera stream not available", "text/plain");
                return;
            }
            std::cerr << "[WEB] MJPEG source connected: " << source_url
                      << "\n";

            res.set_header("Cache-Control",
                           "no-cache, no-store, must-revalidate");
            res.set_header("Pragma", "no-cache");
            res.set_header("X-Accel-Buffering", "no");

            res.set_chunked_content_provider(
                "multipart/x-mixed-replace; boundary=frame",
                [cap, sources, open_params](size_t,
                                            httplib::DataSink& sink) mutable
                    -> bool {
                    int consecutive_failures = 0;
                    std::uint64_t frames_sent = 0;

                    const auto reopen_capture = [&]() -> bool {
                        cap->release();
                        for (const auto& url : sources) {
                            auto candidate =
                                std::make_shared<cv::VideoCapture>();
                            try {
                                if (candidate->open(url, cv::CAP_FFMPEG,
                                                    open_params)) {
                                    cap = std::move(candidate);
                                    std::cerr << "[WEB] MJPEG reconnected: "
                                              << url << "\n";
                                    return true;
                                }
                            } catch (const std::exception& e) {
                                std::cerr << "[WEB] MJPEG reconnect exception ("
                                          << url << "): " << e.what()
                                          << "\n";
                            }
                        }
                        return false;
                    };

                    while (sink.is_writable()) {
                        try {
                            cv::Mat frame;
                            if (cap->read(frame) && !frame.empty()) {
                                consecutive_failures = 0;
                                std::vector<uchar> buf;
                                cv::imencode(".jpg", frame, buf,
                                             {cv::IMWRITE_JPEG_QUALITY, 80});
                                const char hdr[] =
                                    "--frame\r\n"
                                    "Content-Type: image/jpeg\r\n\r\n";
                                const char tail[] = "\r\n";
                                const bool ok =
                                    sink.write(hdr, sizeof(hdr) - 1) &&
                                    sink.write(reinterpret_cast<const char*>(
                                                   buf.data()),
                                               buf.size()) &&
                                    sink.write(tail, 2);
                                if (!ok) {
                                    break;
                                }
                                ++frames_sent;
                            } else if (++consecutive_failures >= 10) {
                                std::cerr << "[WEB] MJPEG stream lost, "
                                             "reconnecting\n";
                                if (!reopen_capture()) {
                                    break;
                                }
                                consecutive_failures = 0;
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "[WEB] MJPEG stream exception: "
                                      << e.what() << "\n";
                            break;
                        }
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(33));
                    }
                    std::cerr << "[WEB] MJPEG client disconnected, sent "
                              << frames_sent << " frames\n";
                    return false;
                });
        });
    }
#endif

    // Low-latency MPEG-TS TCP proxy: no decode/re-encode, browser decodes via MSE
    {
        std::string url = impl_->config.mpegts_url;
        int port = 7777;
        auto pos = url.rfind(':');
        if (pos != std::string::npos) {
            try { port = std::stoi(url.substr(pos + 1)); }
            catch (...) {}
        }

        impl_->svr->Get("/api/hls/stream", [port](const httplib::Request&,
                                                   httplib::Response& res) {
            std::cerr << "[WEB] GET /api/hls/stream (MPEG-TS port=" << port << ")\n";
            res.set_header("Cache-Control",
                           "no-cache, no-store, must-revalidate");
            res.set_header("Pragma", "no-cache");
            res.set_header("X-Accel-Buffering", "no");
            res.set_chunked_content_provider(
                "video/mp2t",
                [port](size_t, httplib::DataSink& sink) -> bool {
                    if (!sink.is_writable()) {
                        return false;
                    }

                    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
                    if (fd < 0) {
                        std::cerr << "[WEB] MPEG-TS socket create failed\n";
                        return false;
                    }

                    struct sockaddr_in addr{};
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(port);
                    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

                    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
                                  sizeof(addr)) < 0) {
                        std::cerr << "[WEB] MPEG-TS connect failed\n";
                        ::close(fd);
                        return false;
                    }

                    // Wake up regularly so a browser disconnect is noticed even
                    // when the local stream temporarily has no data.
                    struct timeval recv_timeout{};
                    recv_timeout.tv_sec = 1;
                    recv_timeout.tv_usec = 0;
                    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                                 &recv_timeout, sizeof(recv_timeout));

                    std::cerr << "[WEB] MPEG-TS proxy connected\n";
                    char buf[4096];
                    std::uint64_t total_bytes = 0;
                    while (sink.is_writable()) {
                        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
                        if (n > 0) {
                            if (!sink.write(buf, static_cast<size_t>(n))) {
                                break;
                            }
                            total_bytes += static_cast<std::uint64_t>(n);
                        } else if (n == 0) {
                            break;
                        } else if (errno == EINTR) {
                            continue;
                        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // Receive timeout: loop to re-check the client.
                            continue;
                        } else {
                            break;
                        }
                    }
                    ::close(fd);
                    std::cerr << "[WEB] MPEG-TS client disconnected, sent "
                              << total_bytes << " bytes\n";
                    return false;
                });
        });
    }

    impl_->svr->set_mount_point("/", impl_->config.static_dir);

    impl_->server_thread = std::thread([this]() {
        impl_->svr->listen(impl_->config.listen_address,
                           impl_->config.port);
    });

    impl_->running = true;
    return true;
}

std::expected<bool, WebError> HttpServer::Stop() {
    if (!impl_->running) {
        return std::unexpected(WebError::kNotRunning);
    }

    std::cerr << "[WEB] Stopping HTTP server\n";

    if (impl_->svr) {
        impl_->svr->stop();
    }
    if (impl_->server_thread.joinable()) {
        impl_->server_thread.join();
    }

    impl_->running = false;
    std::cerr << "[WEB] HTTP server stopped\n";
    return true;
}

bool HttpServer::IsRunning() const {
    return impl_->running;
}

}  // namespace ccme::web
