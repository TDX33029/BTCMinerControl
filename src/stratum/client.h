#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "../json.hpp"

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
    uint32_t    version;          // hex → uint32
    uint32_t    nbits;            // hex → uint32
    uint32_t    ntime;            // hex → uint32
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

// What the pool sends us after mining.subscribe
struct SubscribeResult {
    std::string extranonce;       // hex string
    int         extranonce_2_len; // bytes
};

// What the pool sends after mining.configure
struct ConfigureResult {
    uint32_t version_mask;
};

class StratumClient {
public:
    // Callbacks — set before calling run()
    std::function<void(const MiningNotify&)>       onNotify;
    std::function<void(double)>                    onSetDifficulty;
    std::function<void(const SubscribeResult&)>    onSubscribeResult;
    std::function<void(const ConfigureResult&)>    onConfigureResult;
    std::function<void(const std::string&)>        onSetExtranonce;
    std::function<void(const std::string&)>        onShowMessage;
    std::function<void(bool, const std::string&)>  onAuthorizeResult;
    std::function<void(int, bool, const std::string&)> onShareResponse; // msg_id, accepted, error

    StratumClient();
    ~StratumClient();

    // Connect to pool. Returns true on success.
    bool connect(const std::string& host, uint16_t port);

    // Send protocol init messages (call after connect succeeds)
    bool subscribe(const std::string& user_agent);
    bool authorize(const std::string& username, const std::string& password);
    bool configureVersionRolling();

    // Send a share (can be called from any thread)
    bool submitShare(const ShareSubmit& share, int& out_msg_id);

    // Suggest difficulty to the pool
    bool suggestDifficulty(uint32_t difficulty);

    // Read and print the next response line (synchronous, for init phase).
    // Returns the parsed JSON, or empty string on timeout/error.
    std::string readResponse(int timeout_ms = 5000);

    // Main receive loop — blocks until disconnected or stop() called
    void run();

    // Request graceful stop
    void stop();

    bool isConnected() const { return m_connected; }

private:
    SOCKET m_socket = INVALID_SOCKET;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stop{false};
    int m_send_uid = 1;

    std::string m_buffer;     // receive buffer for line assembly
    std::string m_extranonce; // from subscribe result
    int m_extranonce_2_len = 4;

    // Low-level send
    bool sendLine(const std::string& line);
    // Build and send a JSON-RPC request
    bool sendRequest(const std::string& method, const nlohmann::json& params);

    // Parse and dispatch a received line
    void dispatchLine(const std::string& line);

    // Parse various response/message types
    void handleMiningNotify(const nlohmann::json& params);
    void handleSetDifficulty(const nlohmann::json& params);
    void handleSetExtranonce(const nlohmann::json& params);
    void handleSetVersionMask(const nlohmann::json& params);
    void handleResult(const nlohmann::json& root);
    void handleShowMessage(const nlohmann::json& params);
};
