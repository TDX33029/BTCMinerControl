#pragma once

#include "json.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "../platform/platform.h"

struct MiningNotify {
    std::string job_id;
    std::string prev_block_hash;
    std::string coinbase_1;
    std::string coinbase_2;
    std::vector<std::string> merkle_branches;
    uint32_t version = 0;
    uint32_t nbits = 0;
    uint32_t ntime = 0;
    bool clean_jobs = false;
};

struct ShareSubmit {
    std::string username;
    std::string job_id;
    std::string extranonce_2;
    uint32_t ntime = 0;
    uint32_t nonce = 0;
    uint32_t version_bits = 0;
    uint64_t board_id = 0; // local correlation only; never serialized
};

struct SubscribeResult {
    std::string extranonce;
    int extranonce_2_len = 0;
};

struct ConfigureResult {
    bool enabled = false;
    uint32_t version_mask = 0;
};

class StratumClient {
public:
    std::function<void(const MiningNotify&)> onNotify;
    std::function<void(double)> onSetDifficulty;
    std::function<void(const SubscribeResult&)> onSubscribeResult;
    std::function<void(const ConfigureResult&)> onConfigureResult;
    std::function<void(const SubscribeResult&)> onSetExtranonce;
    std::function<void(const std::string&)> onShowMessage;
    std::function<void(bool, const std::string&)> onAuthorizeResult;
    std::function<void(int, uint64_t, bool, const std::string&)>
        onShareResponse;

    StratumClient() = default;
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
    enum class RequestKind {
        Subscribe,
        Authorize,
        Configure,
        Submit,
        SuggestDifficulty,
    };

    struct PendingRequest {
        RequestKind kind;
        uint64_t board_id = 0;
    };

    std::atomic<SOCKET> m_socket{INVALID_SOCKET};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stop{false};
    int m_send_uid = 1;
    std::string m_buffer;
    std::string m_extranonce;
    int m_extranonce_2_len = 0;

    mutable std::mutex m_send_mutex;
    std::unordered_map<int, PendingRequest> m_pending;

    bool sendRequest(const std::string& method, const nlohmann::json& params,
                     RequestKind kind, uint64_t board_id = 0,
                     int* out_id = nullptr);
    bool appendFromSocket(int timeout_ms);
    bool popBufferedLine(std::string& line);
    void dispatchLine(const std::string& line);
    void handleMiningNotify(const nlohmann::json& params);
    void handleSetDifficulty(const nlohmann::json& params);
    void handleSetExtranonce(const nlohmann::json& params);
    void handleSetVersionMask(const nlohmann::json& params);
    void handleResult(const nlohmann::json& root);
    void handleShowMessage(const nlohmann::json& params);
};
