#include "web/http_server.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>

#include <sys/socket.h>
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

struct HttpServer::Impl {
    ServerConfig config;
    bool running{false};
    StatusCallback status_cb;
    StartCallback start_cb;
    StopCallback stop_cb;
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

std::expected<bool, WebError> HttpServer::Start() {
    if (impl_->running) {
        return std::unexpected(WebError::kRunning);
    }

    impl_->svr = std::make_unique<httplib::Server>();

    impl_->svr->Get("/api/status", [this](const httplib::Request&,
                                           httplib::Response& res) {
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
        nlohmann::json j;
        try {
            auto body = nlohmann::json::parse(req.body);
            double volume = body.value("volume_ml", 0.0);
            if (impl_->start_cb && impl_->start_cb(volume)) {
                j["success"] = true;
            } else {
                j["success"] = false;
                j["error"] = "start failed";
            }
        } catch (const std::exception& e) {
            j["success"] = false;
            j["error"] = e.what();
        }
        res.set_content(j.dump(), "application/json");
    });

    impl_->svr->Post("/api/stop", [this](const httplib::Request&,
                                          httplib::Response& res) {
        nlohmann::json j;
        if (impl_->stop_cb && impl_->stop_cb()) {
            j["success"] = true;
        } else {
            j["success"] = false;
            j["error"] = "stop failed";
        }
        res.set_content(j.dump(), "application/json");
    });

#ifdef CCME_HAS_OPENCV
    // MJPEG fallback endpoint (OpenCV decode + JPEG re-encode)
    if (!impl_->config.stream_url.empty()) {
        auto stream_url = impl_->config.stream_url;
        impl_->svr->Get("/api/stream", [stream_url](const httplib::Request&,
                                                     httplib::Response& res) {
            auto cap = std::make_shared<cv::VideoCapture>(
                stream_url, cv::CAP_FFMPEG);
            if (!cap->isOpened()) {
                res.status = 503;
                res.set_content("Camera stream not available", "text/plain");
                return;
            }

            res.set_chunked_content_provider(
                "multipart/x-mixed-replace; boundary=frame",
                [cap](size_t, httplib::DataSink& sink) {
                    cv::Mat frame;
                    while (sink.is_writable()) {
                        if (cap->read(frame) && !frame.empty()) {
                            std::vector<uchar> buf;
                            cv::imencode(".jpg", frame, buf,
                                         {cv::IMWRITE_JPEG_QUALITY, 80});
                            const char hdr[] =
                                "--frame\r\n"
                                "Content-Type: image/jpeg\r\n\r\n";
                            sink.write(hdr, sizeof(hdr) - 1);
                            sink.write(reinterpret_cast<const char*>(buf.data()),
                                       buf.size());
                            const char tail[] = "\r\n";
                            sink.write(tail, 2);
                        }
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(33));
                    }
                    return true;
                });
        });
    }
#endif

    // Low-latency MPEG-TS TCP proxy: no decode/re-encode, browser decodes via MSE
    {
        std::string url = impl_->config.stream_url;
        int port = 7777;
        auto pos = url.rfind(':');
        if (pos != std::string::npos) {
            try { port = std::stoi(url.substr(pos + 1)); }
            catch (...) {}
        }

        impl_->svr->Get("/api/hls/stream", [port](const httplib::Request&,
                                                   httplib::Response& res) {
            res.set_chunked_content_provider(
                "video/mp2t",
                [port](size_t, httplib::DataSink& sink) -> bool {
                    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
                    if (fd < 0) return false;

                    struct sockaddr_in addr{};
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(port);
                    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

                    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
                                  sizeof(addr)) < 0) {
                        ::close(fd);
                        return false;
                    }

                    char buf[4096];
                    bool ok = true;
                    while (ok && sink.is_writable()) {
                        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
                        if (n > 0) {
                            ok = sink.write(buf, n);
                        } else {
                            break;
                        }
                    }
                    ::close(fd);
                    return true;
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

    if (impl_->svr) {
        impl_->svr->stop();
    }
    if (impl_->server_thread.joinable()) {
        impl_->server_thread.join();
    }

    impl_->running = false;
    return true;
}

bool HttpServer::IsRunning() const {
    return impl_->running;
}

}  // namespace ccme::web
