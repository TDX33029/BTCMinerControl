#include "client.h"
#include "../mine/sha256.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

#pragma comment(lib, "ws2_32.lib")

namespace {

struct WinSockInit {
    WinSockInit() {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~WinSockInit() { WSACleanup(); }
} g_winsock;

bool send_all(SOCKET socket, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        const int length = static_cast<int>(std::min<size_t>(
            data.size() - offset, static_cast<size_t>(INT_MAX)));
        const int sent = ::send(socket, data.data() + offset, length, 0);
        if (sent <= 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

bool is_hex(const std::string& value, size_t exact_length = 0) {
    if ((exact_length != 0 && value.size() != exact_length) ||
        (value.size() & 1U) != 0) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    });
}

uint32_t parse_hex_u32(const json& value, const char* field) {
    if (!value.is_string()) throw std::invalid_argument(std::string(field) + " is not a string");
    const std::string text = value.get<std::string>();
    if (text.empty() || text.size() > 8 || !std::all_of(text.begin(), text.end(),
        [](unsigned char c) { return std::isxdigit(c) != 0; })) {
        throw std::invalid_argument(std::string(field) + " is not uint32 hex");
    }
    size_t consumed = 0;
    const unsigned long parsed = std::stoul(text, &consumed, 16);
    if (consumed != text.size() || parsed > std::numeric_limits<uint32_t>::max()) {
        throw std::out_of_range(std::string(field) + " exceeds uint32");
    }
    return static_cast<uint32_t>(parsed);
}

std::string response_error(const json& root) {
    if (!root.contains("error") || root["error"].is_null()) return {};
    const auto& error = root["error"];
    if (error.is_array() && error.size() >= 2 && error[1].is_string()) {
        return error[1].get<std::string>();
    }
    if (error.is_object() && error.contains("message") &&
        error["message"].is_string()) {
        return error["message"].get<std::string>();
    }
    return error.dump();
}

std::string hex8(uint32_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(8) << value;
    return stream.str();
}

} // namespace

StratumClient::~StratumClient() {
    stop();
}

bool StratumClient::connect(const std::string& host, uint16_t port) {
    if (host.empty() || port == 0 || m_connected) return false;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* addresses = nullptr;
    const std::string port_text = std::to_string(port);
    const int resolved = getaddrinfo(host.c_str(), port_text.c_str(),
                                     &hints, &addresses);
    if (resolved != 0) {
        std::cerr << "[stratum] DNS resolution failed for " << host
                  << ": " << gai_strerrorA(resolved) << std::endl;
        return false;
    }

    SOCKET connected_socket = INVALID_SOCKET;
    for (addrinfo* address = addresses; address; address = address->ai_next) {
        SOCKET candidate = socket(address->ai_family, address->ai_socktype,
                                  address->ai_protocol);
        if (candidate == INVALID_SOCKET) continue;

        int nodelay = 1;
        setsockopt(candidate, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
        if (::connect(candidate, address->ai_addr,
                      static_cast<int>(address->ai_addrlen)) == 0) {
            connected_socket = candidate;
            break;
        }
        closesocket(candidate);
    }
    freeaddrinfo(addresses);

    if (connected_socket == INVALID_SOCKET) {
        std::cerr << "[stratum] connect() failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_send_mutex);
        m_socket = connected_socket;
        m_pending.clear();
        m_send_uid = 1;
    }
    m_buffer.clear();
    m_stop = false;
    m_connected = true;
    std::cout << "[stratum] Connected to " << host << ':' << port << std::endl;
    return true;
}

bool StratumClient::sendRequest(const std::string& method, const json& params,
                                RequestKind kind, uint64_t board_id,
                                int* out_id) {
    std::lock_guard<std::mutex> lock(m_send_mutex);
    const SOCKET socket = m_socket.load();
    if (socket == INVALID_SOCKET || !m_connected) return false;

    const int id = m_send_uid++;
    if (out_id) *out_id = id;
    const json request = {
        {"id", id}, {"method", method}, {"params", params}
    };
    const std::string wire = request.dump() + '\n';

    m_pending.emplace(id, PendingRequest{kind, board_id});
    if (send_all(socket, wire)) return true;

    m_pending.erase(id);
    m_connected = false;
    return false;
}

bool StratumClient::subscribe(const std::string& user_agent) {
    return !user_agent.empty() && sendRequest("mining.subscribe",
        json::array({user_agent}), RequestKind::Subscribe);
}

bool StratumClient::authorize(const std::string& username,
                              const std::string& password) {
    return !username.empty() && sendRequest("mining.authorize",
        json::array({username, password}), RequestKind::Authorize);
}

bool StratumClient::configureVersionRolling() {
    const json params = json::array({
        json::array({"version-rolling"}),
        json::object({{"version-rolling.mask", "ffffffff"}})
    });
    return sendRequest("mining.configure", params, RequestKind::Configure);
}

bool StratumClient::submitShare(const ShareSubmit& share, int& out_msg_id) {
    if (!is_hex(share.extranonce_2)) return false;
    const std::string ntime_hex = hex8(share.ntime);
    const std::string nonce_hex = hex8(share.nonce);
    const std::string version_bits_hex = hex8(share.version_bits);

    // TEMP DIAG: exact values the pool will use to rebuild the block header.
    std::cout << "[SUBMIT] job=" << share.job_id
              << " en2=" << share.extranonce_2
              << " ntime=" << ntime_hex
              << " nonce=" << nonce_hex
              << " version_bits=" << version_bits_hex
              << " board=0x" << std::hex << share.board_id << std::dec
              << std::endl;

    const json params = json::array({
        share.username, share.job_id, share.extranonce_2,
        ntime_hex, nonce_hex, version_bits_hex
    });
    return sendRequest("mining.submit", params, RequestKind::Submit,
                       share.board_id, &out_msg_id);
}

bool StratumClient::suggestDifficulty(uint32_t difficulty) {
    return difficulty != 0 && sendRequest("mining.suggest_difficulty",
        json::array({difficulty}), RequestKind::SuggestDifficulty);
}

bool StratumClient::popBufferedLine(std::string& line) {
    const size_t newline = m_buffer.find('\n');
    if (newline == std::string::npos) return false;
    line = m_buffer.substr(0, newline);
    m_buffer.erase(0, newline + 1);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

bool StratumClient::appendFromSocket(int timeout_ms) {
    const SOCKET socket = m_socket.load();
    if (socket == INVALID_SOCKET) return false;

    const int effective_timeout = timeout_ms <= 0 ? 1 : timeout_ms;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&effective_timeout),
               sizeof(effective_timeout));

    char chunk[8192];
    const int received = recv(socket, chunk, sizeof(chunk), 0);
    if (received > 0) {
        m_buffer.append(chunk, static_cast<size_t>(received));
        if (m_buffer.size() > 1024 * 1024) {
            std::cerr << "[stratum] Receive buffer exceeded 1 MiB" << std::endl;
            m_connected = false;
            return false;
        }
        return true;
    }
    if (received == 0) {
        m_connected = false;
        return false;
    }

    const int error = WSAGetLastError();
    if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) return false;
    if (!m_stop) std::cerr << "[stratum] recv() error: " << error << std::endl;
    m_connected = false;
    return false;
}

std::string StratumClient::readResponse(int timeout_ms) {
    std::string line;
    if (!popBufferedLine(line)) {
        if (!appendFromSocket(timeout_ms) || !popBufferedLine(line)) return {};
    }
    if (!line.empty()) {
        std::cout << "[stratum] RECV " << line << std::endl;
        dispatchLine(line);
    }
    return line;
}

void StratumClient::stop() {
    m_stop = true;
    m_connected = false;

    std::lock_guard<std::mutex> lock(m_send_mutex);
    const SOCKET socket = m_socket.exchange(INVALID_SOCKET);
    if (socket != INVALID_SOCKET) {
        shutdown(socket, SD_BOTH);
        closesocket(socket);
    }
    m_pending.clear();
}

void StratumClient::run() {
    m_stop = false;

    while (!m_stop && m_connected) {
        std::string line;
        while (popBufferedLine(line)) {
            if (!line.empty()) dispatchLine(line);
            if (m_stop || !m_connected) return;
        }
        appendFromSocket(5000);
    }
}

void StratumClient::dispatchLine(const std::string& line) {
    try {
        const json root = json::parse(line);
        if (!root.is_object()) throw std::invalid_argument("root is not an object");

        if (root.contains("method")) {
            if (!root["method"].is_string() || !root.contains("params") ||
                !root["params"].is_array()) {
                throw std::invalid_argument("invalid notification envelope");
            }
            const std::string method = root["method"].get<std::string>();
            const auto& params = root["params"];
            if (method == "mining.notify") handleMiningNotify(params);
            else if (method == "mining.set_difficulty") handleSetDifficulty(params);
            else if (method == "mining.set_extranonce") handleSetExtranonce(params);
            else if (method == "mining.set_version_mask") handleSetVersionMask(params);
            else if (method == "client.show_message") handleShowMessage(params);
            else std::cout << "[stratum] Unknown method: " << method << std::endl;
            return;
        }

        if (root.contains("id") &&
            (root.contains("result") || root.contains("error"))) {
            handleResult(root);
            return;
        }
        throw std::invalid_argument("unrecognized JSON-RPC message");
    } catch (const std::exception& error) {
        std::cerr << "[stratum] Invalid message: " << error.what() << std::endl;
    }
}

void StratumClient::handleMiningNotify(const json& params) {
    if (params.size() < 9 || !params[0].is_string() ||
        !params[1].is_string() || !params[2].is_string() ||
        !params[3].is_string() || !params[4].is_array() ||
        !params[8].is_boolean()) {
        throw std::invalid_argument("malformed mining.notify");
    }

    MiningNotify notify{};
    notify.job_id = params[0].get<std::string>();
    notify.prev_block_hash = params[1].get<std::string>();
    notify.coinbase_1 = params[2].get<std::string>();
    notify.coinbase_2 = params[3].get<std::string>();
    if (notify.job_id.empty() || !is_hex(notify.prev_block_hash, 64) ||
        !is_hex(notify.coinbase_1) || !is_hex(notify.coinbase_2)) {
        throw std::invalid_argument("invalid notify hex fields");
    }
    for (const auto& branch : params[4]) {
        if (!branch.is_string()) throw std::invalid_argument("merkle branch is not a string");
        const std::string branch_hex = branch.get<std::string>();
        if (!is_hex(branch_hex, 64)) throw std::invalid_argument("invalid merkle branch");
        notify.merkle_branches.push_back(branch_hex);
    }
    notify.version = parse_hex_u32(params[5], "version");
    notify.nbits = parse_hex_u32(params[6], "nbits");
    notify.ntime = parse_hex_u32(params[7], "ntime");
    notify.clean_jobs = params[8].get<bool>();
    if (onNotify) onNotify(notify);
}

void StratumClient::handleSetDifficulty(const json& params) {
    if (params.empty() || !params[0].is_number()) {
        throw std::invalid_argument("invalid mining.set_difficulty");
    }
    const double difficulty = params[0].get<double>();
    if (!std::isfinite(difficulty) || difficulty <= 0.0) {
        throw std::invalid_argument("difficulty must be finite and positive");
    }
    if (onSetDifficulty) onSetDifficulty(difficulty);
}

void StratumClient::handleSetExtranonce(const json& params) {
    if (params.size() < 2 || !params[0].is_string() ||
        !params[1].is_number_integer()) {
        throw std::invalid_argument("invalid mining.set_extranonce");
    }
    SubscribeResult value{params[0].get<std::string>(), params[1].get<int>()};
    if (!is_hex(value.extranonce) || value.extranonce_2_len <= 0 ||
        value.extranonce_2_len > 32) {
        throw std::invalid_argument("invalid extranonce values");
    }
    m_extranonce = value.extranonce;
    m_extranonce_2_len = value.extranonce_2_len;
    if (onSetExtranonce) onSetExtranonce(value);
}

void StratumClient::handleSetVersionMask(const json& params) {
    if (params.empty()) throw std::invalid_argument("empty version mask");
    ConfigureResult result{true, parse_hex_u32(params[0], "version mask")};
    if (onConfigureResult) onConfigureResult(result);
}

void StratumClient::handleShowMessage(const json& params) {
    if (params.empty() || !params[0].is_string()) {
        throw std::invalid_argument("invalid client.show_message");
    }
    const std::string message = params[0].get<std::string>();
    std::cout << "[stratum] Pool message: " << message << std::endl;
    if (onShowMessage) onShowMessage(message);
}

void StratumClient::handleResult(const json& root) {
    if (!root["id"].is_number_integer()) {
        throw std::invalid_argument("response id is not an integer");
    }
    const int id = root["id"].get<int>();

    PendingRequest pending{};
    {
        std::lock_guard<std::mutex> lock(m_send_mutex);
        const auto found = m_pending.find(id);
        if (found == m_pending.end()) {
            std::cout << "[stratum] Response for unknown request " << id << std::endl;
            return;
        }
        pending = found->second;
        m_pending.erase(found);
    }

    const std::string error = response_error(root);
    const json result = root.contains("result") ? root["result"] : json(nullptr);

    switch (pending.kind) {
    case RequestKind::Subscribe: {
        if (!error.empty() || !result.is_array() || result.size() < 3 ||
            !result[1].is_string() || !result[2].is_number_integer()) {
            throw std::invalid_argument("subscribe failed: " + error);
        }
        SubscribeResult value{result[1].get<std::string>(), result[2].get<int>()};
        if (!is_hex(value.extranonce) || value.extranonce_2_len <= 0 ||
            value.extranonce_2_len > 32) {
            throw std::invalid_argument("subscribe returned invalid extranonce");
        }
        m_extranonce = value.extranonce;
        m_extranonce_2_len = value.extranonce_2_len;
        if (onSubscribeResult) onSubscribeResult(value);
        break;
    }
    case RequestKind::Configure: {
        ConfigureResult value{};
        if (error.empty() && result.is_object()) {
            value.enabled = result.value("version-rolling", false);
            if (value.enabled && result.contains("version-rolling.mask")) {
                value.version_mask = parse_hex_u32(
                    result["version-rolling.mask"], "version mask");
            }
        }
        if (onConfigureResult) onConfigureResult(value);
        break;
    }
    case RequestKind::Authorize: {
        const bool accepted = error.empty() && result.is_boolean() &&
                              result.get<bool>();
        if (onAuthorizeResult) onAuthorizeResult(accepted, error);
        break;
    }
    case RequestKind::Submit: {
        bool accepted = false;
        if (error.empty()) {
            if (result.is_boolean()) accepted = result.get<bool>();
            else if (result.is_object() && result.contains("status") &&
                     result["status"].is_string()) {
                accepted = result["status"].get<std::string>() == "ok";
            }
        }
        if (onShareResponse) {
            onShareResponse(id, pending.board_id, accepted, error);
        }
        break;
    }
    case RequestKind::SuggestDifficulty:
        break;
    }
}
