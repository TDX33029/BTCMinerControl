// Stratum V1 TCP client implementation using WinSock2.
// Connects to a mining pool, handles JSON-RPC protocol.

#include "client.h"
#include "../json.hpp"
#include <iostream>
#include <sstream>

using json = nlohmann::json;

#pragma comment(lib, "ws2_32.lib")

// ---------------------------------------------------------------------------
// WinSock initializer (RAII)
// ---------------------------------------------------------------------------
static struct WinSockInit {
    WinSockInit() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    ~WinSockInit() {
        WSACleanup();
    }
} g_winsock;

// ---------------------------------------------------------------------------
// StratumClient
// ---------------------------------------------------------------------------

StratumClient::StratumClient() {}
StratumClient::~StratumClient() {
    stop();
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
    }
}

bool StratumClient::connect(const std::string& host, uint16_t port) {
    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET) {
        std::cerr << "[stratum] socket() failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    // Set TCP_NODELAY for low-latency mining
    int nodelay = 1;
    setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

    // Set receive timeout (5 seconds)
    int timeout = 5000;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    // Resolve hostname
    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string port_str = std::to_string(port);
    int ret = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
    if (ret != 0) {
        std::cerr << "[stratum] DNS resolution failed for " << host << ": " << gai_strerror(ret) << std::endl;
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    ret = ::connect(m_socket, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);

    if (ret == SOCKET_ERROR) {
        std::cerr << "[stratum] connect() failed: " << WSAGetLastError() << std::endl;
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    std::cout << "[stratum] Connected to " << host << ":" << port << std::endl;
    m_connected = true;
    return true;
}

bool StratumClient::sendLine(const std::string& line) {
    if (m_socket == INVALID_SOCKET) return false;
    std::string data = line + "\n";
    int sent = ::send(m_socket, data.c_str(), (int)data.length(), 0);
    return sent == (int)data.length();
}

bool StratumClient::sendRequest(const std::string& method, const json& params) {
    int id = m_send_uid++;
    json req = {
        {"id", id},
        {"method", method},
        {"params", params}
    };
    return sendLine(req.dump());
}

bool StratumClient::subscribe(const std::string& user_agent) {
    return sendRequest("mining.subscribe", json::array({user_agent}));
}

bool StratumClient::authorize(const std::string& username, const std::string& password) {
    return sendRequest("mining.authorize", json::array({username, password}));
}

bool StratumClient::configureVersionRolling() {
    json params = json::array({{"version-rolling"}, {"version-rolling.mask", "1fffe000"}});
    return sendRequest("mining.configure", params);
}

bool StratumClient::submitShare(const ShareSubmit& share, int& out_msg_id) {
    out_msg_id = m_send_uid;
    std::ostringstream ntime_hex;
    ntime_hex << std::hex << share.ntime;
    std::ostringstream nonce_hex;
    nonce_hex << std::hex << share.nonce;

    // Version bits: hex string, zero-padded to 8 chars
    char vb_hex[16];
    snprintf(vb_hex, sizeof(vb_hex), "%08x", share.version_bits);

    json params = json::array({
        share.username,
        share.job_id,
        share.extranonce_2,
        ntime_hex.str(),
        nonce_hex.str(),
        std::string(vb_hex)
    });
    return sendRequest("mining.submit", params);
}

bool StratumClient::suggestDifficulty(uint32_t difficulty) {
    return sendRequest("mining.suggest_difficulty", json::array({difficulty}));
}

std::string StratumClient::readResponse(int timeout_ms) {
    if (m_socket == INVALID_SOCKET) return "";

    // Set timeout for this read
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

    char buf[8192];
    int received = recv(m_socket, buf, sizeof(buf) - 1, 0);

    // Restore long timeout for the main loop
    int long_timeout = 5000;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&long_timeout, sizeof(long_timeout));

    if (received <= 0) {
        if (received == 0) {
            m_connected = false;
        }
        return "";
    }

    buf[received] = '\0';
    m_buffer += buf;

    // Extract the first complete line (JSON-RPC: newline-delimited)
    size_t pos = m_buffer.find('\n');
    if (pos == std::string::npos) {
        // No complete line yet, return what we have
        std::string partial = m_buffer;
        return partial;
    }

    std::string line = m_buffer.substr(0, pos);
    m_buffer.erase(0, pos + 1);

    if (!line.empty() && line.back() == '\r') line.pop_back();

    // Log the raw line for debugging
    std::cout << "[stratum] RECV " << line << std::endl;

    // Try to dispatch it
    dispatchLine(line);

    return line;
}

void StratumClient::stop() {
    m_stop = true;
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    m_connected = false;
}

void StratumClient::run() {
    m_stop = false;
    char buf[8192];
    m_buffer.clear();

    while (!m_stop && m_connected) {
        int received = recv(m_socket, buf, sizeof(buf) - 1, 0);
        if (received <= 0) {
            if (received == 0) {
                std::cerr << "[stratum] Connection closed by pool." << std::endl;
            } else {
                int err = WSAGetLastError();
                if (err == WSAETIMEDOUT) continue; // timeout, retry
                std::cerr << "[stratum] recv() error: " << err << std::endl;
            }
            m_connected = false;
            break;
        }

        buf[received] = '\0';
        m_buffer += buf;

        // Process complete lines (JSON-RPC uses newline framing)
        while (true) {
            size_t pos = m_buffer.find('\n');
            if (pos == std::string::npos) break;

            std::string line = m_buffer.substr(0, pos);
            m_buffer.erase(0, pos + 1);

            // Trim trailing \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (!line.empty()) {
                dispatchLine(line);
            }
        }
    }
}

void StratumClient::dispatchLine(const std::string& line) {
    try {
        json root = json::parse(line);

        // Check for "method" → it's a request/notification from the pool
        if (root.contains("method")) {
            std::string method = root["method"];
            auto& params = root["params"];

            if (method == "mining.notify") {
                handleMiningNotify(params);
            } else if (method == "mining.set_difficulty") {
                handleSetDifficulty(params);
            } else if (method == "mining.set_extranonce") {
                handleSetExtranonce(params);
            } else if (method == "mining.set_version_mask") {
                handleSetVersionMask(params);
            } else if (method == "client.show_message") {
                handleShowMessage(params);
            } else {
                std::cout << "[stratum] Unknown method: " << method << std::endl;
            }
            return;
        }

        // Check for "result" — it's a response to one of our requests
        if (root.contains("id") && root.contains("result")) {
            handleResult(root);
            return;
        }

        std::cout << "[stratum] Unrecognized message: " << line.substr(0, 100) << std::endl;
    } catch (const json::parse_error& e) {
        std::cerr << "[stratum] JSON parse error: " << e.what() << std::endl;
    }
}

// ---------------------------------------------------------------------------
// Notification handlers
// ---------------------------------------------------------------------------

void StratumClient::handleMiningNotify(const json& params) {
    MiningNotify n;
    n.job_id          = params[0].get<std::string>();
    n.prev_block_hash = params[1].get<std::string>();
    n.coinbase_1      = params[2].get<std::string>();
    n.coinbase_2      = params[3].get<std::string>();

    // merkle_branches: params[4] is an array of hex strings
    for (const auto& branch : params[4]) {
        n.merkle_branches.push_back(branch.get<std::string>());
    }

    // Parse hex values to uint32
    auto hex2u32 = [](const std::string& s) -> uint32_t {
        return uint32_t(std::stoul(s, nullptr, 16));
    };

    n.version   = hex2u32(params[5].get<std::string>());
    n.nbits     = hex2u32(params[6].get<std::string>());
    n.ntime     = hex2u32(params[7].get<std::string>());

    // clean_jobs is the last parameter
    n.clean_jobs = params[8].get<bool>();

    if (onNotify) onNotify(n);
}

void StratumClient::handleSetDifficulty(const json& params) {
    double diff = params[0].get<double>();
    if (onSetDifficulty) onSetDifficulty(diff);
}

void StratumClient::handleSetExtranonce(const json& params) {
    m_extranonce = params[0].get<std::string>();
    m_extranonce_2_len = params[1].get<int>();
    std::cout << "[stratum] Extranonce: " << m_extranonce << " (len2=" << m_extranonce_2_len << ")" << std::endl;
    if (onSetExtranonce) onSetExtranonce(m_extranonce);
}

void StratumClient::handleSetVersionMask(const json& params) {
    if (onConfigureResult) {
        ConfigureResult r;
        r.version_mask = uint32_t(std::stoul(params[0].get<std::string>(), nullptr, 16));
        onConfigureResult(r);
    }
}

void StratumClient::handleShowMessage(const json& params) {
    std::string msg = params[0].get<std::string>();
    std::cout << "[stratum] Pool message: " << msg << std::endl;
    if (onShowMessage) onShowMessage(msg);
}

// ---------------------------------------------------------------------------
// Response handler
// ---------------------------------------------------------------------------

void StratumClient::handleResult(const json& root) {
    int id = root["id"].get<int>();
    auto& result = root["result"];

    // Try to determine response type by structure

    // Subscribe response: [ ["mining.notify", ...], extranonce_1, extranonce_2_size ]
    if (result.is_array() && result.size() >= 3 && result[1].is_string()) {
        SubscribeResult sr;
        sr.extranonce = result[1].get<std::string>();
        sr.extranonce_2_len = result[2].get<int>();
        m_extranonce = sr.extranonce;
        m_extranonce_2_len = sr.extranonce_2_len;
        std::cout << "[stratum] Subscribed. Extranonce: " << sr.extranonce
                  << " (extranonce_2_len=" << sr.extranonce_2_len << ")" << std::endl;
        if (onSubscribeResult) onSubscribeResult(sr);
        return;
    }

    // Configure response: { "version-rolling": true, "version-rolling.mask": "1fffe000" }
    if (result.is_object() && result.contains("version-rolling.mask")) {
        ConfigureResult cr;
        cr.version_mask = uint32_t(std::stoul(result["version-rolling.mask"].get<std::string>(), nullptr, 16));
        std::cout << "[stratum] Version-rolling enabled. Mask: 0x" << std::hex << cr.version_mask << std::dec << std::endl;
        if (onConfigureResult) onConfigureResult(cr);
        return;
    }

    // Authorize response: true/false
    if (result.is_boolean()) {
        bool ok = result.get<bool>();
        std::cout << "[stratum] Authorize: " << (ok ? "OK" : "FAILED") << std::endl;
        if (onAuthorizeResult) onAuthorizeResult(ok, "");
        return;
    }

    // Share submit response: { "status": "ok" } or null or true
    bool accepted = false;
    std::string error_msg;
    if (result.is_null()) {
        accepted = true; // null = rejected? Actually pools vary. Let's check.
    } else if (result.is_boolean()) {
        accepted = result.get<bool>();
    } else if (result.is_object()) {
        if (result.contains("status") && result["status"] == "ok") {
            accepted = true;
        } else if (result.contains("error")) {
            accepted = false;
            error_msg = result["error"].is_string() ? result["error"].get<std::string>()
                                                     : result["error"].dump();
        }
    }

    // Check root-level error
    if (root.contains("error") && !root["error"].is_null()) {
        accepted = false;
        auto& err = root["error"];
        if (err.is_array() && err.size() >= 2) {
            error_msg = err[1].get<std::string>();
        } else if (err.is_object() && err.contains("message")) {
            error_msg = err["message"].get<std::string>();
        }
    }

    if (onShareResponse) onShareResponse(id, accepted, error_msg);
}
