#pragma once

#include <string>

namespace ccme::config {

// Runtime configuration for the vial matrix. Values are loaded from a JSON
// file at process startup; the defaults below are only used when no config
// file can be found.
struct VialMatrixConfig {
    int rows{6};
    int cols{6};
    double spacing_x{-750.0};
    double spacing_y{-750.0};
    double origin_x{-50.0};
    double origin_y{-450.0};
    double origin_z{0.0};

    [[nodiscard]] int total_vials() const { return rows * cols; }
};

struct LoadedConfig {
    VialMatrixConfig vial_matrix;
    // Empty when the built-in defaults were used.
    std::string source_path;
    // Non-empty when the config file was missing or partially invalid.
    std::string warning;
};

// Search order:
//   1. CCME_CONFIG_PATH environment variable (explicit file)
//   2. /etc/ccme/ccme.json
//   3. /usr/etc/ccme/ccme.json
//   4. /usr/local/etc/ccme/ccme.json
//   5. $HOME/.local/etc/ccme/ccme.json
//   6. ./config/ccme.json (development tree)
//   7. ./ccme.json
[[nodiscard]] LoadedConfig LoadConfig();

}  // namespace ccme::config
