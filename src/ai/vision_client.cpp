#include "ai/vision_client.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ccme::ai {

namespace {

struct Endpoint {
    std::string origin;
    std::string path;
};

[[nodiscard]] std::string ToLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

[[nodiscard]] std::optional<Endpoint> ParseEndpoint(std::string_view url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::nullopt;
    }

    std::string scheme = ToLower(std::string{url.substr(0, scheme_end)});
    if (scheme != "http" && scheme != "https") {
        return std::nullopt;
    }

    const std::string_view rest = url.substr(scheme_end + 3);
    if (rest.empty()) {
        return std::nullopt;
    }

    const auto path_pos = rest.find_first_of("/?#");
    const std::string_view authority = rest.substr(0, path_pos);
    if (authority.empty()) {
        return std::nullopt;
    }

    std::string path =
        path_pos == std::string_view::npos
            ? std::string{}
            : std::string{rest.substr(path_pos)};

    if (path.empty()) {
        path = "/";
    }
    if (path.front() != '/') {
        path.insert(path.begin(), '/');
    }

    // Separate the query string, then normalize common base URLs.
    std::string query;
    if (const auto query_pos = path.find('?');
        query_pos != std::string::npos) {
        query = path.substr(query_pos);
        path.erase(query_pos);
    }
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }

    // Accept both a full endpoint URL and a base URL such as
    // https://api.openai.com, https://api.openai.com/v1 or
    // https://ark.cn-beijing.volces.com/api/v3.
    if (path == "/") {
        path = "/v1/chat/completions";
    } else if (path == "/v1" || path == "/v3" || path == "/api/v3") {
        path += "/chat/completions";
    }
    path += query;

    return Endpoint{
        scheme + "://" + std::string{authority},
        std::move(path),
    };
}

[[nodiscard]] std::string ExtractMessageText(const nlohmann::json& choice) {
    if (!choice.is_object()) {
        return {};
    }

    if (choice.contains("message") && choice["message"].is_object()) {
        const auto& message = choice["message"];
        if (message.contains("content")) {
            const auto& content = message["content"];
            if (content.is_string()) {
                return content.get<std::string>();
            }
            if (content.is_array()) {
                std::string text;
                for (const auto& part : content) {
                    if (!part.is_object() || !part.contains("type") ||
                        !part["type"].is_string() ||
                        part["type"].get<std::string>() != "text" ||
                        !part.contains("text") || !part["text"].is_string()) {
                        continue;
                    }
                    text += part["text"].get<std::string>();
                }
                return text;
            }
        }
    }

    // Some OpenAI-compatible servers use a completion-style response.
    if (choice.contains("text") && choice["text"].is_string()) {
        return choice["text"].get<std::string>();
    }
    return {};
}

[[nodiscard]] std::optional<bool> ParseDissolved(const nlohmann::json& value) {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_number_integer()) {
        return value.get<long long>() != 0;
    }
    if (value.is_string()) {
        const std::string text = ToLower(value.get<std::string>());
        if (text == "true" || text == "yes" || text == "1" || text == "是" ||
            text == "已溶解") {
            return true;
        }
        if (text == "false" || text == "no" || text == "0" || text == "否" ||
            text == "未溶解") {
            return false;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<VisionVerdict> ParseVerdict(
    const std::string& content) {
    const auto begin = content.find('{');
    const auto end = content.rfind('}');
    if (begin == std::string::npos || end == std::string::npos || end <= begin) {
        return std::nullopt;
    }

    const auto parsed = nlohmann::json::parse(
        content.substr(begin, end - begin + 1), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() ||
        !parsed.contains("dissolved")) {
        return std::nullopt;
    }

    const auto dissolved = ParseDissolved(parsed["dissolved"]);
    if (!dissolved) {
        return std::nullopt;
    }

    VisionVerdict verdict;
    verdict.dissolved = *dissolved;

    if (parsed.contains("confidence")) {
        const auto& confidence = parsed["confidence"];
        if (confidence.is_number()) {
            verdict.confidence = confidence.get<double>();
        } else if (confidence.is_string()) {
            try {
                verdict.confidence = std::stod(confidence.get<std::string>());
            } catch (...) {
                verdict.confidence = 0.0;
            }
        }
        verdict.confidence = std::clamp(verdict.confidence, 0.0, 1.0);
    }

    for (const char* key : {"reasoning", "reason", "explanation"}) {
        if (parsed.contains(key) && parsed[key].is_string()) {
            verdict.reasoning = parsed[key].get<std::string>();
            break;
        }
    }

    return verdict;
}

[[nodiscard]] std::string BuildRequestBody(const VisionConfig& config,
                                           const VisionRequest& request) {
    std::string system_prompt = config.prompt;
    if (!config.extra_prompt.empty()) {
        system_prompt += "\n\n额外要求：" + config.extra_prompt;
    }

    nlohmann::json system_message = nlohmann::json::object();
    system_message["role"] = "system";
    system_message["content"] = std::move(system_prompt);

    nlohmann::json user_content = nlohmann::json::array();
    nlohmann::json instruction = nlohmann::json::object();
    instruction["type"] = "text";
    instruction["text"] =
        "以下是按时间顺序从烧杯上方拍摄的 " +
        std::to_string(request.jpeg_frames.size()) +
        " 帧图像。请判断固体是否已经完全溶解。";
    user_content.push_back(std::move(instruction));

    for (const auto& frame : request.jpeg_frames) {
        nlohmann::json image_url = nlohmann::json::object();
        image_url["url"] =
            "data:image/jpeg;base64," + Base64Encode(frame);

        nlohmann::json image = nlohmann::json::object();
        image["type"] = "image_url";
        image["image_url"] = std::move(image_url);
        user_content.push_back(std::move(image));
    }

    nlohmann::json user_message = nlohmann::json::object();
    user_message["role"] = "user";
    user_message["content"] = std::move(user_content);

    nlohmann::json messages = nlohmann::json::array();
    messages.push_back(std::move(system_message));
    messages.push_back(std::move(user_message));

    nlohmann::json body = nlohmann::json::object();
    body["model"] = config.model;
    body["messages"] = std::move(messages);
    body["temperature"] = 0;
    if (config.max_tokens > 0) {
        body["max_tokens"] = config.max_tokens;
    }
    if (config.disable_thinking) {
        body["thinking"] = nlohmann::json::object({{"type", "disabled"}});
    }
    return body.dump();
}

[[nodiscard]] VisionErrorInfo MakeError(VisionError code, int http_status,
                                        std::string detail) {
    return VisionErrorInfo{code, http_status, std::move(detail)};
}

}  // namespace

OpenAiVisionClient::OpenAiVisionClient(VisionConfig config)
    : config_(std::move(config)) {}

bool OpenAiVisionClient::IsConfigured() const noexcept {
    return !config_.api_url.empty() && !config_.model.empty() &&
           !config_.prompt.empty();
}

std::expected<VisionVerdict, VisionErrorInfo> OpenAiVisionClient::Analyze(
    const VisionRequest& request) const {
    if (!IsConfigured()) {
        return std::unexpected(MakeError(
            VisionError::kNotConfigured, 0,
            "AI API URL, model, or prompt file is not configured"));
    }
    if (request.jpeg_frames.empty()) {
        return std::unexpected(MakeError(
            VisionError::kInvalidRequest, 0, "No image frames provided"));
    }

    const auto endpoint = ParseEndpoint(config_.api_url);
    if (!endpoint) {
        return std::unexpected(MakeError(
            VisionError::kNotConfigured, 0,
            "Invalid AI API URL: " + config_.api_url));
    }

    try {
        httplib::Client client(endpoint->origin);
        const int timeout = config_.timeout_sec > 0 ? config_.timeout_sec : 30;
        client.set_connection_timeout(timeout);
        client.set_read_timeout(timeout);
        client.set_write_timeout(timeout);
        client.set_follow_location(true);

        httplib::Headers headers = {{"Accept", "application/json"}};
        if (!config_.api_key.empty()) {
            headers.emplace("Authorization", "Bearer " + config_.api_key);
        }

        const std::string body = BuildRequestBody(config_, request);
        auto response = client.Post(endpoint->path, headers, body,
                                    "application/json");
        if (!response) {
            return std::unexpected(MakeError(
                VisionError::kNetworkError, 0,
                "HTTP request failed: " + httplib::to_string(response.error())));
        }

        if (response->status < 200 || response->status >= 300) {
            std::string detail = "AI API returned HTTP " +
                                 std::to_string(response->status);
            if (!response->body.empty()) {
                detail += ": ";
                detail += response->body.substr(
                    0, std::min<std::size_t>(response->body.size(), 512));
            }
            return std::unexpected(MakeError(
                VisionError::kHttpError, response->status, std::move(detail)));
        }

        const auto json = nlohmann::json::parse(
            response->body, nullptr, false);
        if (json.is_discarded() || !json.is_object()) {
            return std::unexpected(MakeError(
                VisionError::kInvalidResponse, response->status,
                "AI response is not a valid JSON object"));
        }

        if (json.contains("error")) {
            return std::unexpected(MakeError(
                VisionError::kHttpError, response->status,
                "AI API error: " + json["error"].dump()));
        }

        if (!json.contains("choices") || !json["choices"].is_array() ||
            json["choices"].empty()) {
            return std::unexpected(MakeError(
                VisionError::kInvalidResponse, response->status,
                "AI response contains no choices"));
        }

        const std::string content =
            ExtractMessageText(json["choices"][0]);
        if (content.empty()) {
            return std::unexpected(MakeError(
                VisionError::kInvalidResponse, response->status,
                "AI response contains no text content"));
        }

        auto verdict = ParseVerdict(content);
        if (!verdict) {
            return std::unexpected(MakeError(
                VisionError::kInvalidResponse, response->status,
                "Could not parse verdict from AI content: " + content));
        }

        return *verdict;
    } catch (const std::exception& e) {
        return std::unexpected(MakeError(
            VisionError::kNetworkError, 0,
            std::string("AI request exception: ") + e.what()));
    }
}

std::string Base64Encode(std::span<const std::uint8_t> data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((data.size() + 2) / 3) * 4);

    std::size_t index = 0;
    while (index + 2 < data.size()) {
        const std::uint32_t block =
            (static_cast<std::uint32_t>(data[index]) << 16) |
            (static_cast<std::uint32_t>(data[index + 1]) << 8) |
            static_cast<std::uint32_t>(data[index + 2]);
        output.push_back(kAlphabet[(block >> 18) & 0x3Fu]);
        output.push_back(kAlphabet[(block >> 12) & 0x3Fu]);
        output.push_back(kAlphabet[(block >> 6) & 0x3Fu]);
        output.push_back(kAlphabet[block & 0x3Fu]);
        index += 3;
    }

    const std::size_t remaining = data.size() - index;
    if (remaining == 1) {
        const std::uint32_t block =
            static_cast<std::uint32_t>(data[index]) << 16;
        output.push_back(kAlphabet[(block >> 18) & 0x3Fu]);
        output.push_back(kAlphabet[(block >> 12) & 0x3Fu]);
        output.push_back('=');
        output.push_back('=');
    } else if (remaining == 2) {
        const std::uint32_t block =
            (static_cast<std::uint32_t>(data[index]) << 16) |
            (static_cast<std::uint32_t>(data[index + 1]) << 8);
        output.push_back(kAlphabet[(block >> 18) & 0x3Fu]);
        output.push_back(kAlphabet[(block >> 12) & 0x3Fu]);
        output.push_back(kAlphabet[(block >> 6) & 0x3Fu]);
        output.push_back('=');
    }

    return output;
}

const char* ToString(VisionError error) noexcept {
    switch (error) {
        case VisionError::kNotConfigured: return "not configured";
        case VisionError::kInvalidRequest: return "invalid request";
        case VisionError::kNetworkError: return "network error";
        case VisionError::kHttpError: return "http error";
        case VisionError::kInvalidResponse: return "invalid response";
    }
    return "unknown";
}

}  // namespace ccme::ai
