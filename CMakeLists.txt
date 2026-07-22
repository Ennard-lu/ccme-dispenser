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

# Pump 0 options
set(CCME_PUMP0_PWM_PATH "/sys/class/pwm/pwmchip0" CACHE STRING "Pump 0 PWM device path")
set(CCME_PUMP0_PWM_CHANNEL 0 CACHE STRING "Pump 0 PWM channel index")
set(CCME_PUMP0_GPIO_DIR_PIN 18 CACHE STRING "Pump 0 GPIO direction pin")
set(CCME_PUMP0_FLOW_RATE 1.0 CACHE STRING "Pump 0 flow rate (ml/s)")
set(CCME_PUMP0_PWM_PERIOD_US 20000 CACHE STRING "Pump 0 PWM period (us)")

# Pump 1 options
set(CCME_PUMP1_PWM_PATH "/sys/class/pwm/pwmchip2" CACHE STRING "Pump 1 PWM device path")
set(CCME_PUMP1_PWM_CHANNEL 0 CACHE STRING "Pump 1 PWM channel index")
set(CCME_PUMP1_GPIO_DIR_PIN 19 CACHE STRING "Pump 1 GPIO direction pin")
set(CCME_PUMP1_FLOW_RATE 1.0 CACHE STRING "Pump 1 flow rate (ml/s)")
set(CCME_PUMP1_PWM_PERIOD_US 20000 CACHE STRING "Pump 1 PWM period (us)")

# Stirrer options
set(CCME_STIRRER_SERIAL_PORT "/dev/ttyS1" CACHE STRING "Serial port device path")
set(CCME_STIRRER_BAUD_RATE 9600 CACHE STRING "Serial port baud rate")
set(CCME_STIRRER_HEAT_TEMP 50.0 CACHE STRING "Default heating temperature (°C)")

# Camera options
set(CCME_CAMERA_STREAM_URL "tcp://127.0.0.1:7777" CACHE STRING "GStreamer video stream URL")
set(CCME_CAMERA_CHECK_INTERVAL_MS 1000 CACHE STRING "Dissolution check interval (ms)")

# FMC options
set(CCME_FMC_CONTROLLER_IP "192.168.1.10" CACHE STRING "FMC4030 controller IP address")
set(CCME_FMC_CONTROLLER_PORT 8088 CACHE STRING "FMC4030 controller port")
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
    CCME_PUMP0_PWM_PATH="${CCME_PUMP0_PWM_PATH}"
    CCME_PUMP0_PWM_CHANNEL=${CCME_PUMP0_PWM_CHANNEL}
    CCME_PUMP0_GPIO_DIR_PIN=${CCME_PUMP0_GPIO_DIR_PIN}
    CCME_PUMP0_FLOW_RATE=${CCME_PUMP0_FLOW_RATE}
    CCME_PUMP0_PWM_PERIOD_US=${CCME_PUMP0_PWM_PERIOD_US}
    CCME_PUMP1_PWM_PATH="${CCME_PUMP1_PWM_PATH}"
    CCME_PUMP1_PWM_CHANNEL=${CCME_PUMP1_PWM_CHANNEL}
    CCME_PUMP1_GPIO_DIR_PIN=${CCME_PUMP1_GPIO_DIR_PIN}
    CCME_PUMP1_FLOW_RATE=${CCME_PUMP1_FLOW_RATE}
    CCME_PUMP1_PWM_PERIOD_US=${CCME_PUMP1_PWM_PERIOD_US}

    CCME_STIRRER_SERIAL_PORT="${CCME_STIRRER_SERIAL_PORT}"
    CCME_STIRRER_BAUD_RATE=${CCME_STIRRER_BAUD_RATE}
    CCME_STIRRER_HEAT_TEMP=${CCME_STIRRER_HEAT_TEMP}

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

# ──────────────────────────────────────────────
# Install rules (split into backend / frontend components)
# Usage:
#   cmake --install build --component backend
#   cmake --install build --component frontend
#   cmake --install build                    # installs everything
# ──────────────────────────────────────────────

include(GNUInstallDirs)

set(CCME_INSTALL_FRONTEND_DIR "/var/www/ccme" CACHE STRING
    "Frontend install directory (relative to install prefix)")
set(CCME_INSTALL_NGINX_DIR "/nginx/sites-available" CACHE STRING
    "nginx config install directory (relative to install prefix)")
set(CCME_INSTALL_LIB_DIR "lib/ccme-dispenser" CACHE STRING
    "SDK library install directory (relative to install prefix)")

# ── backend component ─────────────────────────
install(TARGETS ccme-pump ccme-stirrer ccme-camera ccme-fmc ccme-web ccme-dispenser
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT backend
)

install(PROGRAMS ${CMAKE_SOURCE_DIR}/scripts/start_services.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME ccme-start
    COMPONENT backend
)

install(PROGRAMS ${CMAKE_SOURCE_DIR}/scripts/setup-ssl.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME ccme-setup-ssl
    COMPONENT backend
)

install(FILES ${CMAKE_SOURCE_DIR}/3rdparty/FMC_Controler/lib/libFMC4030_2009_1.so
    DESTINATION ${CCME_INSTALL_LIB_DIR}
    COMPONENT backend
)

install(FILES ${CMAKE_SOURCE_DIR}/dbus/org.ccme.conf
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/dbus-1/system.d
    COMPONENT backend
)

# ── frontend component ────────────────────────
install(DIRECTORY ${CMAKE_SOURCE_DIR}/frontend/
    DESTINATION ${CCME_INSTALL_FRONTEND_DIR}
    COMPONENT frontend
    FILES_MATCHING PATTERN "*.html" PATTERN "*.css" PATTERN "*.js"
)

install(FILES ${CMAKE_SOURCE_DIR}/nginx/nginx.conf
    DESTINATION ${CCME_INSTALL_NGINX_DIR}
    RENAME ccme.conf
    COMPONENT frontend
)
