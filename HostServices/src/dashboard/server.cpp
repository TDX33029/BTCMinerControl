#include "server.h"
#include "../json.hpp"
#include <iostream>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <ws2tcpip.h>

using json = nlohmann::json;

// Embedded HTML dashboard page
static const char* DASHBOARD_HTML = R"html(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>BTCMinerControl</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:#0d1117;color:#c9d1d9;padding:20px}
h1{color:#58a6ff;margin-bottom:10px}
.header{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px;flex-wrap:wrap}
.status{padding:4px 12px;border-radius:12px;font-size:14px}
.status.online{background:#238636;color:#fff}
.status.offline{background:#da3633;color:#fff}
.status.waiting{background:#9e6a03;color:#fff}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:16px;margin-top:16px}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:16px}
.card h3{margin-bottom:8px;color:#58a6ff}
.card .hashrate{font-size:28px;font-weight:bold;color:#7ee787}
.card .stat{margin-top:4px;font-size:14px;color:#8b949e}
.card.online{border-color:#238636}
.card.offline{border-color:#da3633;opacity:0.6}
.metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin-bottom:16px}
.metric{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px;text-align:center}
.metric .value{font-size:24px;font-weight:bold;color:#7ee787}
.metric .label{font-size:12px;color:#8b949e;margin-top:4px}
</style>
</head>
<body>
<h1>BTCMinerControl</h1>
<div class="header">
  <div>
    <span id="connectionLabel">Pool</span>: <span id="poolUrl">-</span>
    <span id="poolStatus" class="status offline">disconnected</span>
  </div>
  <div id="uptime" style="color:#8b949e"></div>
</div>
<div class="metrics">
  <div class="metric"><div class="value" id="hashrateTotal">0</div><div class="label">Total GH/s</div></div>
  <div class="metric"><div class="value" id="sharesAccepted">0</div><div class="label">Accepted</div></div>
  <div class="metric"><div class="value" id="sharesRejected">0</div><div class="label">Rejected</div></div>
  <div class="metric"><div class="value" id="boardCount">0</div><div class="label">Boards Online</div></div>
</div>
<div class="grid" id="boardGrid"></div>
<script>
const API_URL = '/api/stats';
let startTime = Date.now();
async function refresh() {
    try {
        const r = await fetch(API_URL);
        const data = await r.json();
        updateUI(data);
    } catch(e) { console.error(e); }
}
function formatHashrate(ghs) {
    if(ghs >= 1000) return (ghs/1000).toFixed(2) + ' TH/s';
    return ghs.toFixed(1) + ' GH/s';
}
function updateUI(data) {
    const testMode = data.test_mode || '';
    document.getElementById('connectionLabel').textContent = testMode ? 'Mode' : 'Pool';
    document.getElementById('poolUrl').textContent = testMode || data.pool_url;
    const ps = document.getElementById('poolStatus');
    ps.textContent = testMode ? 'active' : (data.pool_connected ? 'online' : 'disconnected');
    ps.className = 'status ' + ((testMode || data.pool_connected) ? 'online' : 'offline');
    document.getElementById('hashrateTotal').textContent = formatHashrate(data.hashrate_total);
    document.getElementById('sharesAccepted').textContent = data.shares_accepted;
    document.getElementById('sharesRejected').textContent = data.shares_rejected;
    document.getElementById('boardCount').textContent = data.boards.filter(b=>b.online).length;

    let elapsed = Math.floor((Date.now() - startTime)/1000);
    let h=Math.floor(elapsed/3600), m=Math.floor((elapsed%3600)/60), s=elapsed%60;
    document.getElementById('uptime').textContent = `uptime: ${h}h ${m}m ${s}s`;

    const grid = document.getElementById('boardGrid');
    grid.innerHTML = data.boards.map(b => {
        let testStatus = '';
        if(testMode === 'CHIP TEST') {
            const state = b.nonces_returned === 0 ? 'WAITING' :
                          (b.best_diff >= 256 ? 'PASS' : 'FAIL');
            const color = state === 'PASS' ? '#7ee787' :
                          (state === 'FAIL' ? '#da3633' : '#d29922');
            testStatus = `<div class="stat">Test: <span style="color:${color};font-weight:bold">${state}</span></div>`;
        }
        return `
        <div class="card ${b.online?'online':'offline'}">
            <h3>Board ${b.board_id.toString(16)}</h3>
            <div class="hashrate">${formatHashrate(b.hashrate_1m)}</div>
            <div class="stat">IP: ${b.ip||b.ip_addr||'-'}</div>
            <div class="stat">ASICs: ${b.asic_count} | Jobs: ${b.jobs_sent}</div>
            <div class="stat">Nonces: ${b.nonces_returned} | Best Diff: ${Number(b.best_diff).toFixed(3)}</div>
            <div class="stat">Accepted: ${b.shares_accepted} | Rejected: ${b.shares_rejected}</div>
            ${testStatus}
            <div class="stat">Status: ${b.online?'<span style=color:#7ee787>Online</span>':'<span style=color:#da3633>Offline</span>'}</div>
        </div>
    `}).join('');
}
refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>)html";

namespace {

bool send_all(SOCKET socket, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        int sent = ::send(socket, data.data() + offset,
                          static_cast<int>(data.size() - offset), 0);
        if (sent <= 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

std::string http_response(int status, const char* reason,
                          const char* content_type, const std::string& body) {
    return "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n"
           "Content-Type: " + content_type + "\r\n"
           "X-Content-Type-Options: nosniff\r\n"
           "Cache-Control: no-store\r\n"
           "Connection: close\r\n"
           "Content-Length: " + std::to_string(body.size()) +
           "\r\n\r\n" + body;
}

} // namespace

DashboardServer::DashboardServer() {}
DashboardServer::~DashboardServer() { stop(); }

bool DashboardServer::start(uint16_t port, BoardManager* board_mgr,
                            const std::string& bind_address) {
    if (m_running || board_mgr == nullptr || port == 0) return false;
    m_boards = board_mgr;

    m_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listen_sock == INVALID_SOCKET) return false;

    int reuse = 1;
    setsockopt(m_listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "[dashboard] Invalid bind address: " << bind_address << std::endl;
        closesocket(m_listen_sock);
        m_listen_sock = INVALID_SOCKET;
        return false;
    }
    addr.sin_port = htons(port);

    if (bind(m_listen_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(m_listen_sock);
        m_listen_sock = INVALID_SOCKET;
        return false;
    }

    if (listen(m_listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(m_listen_sock);
        m_listen_sock = INVALID_SOCKET;
        return false;
    }

    m_running = true;
    m_stop = false;
    m_thread = std::thread(&DashboardServer::acceptLoop, this);

    std::cout << "[dashboard] Web UI: http://" << bind_address << ':' << port << std::endl;
    return true;
}

void DashboardServer::stop() {
    if (m_stop.exchange(true) && !m_running) return;
    m_running = false;
    if (m_listen_sock != INVALID_SOCKET) {
        closesocket(m_listen_sock);
        m_listen_sock = INVALID_SOCKET;
    }
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        for (SOCKET client : m_client_sockets) shutdown(client, SD_BOTH);
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void DashboardServer::acceptLoop() {
    while (!m_stop) {
        SOCKET client = accept(m_listen_sock, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (m_stop) break;
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m_clients_mutex);
            m_client_sockets.push_back(client);
        }
        // Requests are small and connection-close; handling them in the
        // accept thread avoids an unbounded thread list under dashboard polling.
        handleClient(client);
    }
}

void DashboardServer::handleClient(SOCKET client) {
    char buf[2048];
    int timeout = 5000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    std::string request;
    while (request.find("\r\n\r\n") == std::string::npos &&
           request.size() < 8192) {
        int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        request.append(buf, static_cast<size_t>(n));
    }

    std::string response;
    std::istringstream request_line(request.substr(0, request.find("\r\n")));
    std::string method, target, version;
    request_line >> method >> target >> version;
    const size_t query = target.find('?');
    if (query != std::string::npos) target.erase(query);

    if (method != "GET" || version.rfind("HTTP/", 0) != 0) {
        response = http_response(405, "Method Not Allowed", "text/plain; charset=utf-8",
                                 "Method Not Allowed\n");
    } else if (target == "/api/stats") {
        // Build JSON stats
        json j;
        {
            std::lock_guard<std::mutex> lock(m_pool_mutex);
            j["test_mode"] = m_test_mode;
            j["pool_url"] = m_pool_url;
            j["pool_connected"] = m_pool_connected;
            j["shares_accepted"] = m_shares_accepted;
            j["shares_rejected"] = m_shares_rejected;
            j["hashrate_total"] = m_hashrate_total;
        }

        auto boards = m_boards ? m_boards->getStats() : std::vector<BoardStats>{};
        json jboards = json::array();
        for (auto& b : boards) {
            json jb;
            jb["board_id"] = b.info.board_id;
            jb["online"] = b.online;
            jb["asic_count"] = b.info.asic_count;
            jb["shares_accepted"] = b.info.shares_accepted;
            jb["shares_rejected"] = b.info.shares_rejected;
            jb["hashrate_1m"] = b.hashrate_1m;
            jb["hashrate_10m"] = b.hashrate_10m;
            jb["jobs_sent"] = b.jobs_sent;
            jb["nonces_returned"] = b.nonces_returned;
            jb["best_diff"] = b.best_diff;
            jb["ip_addr"] = b.info.ip_addr;
            jboards.push_back(jb);
        }
        j["boards"] = jboards;

        std::string body = j.dump();
        response = http_response(200, "OK", "application/json; charset=utf-8", body);
    } else if (target == "/") {
        // Serve dashboard HTML
        std::string body = DASHBOARD_HTML;
        response = http_response(200, "OK", "text/html; charset=utf-8", body);
    } else {
        response = http_response(404, "Not Found", "text/plain; charset=utf-8",
                                 "Not Found\n");
    }

    send_all(client, response);
    closesocket(client);
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    m_client_sockets.erase(std::remove(m_client_sockets.begin(),
                                      m_client_sockets.end(), client),
                           m_client_sockets.end());
}

void DashboardServer::setPoolStats(const std::string& pool_url, bool connected,
                                    uint64_t accepted, uint64_t rejected,
                                    double hashrate_total) {
    std::lock_guard<std::mutex> lock(m_pool_mutex);
    m_pool_url = pool_url;
    m_pool_connected = connected;
    m_shares_accepted = accepted;
    m_shares_rejected = rejected;
    m_hashrate_total = hashrate_total;
}

void DashboardServer::setTestMode(const std::string& mode) {
    std::lock_guard<std::mutex> lock(m_pool_mutex);
    m_test_mode = mode;
}
