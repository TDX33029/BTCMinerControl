#pragma once
#include <string>

// Application configuration loaded from config.json

struct AppConfig {
    // Pool settings
    std::string pool_host = "stratum.braiins.com";
    uint16_t    pool_port = 3333;          // TCP (no TLS)
    std::string pool_user = "username.worker";
    std::string pool_pass = "x";
    std::string pool_management_url = "https://pool.braiins.com/";

    // Board server
    uint16_t    board_port = 4200;         // Port for STM32 boards to connect
    uint32_t    board_detection_interval_ms = 5000; // Periodic latency probe

    // Dashboard
    uint16_t    dashboard_port = 8080;     // Web UI port
    std::string dashboard_bind = "127.0.0.1"; // opt in to LAN exposure explicitly

    // Mining
    bool        version_rolling = true;
    uint32_t    default_frequency_mhz = 485;
    uint32_t    default_voltage_mv = 1200;
    double      min_difficulty = 256.0;
};

// Load and validate config JSON. Returns safe defaults on failure.
AppConfig load_config(const std::string& path = "config.json");

// Persist listener ports and the board detection/latency polling interval.
// Port changes are applied after restart; the detection interval can also be
// applied immediately to a running BoardManager.
bool save_runtime_settings(const std::string& path,
                           uint16_t board_port,
                           uint16_t dashboard_port,
                           uint32_t board_detection_interval_ms);
