cmake_minimum_required(VERSION 3.25)
project(ccme-dispenser
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "CCME Automated Solution Dispenser"
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler flags
add_compile_options(-Wall -Wextra -Wpedantic)

# ──────────────────────────────────────────────
# Configurable options
# ──────────────────────────────────────────────

# Pump options
set(CCME_PUMP_PWM_PATH "/sys/class/pwm/pwmchip0" CACHE STRING "PWM device path")
set(CCME_PUMP_PWM_CHANNEL 0 CACHE STRING "PWM channel index")
set(CCME_PUMP_GPIO_DIR_PIN 18 CACHE STRING "GPIO direction pin number")
set(CCME_PUMP_FLOW_RATE 1.0 CACHE STRING "Peristaltic pump flow rate (ml/s)")
set(CCME_PUMP_PWM_PERIOD_US 20000 CACHE STRING "PWM period in microseconds")

# Stirrer options
set(CCME_STIRRER_SERIAL_PORT "/dev/ttyS1" CACHE STRING "Serial port device path")
set(CCME_STIRRER_BAUD_RATE 9600 CACHE STRING "Serial port baud rate")

# Camera options
set(CCME_CAMERA_STREAM_URL "tcp://127.0.0.1:2778" CACHE STRING "GStreamer video stream URL")
set(CCME_CAMERA_CHECK_INTERVAL_MS 1000 CACHE STRING "Dissolution check interval (ms)")

# FMC options
set(CCME_FMC_CONTROLLER_IP "192.168.1.10" CACHE STRING "FMC4030 controller IP address")
set(CCME_FMC_CONTROLLER_PORT 8080 CACHE STRING "FMC4030 controller port")
set(CCME_FMC_CARD_ID 0 CACHE STRING "FMC4030 card ID")

# Web options
set(CCME_WEB_HTTP_PORT 8080 CACHE STRING "HTTP server port")
set(CCME_WEB_LISTEN_ADDRESS "0.0.0.0" CACHE STRING "HTTP server listen address")

# Vial matrix options
set(CCME_VIAL_ROWS 4 CACHE STRING "Number of vial rows")
set(CCME_VIAL_COLS 4 CACHE STRING "Number of vial columns")
set(CCME_VIAL_SPACING_X 50.0 CACHE STRING "X-axis spacing between vials (mm)")
set(CCME_VIAL_SPACING_Y 50.0 CACHE STRING "Y-axis spacing between vials (mm)")
set(CCME_VIAL_ORIGIN_X 0.0 CACHE STRING "X coordinate of first vial (mm)")
set(CCME_VIAL_ORIGIN_Y 0.0 CACHE STRING "Y coordinate of first vial (mm)")

# ──────────────────────────────────────────────
# Third-party dependencies (git submodules)
# ──────────────────────────────────────────────

# sdbus-c++
set(SDBUS_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SDBUS_CPP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(SDBUS_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(SDBUS_CPP_BUILD_LIBSYSTEMD OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/sdbus-cpp)

# nlohmann/json
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/json)

# cpp-httplib (header-only HTTP server)
set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/cpp-httplib)

find_package(Threads REQUIRED)

# ──────────────────────────────────────────────
# FMC4030 SDK
# ──────────────────────────────────────────────

add_library(fmc4030_sdk SHARED IMPORTED)
set_target_properties(fmc4030_sdk PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/3rdparty/FMC_Controler/lib/libFMC4030_2009_1.so"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/3rdparty/FMC_Controler/include"
)

# ──────────────────────────────────────────────
# Generate compile definitions from options
# ──────────────────────────────────────────────

add_compile_definitions(
    CCME_PUMP_PWM_PATH="${CCME_PUMP_PWM_PATH}"
    CCME_PUMP_PWM_CHANNEL=${CCME_PUMP_PWM_CHANNEL}
    CCME_PUMP_GPIO_DIR_PIN=${CCME_PUMP_GPIO_DIR_PIN}
    CCME_PUMP_FLOW_RATE=${CCME_PUMP_FLOW_RATE}
    CCME_PUMP_PWM_PERIOD_US=${CCME_PUMP_PWM_PERIOD_US}

    CCME_STIRRER_SERIAL_PORT="${CCME_STIRRER_SERIAL_PORT}"
    CCME_STIRRER_BAUD_RATE=${CCME_STIRRER_BAUD_RATE}

    CCME_CAMERA_STREAM_URL="${CCME_CAMERA_STREAM_URL}"
    CCME_CAMERA_CHECK_INTERVAL_MS=${CCME_CAMERA_CHECK_INTERVAL_MS}

    CCME_FMC_CONTROLLER_IP="${CCME_FMC_CONTROLLER_IP}"
    CCME_FMC_CONTROLLER_PORT=${CCME_FMC_CONTROLLER_PORT}
    CCME_FMC_CARD_ID=${CCME_FMC_CARD_ID}

    CCME_WEB_HTTP_PORT=${CCME_WEB_HTTP_PORT}
    CCME_WEB_LISTEN_ADDRESS="${CCME_WEB_LISTEN_ADDRESS}"

    CCME_VIAL_ROWS=${CCME_VIAL_ROWS}
    CCME_VIAL_COLS=${CCME_VIAL_COLS}
    CCME_VIAL_SPACING_X=${CCME_VIAL_SPACING_X}
    CCME_VIAL_SPACING_Y=${CCME_VIAL_SPACING_Y}
    CCME_VIAL_ORIGIN_X=${CCME_VIAL_ORIGIN_X}
    CCME_VIAL_ORIGIN_Y=${CCME_VIAL_ORIGIN_Y}
)

# ──────────────────────────────────────────────
# Subdirectories (modules)
# ──────────────────────────────────────────────

add_subdirectory(src/pump)
add_subdirectory(src/stirrer)
add_subdirectory(src/camera)
add_subdirectory(src/fmc)
add_subdirectory(src/web)
add_subdirectory(src/orchestrator)
