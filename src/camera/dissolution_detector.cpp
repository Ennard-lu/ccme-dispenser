#include "camera/dissolution_detector.h"

#include <cmath>
#include <vector>

#ifdef CCME_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace ccme::camera {

namespace {

#ifdef CCME_HAS_OPENCV

struct LogisticRegression {
    float w_circle_radius{0.5f};
    float w_circle_count{-0.3f};
    float w_circle_density{0.8f};
    float bias{-2.0f};

    bool Predict(float radius_avg, int count, float density) const {
        float z = w_circle_radius * radius_avg +
                  w_circle_count * static_cast<float>(count) +
                  w_circle_density * density + bias;
        float prob = 1.0f / (1.0f + std::exp(-z));
        return prob < 0.5f;
    }
};

constexpr int kMaxCircles = 50;
constexpr float kMinRadius = 5.0f;
constexpr float kMaxRadius = 100.0f;
constexpr float kMinDist = 20.0f;

float DetectCircleRatio(cv::Mat& frame, int& circle_count) {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(9, 9), 2.0);

    std::vector<cv::Vec3f> circles;
    cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT,
                     1.5, kMinDist, 100, 30,
                     static_cast<int>(kMinRadius),
                     static_cast<int>(kMaxRadius));

    circle_count = static_cast<int>(circles.size());
    if (circle_count == 0) return 0.0f;

    float total_area = 0.0f;
    float frame_area = static_cast<float>(frame.cols * frame.rows);
    for (const auto& c : circles) {
        float r = c[2];
        total_area += 3.14159f * r * r;
    }
    return total_area / frame_area;
}

#endif

}  // namespace

struct DissolutionDetector::Impl {
    bool connected{false};
    std::string stream_url;
#ifdef CCME_HAS_OPENCV
    cv::VideoCapture cap;
    LogisticRegression model;
#endif

    Impl() {
#ifdef CCME_HAS_OPENCV
        stream_url = CCME_CAMERA_STREAM_URL;
#endif
    }
};

DissolutionDetector::DissolutionDetector()
    : impl_(std::make_unique<Impl>()) {}

DissolutionDetector::~DissolutionDetector() {
    Disconnect();
}

DissolutionDetector::DissolutionDetector(DissolutionDetector&&) noexcept = default;
DissolutionDetector& DissolutionDetector::operator=(DissolutionDetector&&) noexcept = default;

std::expected<bool, CameraError> DissolutionDetector::Connect() {
    if (impl_->connected) {
        return true;
    }

#ifdef CCME_HAS_OPENCV
    if (!impl_->cap.open(impl_->stream_url, cv::CAP_FFMPEG)) {
        return std::unexpected(CameraError::kConnectionFailed);
    }
    impl_->connected = true;
    return true;
#else
    return std::unexpected(CameraError::kConnectionFailed);
#endif
}

void DissolutionDetector::Disconnect() {
    if (impl_->connected) {
#ifdef CCME_HAS_OPENCV
        impl_->cap.release();
#endif
        impl_->connected = false;
    }
}

std::expected<bool, CameraError> DissolutionDetector::CheckDissolution() {
    if (!impl_->connected) {
        return std::unexpected(CameraError::kConnectionFailed);
    }

#ifdef CCME_HAS_OPENCV
    cv::Mat frame;
    if (!impl_->cap.read(frame) || frame.empty()) {
        return std::unexpected(CameraError::kFrameCaptureFailed);
    }

    int circle_count = 0;
    float density = DetectCircleRatio(frame, circle_count);

    float avg_radius = 0.0f;
    if (circle_count > 0) {
        std::vector<cv::Vec3f> circles;
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, gray, cv::Size(9, 9), 2.0);
        cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT,
                         1.5, kMinDist, 100, 30,
                         static_cast<int>(kMinRadius),
                         static_cast<int>(kMaxRadius));
        float sum_r = 0.0f;
        for (const auto& c : circles) {
            sum_r += c[2];
        }
        avg_radius = sum_r / static_cast<float>(circle_count);
    }

    return impl_->model.Predict(avg_radius, circle_count, density);
#else
    return false;
#endif
}

}  // namespace ccme::camera
