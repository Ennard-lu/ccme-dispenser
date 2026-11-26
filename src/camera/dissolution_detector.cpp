#include "camera/dissolution_detector.h"

#include "ai/vision_client.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef CCME_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace ccme::camera {

namespace {

[[nodiscard]] std::string EnvString(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? std::string{value}
                                                : std::string{fallback};
}

[[nodiscard]] int EnvInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    return (end != value) ? static_cast<int>(parsed) : fallback;
}

[[nodiscard]] bool EnvBool(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    std::string text{value};
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return text == "1" || text == "true" || text == "yes" || text == "on";
}

[[nodiscard]] std::string Trim(std::string text) {
    const auto is_space = [](unsigned char c) {
        return std::isspace(c) != 0;
    };

    const auto begin = std::find_if_not(text.begin(), text.end(), is_space);
    const auto end = std::find_if_not(text.rbegin(), text.rend(), is_space).base();
    if (begin >= end) {
        return {};
    }
    return std::string{begin, end};
}

struct PromptFile {
    std::string path;
    std::string text;
};

[[nodiscard]] std::optional<PromptFile> ReadPromptFile(
    const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string text = Trim(buffer.str());
    if (text.empty()) {
        return std::nullopt;
    }
    return PromptFile{path, std::move(text)};
}

[[nodiscard]] std::optional<PromptFile> FindPromptFile() {
    if (const char* explicit_path = std::getenv("CCME_AI_PROMPT_FILE");
        explicit_path != nullptr && *explicit_path != '\0') {
        if (auto prompt = ReadPromptFile(explicit_path)) {
            return prompt;
        }
        std::cerr << "[CAMERA] Warning: cannot read CCME_AI_PROMPT_FILE="
                  << explicit_path << "\n";
        return std::nullopt;
    }

    std::vector<std::string> candidates;
    if (const char* home = std::getenv("HOME");
        home != nullptr && *home != '\0') {
        candidates.push_back(std::string{home} +
                             "/.config/ccme/ai_prompt.txt");
        candidates.push_back(std::string{home} +
                             "/.local/etc/ccme/ai_prompt.txt");
    }
    candidates.emplace_back("/etc/ccme/ai_prompt.txt");
    candidates.emplace_back("/usr/etc/ccme/ai_prompt.txt");
    candidates.emplace_back("/usr/local/etc/ccme/ai_prompt.txt");
    candidates.emplace_back("config/ai_prompt.txt");
    candidates.emplace_back("ai_prompt.txt");

    for (const auto& path : candidates) {
        if (auto prompt = ReadPromptFile(path)) {
            return prompt;
        }
    }
    return std::nullopt;
}

[[nodiscard]] ai::VisionConfig BuildVisionConfig() {
    ai::VisionConfig config;
    config.api_url = EnvString("CCME_AI_API_URL", CCME_AI_API_URL);
    config.api_key = EnvString("CCME_AI_API_KEY", CCME_AI_API_KEY);
    config.model = EnvString("CCME_AI_MODEL", CCME_AI_MODEL);
    config.timeout_sec = EnvInt("CCME_AI_TIMEOUT_SEC", CCME_AI_TIMEOUT_SEC);
    config.max_tokens = CCME_AI_MAX_TOKENS;
    config.disable_thinking =
        EnvBool("CCME_AI_DISABLE_THINKING", CCME_AI_DISABLE_THINKING);
    config.extra_prompt = EnvString("CCME_AI_PROMPT", CCME_AI_PROMPT);

    if (auto prompt = FindPromptFile()) {
        config.prompt = std::move(prompt->text);
        config.prompt_source = std::move(prompt->path);
    } else {
        std::cerr << "[CAMERA] Warning: AI prompt file not found; create "
                     "~/.config/ccme/ai_prompt.txt or set "
                     "CCME_AI_PROMPT_FILE\n";
    }

    return config;
}

[[nodiscard]] CameraError MapVisionError(ai::VisionError error) {
    switch (error) {
        case ai::VisionError::kNotConfigured:
            return CameraError::kAiNotConfigured;
        case ai::VisionError::kInvalidRequest:
            return CameraError::kProcessingFailed;
        case ai::VisionError::kNetworkError:
        case ai::VisionError::kHttpError:
            return CameraError::kAiRequestFailed;
        case ai::VisionError::kInvalidResponse:
            return CameraError::kAiResponseInvalid;
    }
    return CameraError::kProcessingFailed;
}

#ifdef CCME_HAS_OPENCV
void PrepareFrameForUpload(cv::Mat& frame) {
    constexpr int kMaxSide = CCME_AI_IMAGE_MAX_SIDE;
    if (kMaxSide <= 0) {
        return;
    }

    const int longest_side = std::max(frame.cols, frame.rows);
    if (longest_side <= kMaxSide) {
        return;
    }

    const double scale = static_cast<double>(kMaxSide) / longest_side;
    cv::resize(frame, frame, cv::Size(), scale, scale, cv::INTER_AREA);
}
#endif

}  // namespace

struct DissolutionDetector::Impl {
    mutable std::mutex mutex;
    bool connected{false};
    std::string stream_url{CCME_CAMERA_STREAM_URL};
    ai::OpenAiVisionClient vision{BuildVisionConfig()};

#ifdef CCME_HAS_OPENCV
    cv::VideoCapture capture;
#endif

    Impl() {
        std::cerr << "[CAMERA] Initialized: stream=" << stream_url
                  << " model=" << vision.Config().model
                  << " api=" << vision.Config().api_url
                  << " key="
                  << (vision.Config().api_key.empty() ? "not set" : "set")
                  << " prompt="
                  << (vision.Config().prompt.empty()
                          ? "missing"
                          : vision.Config().prompt_source)
                  << "\n";
        if (vision.Config().api_key.empty()) {
            std::cerr << "[CAMERA] Warning: no API key configured; set "
                         "CCME_AI_API_KEY unless the endpoint is key-less\n";
        }
        if (!vision.Config().prompt.empty()) {
            std::cerr << "[CAMERA] AI prompt loaded: "
                      << vision.Config().prompt.size() << " bytes\n";
        }
    }

    bool OpenStreamLocked() {
#ifdef CCME_HAS_OPENCV
        if (capture.isOpened()) {
            connected = true;
            return true;
        }

        std::cerr << "[CAMERA] Opening stream: " << stream_url << "\n";
        if (!capture.open(stream_url, cv::CAP_FFMPEG)) {
            std::cerr << "[CAMERA] Stream open failed\n";
            connected = false;
            return false;
        }

        capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
        connected = true;
        std::cerr << "[CAMERA] Stream opened\n";
        return true;
#else
        std::cerr << "[CAMERA] OpenCV is not available; cannot capture frames\n";
        return false;
#endif
    }

    void CloseStreamLocked() {
        if (!connected) {
            return;
        }
#ifdef CCME_HAS_OPENCV
        capture.release();
#endif
        connected = false;
        std::cerr << "[CAMERA] Stream closed\n";
    }
};

DissolutionDetector::DissolutionDetector()
    : impl_(std::make_unique<Impl>()) {}

DissolutionDetector::~DissolutionDetector() {
    Disconnect();
}

DissolutionDetector::DissolutionDetector(DissolutionDetector&&) noexcept =
    default;
DissolutionDetector& DissolutionDetector::operator=(
    DissolutionDetector&&) noexcept = default;

std::expected<bool, CameraError> DissolutionDetector::Connect() {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    if (impl_->connected) {
        std::cerr << "[CAMERA] Already connected\n";
        return true;
    }

    if (!impl_->OpenStreamLocked()) {
        return std::unexpected(CameraError::kConnectionFailed);
    }
    return true;
}

void DissolutionDetector::Disconnect() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->CloseStreamLocked();
}

std::expected<DetectionResult, CameraError>
DissolutionDetector::CheckDissolution() {
    if (!impl_->vision.IsConfigured()) {
        std::cerr << "[CAMERA] AI is not configured "
                     "(set CCME_AI_API_URL / CCME_AI_MODEL / CCME_AI_API_KEY "
                     "and provide ~/.config/ccme/ai_prompt.txt)\n";
        return std::unexpected(CameraError::kAiNotConfigured);
    }

#ifdef CCME_HAS_OPENCV
    std::vector<std::vector<std::uint8_t>> frames;

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (!impl_->connected && !impl_->OpenStreamLocked()) {
            return std::unexpected(CameraError::kConnectionFailed);
        }

        constexpr int kFrameCount =
            CCME_AI_FRAME_COUNT > 0 ? CCME_AI_FRAME_COUNT : 1;
        frames.reserve(static_cast<std::size_t>(kFrameCount));

        // Discard a potentially buffered stale frame.
        impl_->capture.grab();

        for (int i = 0; i < kFrameCount; ++i) {
            cv::Mat frame;
            if (!impl_->capture.read(frame) || frame.empty()) {
                std::cerr << "[CAMERA] Frame capture failed, reconnecting\n";
                impl_->CloseStreamLocked();
                if (!impl_->OpenStreamLocked() ||
                    !impl_->capture.read(frame) || frame.empty()) {
                    return std::unexpected(CameraError::kFrameCaptureFailed);
                }
            }

            PrepareFrameForUpload(frame);

            std::vector<uchar> jpeg;
            const std::vector<int> params{
                cv::IMWRITE_JPEG_QUALITY, CCME_AI_JPEG_QUALITY};
            if (!cv::imencode(".jpg", frame, jpeg, params)) {
                std::cerr << "[CAMERA] JPEG encoding failed\n";
                return std::unexpected(CameraError::kProcessingFailed);
            }
            frames.emplace_back(jpeg.begin(), jpeg.end());

            if (i + 1 < kFrameCount) {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
        }
    }

    std::cerr << "[CAMERA] Sending " << frames.size()
              << " frame(s) to AI model " << impl_->vision.Config().model
              << "\n";

    ai::VisionRequest request;
    request.jpeg_frames = std::move(frames);

    auto verdict = impl_->vision.Analyze(request);
    if (!verdict) {
        std::cerr << "[CAMERA] AI request failed ("
                  << ai::ToString(verdict.error().code) << "): "
                  << verdict.error().detail << "\n";
        return std::unexpected(MapVisionError(verdict.error().code));
    }

    std::cerr << "[CAMERA] AI verdict: dissolved="
              << (verdict->dissolved ? "yes" : "no")
              << " confidence=" << verdict->confidence
              << " reasoning=" << verdict->reasoning << "\n";

    return DetectionResult{
        verdict->dissolved,
        verdict->confidence,
        std::move(verdict->reasoning),
    };
#else
    std::cerr << "[CAMERA] OpenCV is not available; cannot capture frames\n";
    return std::unexpected(CameraError::kProcessingFailed);
#endif
}

bool DissolutionDetector::IsConnected() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->connected;
}

bool DissolutionDetector::IsAiConfigured() const {
    return impl_->vision.IsConfigured();
}

const char* ToString(CameraError error) noexcept {
    switch (error) {
        case CameraError::kStreamNotFound: return "stream not found";
        case CameraError::kConnectionFailed: return "connection failed";
        case CameraError::kFrameCaptureFailed: return "frame capture failed";
        case CameraError::kProcessingFailed: return "processing failed";
        case CameraError::kAiNotConfigured: return "AI API not configured";
        case CameraError::kAiRequestFailed: return "AI request failed";
        case CameraError::kAiResponseInvalid: return "AI response invalid";
    }
    return "unknown";
}

}  // namespace ccme::camera
