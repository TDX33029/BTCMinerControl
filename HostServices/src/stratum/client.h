#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "json.hpp"

// ---------------------------------------------------------------------------
// Stratum V1 TCP client — connects to a mining pool, handles the protocol.
//
// Usage:
//   StratumClient client;
//   client.onNotify = [](const MiningNotify& n) { ... };
//   client.onSetDifficulty = [](double d) { ... };
//   client.connect("pool.example.com", 3333);
//   client.subscribe("bitaxe/ultra/1.0");
//   client.authorize("username.worker", "password");
//   client.run();  // blocks, runs receive loop
//
// From another thread:
//   client.submitShare(share);
// ---------------------------------------------------------------------------

struct MiningNotify {
    std::string job_id;
    std::string prev_block_hash;  // hex
    std::string coinbase_1;       // hex
    std::string coinbase_2;       // hex
    std::vector<std::string> merkle_branches; // hex strings
    uint32_t    version;          // hex -> uint32
    uint32_t    nbits;            // hex -> uint32
    uint32_t    ntime;            // hex -> uint32
    bool        clean_jobs;
};

struct ShareSubmit {
    std::string username;
    std::string job_id;
    std::string extranonce_2;
    uint32_t    ntime;
    uint32_t    nonce;
    uint32_t    version_bits;
};

struct ShareResponse {
    bool   accepted;
    int    message_id;
    std::string error_message;
};

struct SubscribeResult {
    std::string extranonce;       // hex string
    int         extranonce_2_len; // bytes
};

struct ConfigureResult {
    uint32_t version_mask;
};

class StratumClient {
public:
    std::function<void(const MiningNotify&)>       onNotify;
    std::function<void(double)>                    onSetDifficulty;
    std::function<void(const SubscribeResult&)>    onSubscribeResult;
    std::function<void(const ConfigureResult&)>    onConfigureResult;
    std::function<void(const std::string&)>        onSetExtranonce;
    std::function<void(const std::string&)>        onShowMessage;
    std::function<void(bool, const std::string&)>  onAuthorizeResult;
    std::function<void(int, bool, const std::string&)> onShareResponse;

    StratumClient();
    ~StratumClient();

    bool connect(const std::string& host, uint16_t port);
    bool subscribe(const std::string& user_agent);
    bool authorize(const std::string& username, const std::string& password);
    bool configureVersionRolling();
    bool submitShare(const ShareSubmit& share, int& out_msg_id);
    bool suggestDifficulty(uint32_t difficulty);
    std::string readResponse(int timeout_ms = 5000);
    void run();
    void stop();
    bool isConnected() const { return m_connected; }

private:
    SOCKET m_socket = INVALID_SOCKET;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stop{false};
    int m_send_uid = 1;
    std::string m_buffer;
    std::string m_extranonce;
    int m_extranonce_2_len = 4;

    bool sendLine(const std::string& line);
    bool sendRequest(const std::string& method, const nlohmann::json& params);
    void dispatchLine(const std::string& line);
    void handleMiningNotify(const nlohmann::json& params);
    void handleSetDifficulty(const nlohmann::json& params);
    void handleSetExtranonce(const nlohmann::json& params);
    void handleSetVersionMask(const nlohmann::json& params);
    void handleResult(const nlohmann::json& root);
    void handleShowMessage(const nlohmann::json& params);
};
