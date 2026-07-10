#include "server.h"
#include "../json.hpp"
#include <iostream>
#include <sstream>
#include <ctime>

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
    Pool: <span id="poolUrl">-</span>
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
    document.getElementById('poolUrl').textContent = data.pool_url;
    const ps = document.getElementById('poolStatus');
    ps.textContent = data.pool_connected ? 'online' : 'disconnected';
    ps.className = 'status ' + (data.pool_connected ? 'online' : 'offline');
    document.getElementById('hashrateTotal').textContent = formatHashrate(data.hashrate_total);
    document.getElementById('sharesAccepted').textContent = data.shares_accepted;
    document.getElementById('sharesRejected').textContent = data.shares_rejected;
    document.getElementById('boardCount').textContent = data.boards.filter(b=>b.online).length;

    let elapsed = Math.floor((Date.now() - startTime)/1000);
    let h=Math.floor(elapsed/3600), m=Math.floor((elapsed%3600)/60), s=elapsed%60;
    document.getElementById('uptime').textContent = `uptime: ${h}h ${m}m ${s}s`;

    const grid = document.getElementById('boardGrid');
    grid.innerHTML = data.boards.map(b => `
        <div class="card ${b.online?'online':'offline'}">
            <h3>Board ${b.board_id.toString(16)}</h3>
            <div class="hashrate">${formatHashrate(b.hashrate_1m)}</div>
            <div class="stat">IP: ${b.ip||b.ip_addr||'-'}</div>
            <div class="stat">ASICs: ${b.asic_count} | Jobs: ${b.jobs_sent}</div>
            <div class="stat">Accepted: ${b.shares_accepted} | Rejected: ${b.shares_rejected}</div>
            <div class="stat">Status: ${b.online?'<span style=color:#7ee787>Online</span>':'<span style=color:#da3633>Offline</span>'}</div>
        </div>
    `).join('');
}
refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>)html";

DashboardServer::DashboardServer() {}

bool DashboardServer::start(uint16_t port, BoardManager* board_mgr) {
    m_boards = board_mgr;

    m_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listen_sock == INVALID_SOCKET) return false;

    int reuse = 1;
    setsockopt(m_listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(m_listen_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(m_listen_sock);
        return false;
    }

    if (listen(m_listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(m_listen_sock);
        return false;
    }

    m_running = true;
    m_stop = false;
    m_thread = std::thread(&DashboardServer::acceptLoop, this);

    std::cout << "[dashboard] Web UI: http://localhost:" << port << std::endl;
    return true;
}

void DashboardServer::stop() {
    m_stop = true;
    m_running = false;
    if (m_listen_sock != INVALID_SOCKET) {
        closesocket(m_listen_sock);
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void DashboardServer::acceptLoop() {
    while (!m_stop) {
        int timeout = 1000;
        setsockopt(m_listen_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        SOCKET client = accept(m_listen_sock, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;

        std::thread(&DashboardServer::handleClient, this, client).detach();
    }
}

void DashboardServer::handleClient(SOCKET client) {
    char buf[4096];
    int timeout = 5000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { closesocket(client); return; }
    buf[n] = '\0';

    std::string request(buf, n);
    bool isApi = (request.find("GET /api/stats") != std::string::npos);
    bool isRoot = (request.find("GET / ") != std::string::npos) ||
                  (request.find("GET / HTTP") != std::string::npos);

    std::string response;

    if (isApi) {
        // Build JSON stats
        json j;
        {
            std::lock_guard<std::mutex> lock(m_pool_mutex);
            j["pool_url"] = m_pool_url;
            j["pool_connected"] = m_pool_connected;
            j["shares_accepted"] = m_shares_accepted;
            j["shares_rejected"] = m_shares_rejected;
            j["hashrate_total"] = m_hashrate_total;
        }

        auto boards = m_boards->getStats();
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
            jb["ip_addr"] = b.info.ip_addr;
            jboards.push_back(jb);
        }
        j["boards"] = jboards;

        std::string body = j.dump();
        response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Content-Length: " + std::to_string(body.length()) +
                   "\r\n\r\n" + body;
    } else {
        // Serve dashboard HTML
        std::string body = DASHBOARD_HTML;
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                   "Content-Length: " + std::to_string(body.length()) +
                   "\r\n\r\n" + body;
    }

    ::send(client, response.c_str(), (int)response.length(), 0);
    closesocket(client);
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
