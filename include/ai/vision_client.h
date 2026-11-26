#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace ccme::ai {

enum class VisionError {
    kNotConfigured,
    kInvalidRequest,
    kNetworkError,
    kHttpError,
    kInvalidResponse,
};

struct VisionErrorInfo {
    VisionError code{VisionError::kInvalidRequest};
    int http_status{0};
    std::string detail;
};

struct VisionConfig {
    // Full OpenAI-compatible chat-completions URL, e.g.
    // https://api.openai.com/v1/chat/completions
    std::string api_url;
    // Bearer token. May be empty for key-less local OpenAI-compatible servers.
    std::string api_key;
    // Vision-capable model name, e.g. gpt-4o-mini.
    std::string model;
    int timeout_sec{30};
    int max_tokens{300};
    // Send "thinking": {"type": "disabled"} in the request. Required by
    // reasoning-capable models such as Doubao-Seed-2.0-mini when low latency
    // matters. Leave false for plain OpenAI endpoints that reject the field.
    bool disable_thinking{false};
    // System prompt loaded from ~/.config/ccme/ai_prompt.txt (or an explicit
    // CCME_AI_PROMPT_FILE). No prompt is hard-coded in the binary.
    std::string prompt;
    // Path the prompt was loaded from, for diagnostics.
    std::string prompt_source;
    // Optional extra instructions appended to the prompt file content.
    std::string extra_prompt;
};

struct VisionRequest {
    // JPEG-encoded frames, sent as image_url data URLs in request order.
    std::vector<std::vector<std::uint8_t>> jpeg_frames;
};

struct VisionVerdict {
    bool dissolved{false};
    double confidence{0.0};
    std::string reasoning;
};

class OpenAiVisionClient {
public:
    explicit OpenAiVisionClient(VisionConfig config);

    [[nodiscard]] bool IsConfigured() const noexcept;
    [[nodiscard]] const VisionConfig& Config() const noexcept { return config_; }

    std::expected<VisionVerdict, VisionErrorInfo> Analyze(
        const VisionRequest& request) const;

private:
    VisionConfig config_;
};

[[nodiscard]] std::string Base64Encode(
    std::span<const std::uint8_t> data);

[[nodiscard]] const char* ToString(VisionError error) noexcept;

}  // namespace ccme::ai
