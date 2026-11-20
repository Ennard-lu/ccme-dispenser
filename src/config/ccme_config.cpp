#include "config/ccme_config.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ccme::config {

namespace {

[[nodiscard]] bool FileIsReadable(const std::string& path) {
    std::ifstream file(path);
    return file.good();
}

[[nodiscard]] std::optional<std::string> FindConfigFile(
    std::string& warning) {
    if (const char* explicit_path = std::getenv("CCME_CONFIG_PATH");
        explicit_path != nullptr && *explicit_path != '\0') {
        if (FileIsReadable(explicit_path)) {
            return std::string{explicit_path};
        }
        warning = "CCME_CONFIG_PATH=" + std::string{explicit_path} +
                  " cannot be opened";
        return std::nullopt;
    }

    std::vector<std::string> candidates{
        "/etc/ccme/ccme.json",
        "/usr/etc/ccme/ccme.json",
        "/usr/local/etc/ccme/ccme.json",
    };

    if (const char* home = std::getenv("HOME");
        home != nullptr && *home != '\0') {
        candidates.push_back(std::string{home} +
                             "/.local/etc/ccme/ccme.json");
    }

    candidates.emplace_back("config/ccme.json");
    candidates.emplace_back("ccme.json");

    for (const auto& path : candidates) {
        if (FileIsReadable(path)) {
            return path;
        }
    }

    warning = "no vial config file found; using built-in defaults";
    return std::nullopt;
}

void AddWarning(std::string& warning, std::string message) {
    if (!warning.empty()) {
        warning += "; ";
    }
    warning += std::move(message);
}

[[nodiscard]] int ReadInt(const nlohmann::json& object, const char* key,
                          int fallback, std::string& warning) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (!object[key].is_number()) {
        AddWarning(warning, std::string{key} + " must be a number");
        return fallback;
    }
    return object[key].get<int>();
}

[[nodiscard]] double ReadDouble(const nlohmann::json& object,
                                const char* key, double fallback,
                                std::string& warning) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (!object[key].is_number()) {
        AddWarning(warning, std::string{key} + " must be a number");
        return fallback;
    }
    return object[key].get<double>();
}

}  // namespace

LoadedConfig LoadConfig() {
    LoadedConfig result;

    std::string warning;
    const auto path = FindConfigFile(warning);
    if (!path) {
        result.warning = warning;
        std::cerr << "[CONFIG] " << warning << "\n"
                  << "[CONFIG] Vial matrix source=built-in defaults"
                  << " rows=" << result.vial_matrix.rows
                  << " cols=" << result.vial_matrix.cols
                  << " spacing=(" << result.vial_matrix.spacing_x << ", "
                  << result.vial_matrix.spacing_y << ")"
                  << " origin=(" << result.vial_matrix.origin_x << ", "
                  << result.vial_matrix.origin_y << ", "
                  << result.vial_matrix.origin_z << ")\n";
        return result;
    }

    result.source_path = *path;

    std::ifstream input(*path);
    if (!input) {
        result.warning = "cannot open " + *path;
        std::cerr << "[CONFIG] " << result.warning
                  << "; using built-in defaults\n";
        return result;
    }

    const auto root = nlohmann::json::parse(input, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        result.warning = "invalid JSON in " + *path;
        std::cerr << "[CONFIG] " << result.warning
                  << "; using built-in defaults\n";
        return result;
    }

    const nlohmann::json* vial = &root;
    if (root.contains("vial_matrix")) {
        if (!root["vial_matrix"].is_object()) {
            result.warning = "vial_matrix must be a JSON object in " + *path;
            std::cerr << "[CONFIG] " << result.warning
                      << "; using built-in defaults\n";
            return result;
        }
        vial = &root["vial_matrix"];
    }

    auto& config = result.vial_matrix;
    config.rows = ReadInt(*vial, "rows", config.rows, warning);
    config.cols = ReadInt(*vial, "cols", config.cols, warning);
    config.spacing_x =
        ReadDouble(*vial, "spacing_x", config.spacing_x, warning);
    config.spacing_y =
        ReadDouble(*vial, "spacing_y", config.spacing_y, warning);
    config.origin_x =
        ReadDouble(*vial, "origin_x", config.origin_x, warning);
    config.origin_y =
        ReadDouble(*vial, "origin_y", config.origin_y, warning);
    config.origin_z =
        ReadDouble(*vial, "origin_z", config.origin_z, warning);

    if (config.rows <= 0) {
        AddWarning(warning, "rows must be greater than zero");
        config.rows = VialMatrixConfig{}.rows;
    }
    if (config.cols <= 0) {
        AddWarning(warning, "cols must be greater than zero");
        config.cols = VialMatrixConfig{}.cols;
    }

    result.warning = warning;
    if (!result.warning.empty()) {
        std::cerr << "[CONFIG] Warning: " << result.warning << "\n";
    }
    std::cerr << "[CONFIG] Vial matrix source=" << result.source_path
              << " rows=" << config.rows
              << " cols=" << config.cols
              << " spacing=(" << config.spacing_x << ", "
              << config.spacing_y << ")"
              << " origin=(" << config.origin_x << ", "
              << config.origin_y << ", " << config.origin_z << ")\n";

    return result;
}

}  // namespace ccme::config
