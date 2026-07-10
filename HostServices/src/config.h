#pragma once
#include <string>

// Application configuration loaded from config.json

struct AppConfig {
    // Pool settings
    std::string pool_host = "stratum.braiins.com";
    uint16_t    pool_port = 3333;          // TCP (no TLS)
    std::string pool_user = "username.worker";
    std::string pool_pass = "x";

    // Board server
    uint16_t    board_port = 4028;         // Port for STM32 boards to connect

    // Dashboard
    uint16_t    dashboard_port = 8080;     // Web UI port

    // Mining
    bool        version_rolling = true;
    uint32_t    default_frequency_mhz = 485;
    uint32_t    default_voltage_mv = 1200;
    double      min_difficulty = 256.0;
};

// Load config from JSON file. Returns defaults on failure.
AppConfig load_config(const std::string& path = "config.json");
