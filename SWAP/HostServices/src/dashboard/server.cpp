#include "server.h"
#include "../platform/platform.h"
#include "../config.h"
#include "../mine/sha256.h"
#include "../json.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

// Embedded HTML dashboard page
static const char* DASHBOARD_HTML =
    R"html(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>BTCMinerControl</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{color-scheme:light}
html{background:#d4d0c8}
body{font-family:Tahoma,Arial,sans-serif;background:#d4d0c8;color:#202020;padding:14px;font-size:13px;height:100vh;display:grid;grid-template-rows:auto auto auto minmax(0,1fr);overflow:hidden}
h1{background:#003b73;color:#fff;border:1px solid #00284f;padding:7px 10px;margin-bottom:8px;font-family:Arial,sans-serif;font-size:22px;line-height:1.2}
.header{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;flex-wrap:wrap;gap:8px;background:#ece9d8;border:2px outset #f4f2e9;padding:8px 10px}
.status{display:inline-block;padding:2px 8px;margin-left:6px;border:1px solid;font-size:12px;font-weight:bold;line-height:18px}
.status.online{background:#dff0d8;border-color:#3c763d;color:#245b27}
.status.offline{background:#f2dede;border-color:#a94442;color:#8a1f1d}
.status.waiting{background:#fcf8e3;border-color:#8a6d3b;color:#6e561f}
.pool-link{display:inline-block;margin-left:10px;padding:2px 8px;background:#e5e5e5;border:1px solid #7f7f7f;color:#003b73;text-decoration:underline;line-height:18px}
.pool-link.disabled{color:#777;text-decoration:none;pointer-events:none}
.header-actions{display:flex;align-items:center;gap:10px}
.icon-button{width:25px;height:25px;display:inline-flex;align-items:center;justify-content:center;padding:3px;background:#e5e5e5;color:#003b73;border:2px outset #f4f2e9;cursor:pointer;vertical-align:middle}
.icon-button svg{width:15px;height:15px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:square;stroke-linejoin:miter}
.icon-button:active{border-style:inset}
.icon-button:disabled{color:#888;cursor:default;opacity:.55}
.latency-test-button{width:24px;height:24px;padding:2px;background:transparent;color:#111;border:1px solid transparent;border-radius:0}
.latency-test-button svg{width:17px;height:17px;stroke-linecap:round;stroke-linejoin:round}
.latency-test-button:hover:not(:disabled){background:#d9e7f5;border-color:#7f9db9}
.latency-test-button:active:not(:disabled){border-style:solid;transform:translateY(1px)}
.latency-test-button:disabled{color:#888;background:transparent;border-color:transparent}
.device-ok{color:#245b27;font-weight:bold}
.device-missing{color:#8a1f1d;font-weight:bold}
.device-read-error{color:#8a6d3b;font-weight:bold}
.metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px;margin-bottom:10px}
.metric{background:#fff;border:1px solid #7f7f7f;padding:9px 8px;text-align:center;box-shadow:inset 1px 1px 0 #fff}
.metric .value{font-family:Arial,sans-serif;font-size:21px;font-weight:bold;color:#003b73}
.metric .label{font-size:11px;color:#555;margin-top:3px;text-transform:uppercase}
#uptime{color:#555;font-family:'Courier New',monospace;font-size:12px}
#listenerSummary{color:#555;font-family:'Courier New',monospace;font-size:12px}
.workspace{display:grid;grid-template-rows:minmax(0,2fr) minmax(0,1fr);gap:10px;min-height:0}
.board-table-wrap{width:100%;min-height:0;overflow:auto;background:#fff;border:2px inset #eee}
.board-table{width:100%;min-width:1420px;border-collapse:collapse;table-layout:auto;font-size:12px}
.board-table th,.board-table td{border:1px solid #a7a7a7;padding:4px 5px;text-align:left;white-space:nowrap;height:27px}
.board-table th{position:sticky;top:0;z-index:2;background:#d9e7f5;color:#003b73;border-color:#7f9db9;font-weight:bold}
.board-table tbody tr:nth-child(even){background:#f3f6f9}
.board-table tbody tr:hover{background:#fff4c4}
.board-table tr.offline{color:#777;background:#e8e8e8}
.board-table .number{text-align:right;font-family:'Courier New',monospace}
.power-control{display:inline-flex;align-items:center;gap:5px;font-weight:bold}
th.col-latency{width:92px}
th.col-temp{width:72px}
.freq-cell{display:inline-flex;align-items:center;gap:5px;white-space:nowrap}
.freq-target{color:#777;font-size:11px}
.mini-button{height:21px;min-width:36px;padding:0 7px;background:#e5e5e5;color:#003b73;border:2px outset #f4f2e9;font:bold 11px Tahoma,Arial,sans-serif;cursor:pointer}
.freq-input{width:64px;height:21px;padding:0 3px;border:2px inset #eee;font:11px 'Courier New',monospace;text-align:right}
.freq-input:disabled{background:#eee;color:#888}
.mini-button:active{border-style:inset}
.mini-button:disabled{color:#888;cursor:default;opacity:.55}
.power-toggle{width:17px;height:17px;accent-color:#2f7d32;vertical-align:middle}
.power-toggle:disabled{opacity:.45}
.latency-value{display:inline-block;min-width:46px;font-family:'Courier New',monospace}
.latency-pending{color:#8a6d3b}.latency-timeout{color:#8a1f1d}
.latency-good{color:#247126;font-weight:bold}.latency-medium{color:#a15c00;font-weight:bold}.latency-bad{color:#a11f1f;font-weight:bold}
.latency-cell{min-width:88px}
.latency-control{display:flex;align-items:center;width:100%;gap:5px}
.latency-control .icon-button{margin-left:auto;flex:0 0 auto}
.terminal-panel{width:100%;min-height:0;display:flex;flex-direction:column;background:#d4d0c8;border:2px outset #f4f2e9;padding:4px}
.terminal-title{background:#003b73;color:#fff;border:1px solid #00284f;padding:4px 7px;font-weight:bold}
.terminal{width:100%;min-height:0;flex:1;overflow:auto;background:#0c0c0c;color:#d4d4d4;border:2px inset #eee;padding:7px;font:12px/1.5 'Courier New',monospace;white-space:pre;overflow-wrap:normal}
.terminal-line.info{color:#d4d4d4}.terminal-line.warn{color:#ffd866}.terminal-line.error{color:#ff7777}
@media(max-width:600px){body{padding:8px;height:auto;min-height:100vh;display:block;overflow:auto}.header{align-items:flex-start;flex-direction:column}.workspace{display:block}.board-table-wrap{max-height:55vh}.terminal-panel{height:250px;margin-top:10px}}
</style>)html"
    R"html(
</head>
<body>
<h1>BTCMinerControl</h1>
<div class="header">
  <div>
    <span id="connectionLabel">Pool</span>: <span id="poolUrl">-</span>
    <span id="poolStatus" class="status offline">disconnected</span>
    <a id="poolManagementLink" class="pool-link disabled" target="_blank" rel="noopener noreferrer">Pool Management</a>
    <a class="pool-link" href="/settings">Settings</a>
  </div>
  <div class="header-actions">
    <div id="listenerSummary">Board: - | Web: - | Detect: -</div>
    <button id="latencyAllButton" class="icon-button latency-test-button" aria-label="Test all board latency" title="Test all board latency" onclick="testAllLatencies(this)">
      <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 14l4-4"/><path d="M3.34 19a10 10 0 1 1 17.32 0"/></svg>
    </button>
    <div id="uptime"></div>
  </div>
</div>
<div class="metrics">
  <div class="metric"><div class="value" id="hashrateTotal">0</div><div class="label">Total GH/s</div></div>
  <div class="metric"><div class="value" id="sharesAccepted">0</div><div class="label">Accepted</div></div>
  <div class="metric"><div class="value" id="sharesRejected">0</div><div class="label">Rejected</div></div>
  <div class="metric"><div class="value" id="boardCount">0</div><div class="label">Boards Online</div></div>
</div>
<div class="workspace">
  <div class="board-table-wrap">
    <table class="board-table">
      <thead><tr>
        <th>Board ID</th><th>IP</th><th>FW</th><th>Status</th><th class="col-latency">Latency (ms)</th><th>Test</th><th>ASIC</th>
        <th>Freq (MHz)</th><th>Hashrate</th><th>Vout (V)</th><th>Iout (A)</th>
        <th>Power (W)</th><th class="col-temp">Temperature (°C)</th><th>TPS Power</th>
        <th>Jobs</th><th>Nonces</th><th>A/R</th><th>Last Share</th><th>Best Diff</th>
      </tr></thead>
      <tbody id="boardTableBody"></tbody>
    </table>
  </div>
  <aside class="terminal-panel">
    <div class="terminal-title">Board Status</div>
    <div class="terminal" id="statusTerminal"></div>
  </aside>
</div>
<script>
const API_URL = '/api/stats';
let uptimeBaseMs = 0;
let uptimeSyncedAt = Date.now();
let lastEventKey = '';
let lastBoardTableHtml = '';
const pendingPowerStates = new Map();
async function actionFetch(url, options) {
    options = options || {};
    options.method = options.method || 'POST';
    options.headers = Object.assign({}, options.headers || {}, {
        'X-BTCMiner-Control': '1'
    });
    const response = await fetch(url, options);
    if(response.status === 401) {
        location.reload();
        throw new Error('Unauthorized');
    }
    return response;
}
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
function pad2(value) { return String(value).padStart(2, '0'); }
function formatShareAge(seconds) {
    if(seconds === null || seconds === undefined || seconds < 0) return '-';
    const total = Math.floor(seconds);
    const h = Math.floor(total / 3600);
    const m = Math.floor((total % 3600) / 60);
    const s = total % 60;
    return `${h}h${pad2(m)}m${pad2(s)}s`;
}
function updateUptime() {
    const elapsed = Math.max(0, Math.floor(
        (uptimeBaseMs + Date.now() - uptimeSyncedAt) / 1000));
    const h=Math.floor(elapsed/3600), m=Math.floor((elapsed%3600)/60), s=elapsed%60;
    document.getElementById('uptime').textContent = `uptime: ${h}h ${m}m ${s}s`;
}
async function setBoardPower(boardId, enabled, control) {
    const previous = !enabled;
    pendingPowerStates.set(boardId, {enabled, expiresAt: Date.now() + 5000});
    const stateLabel = control.nextElementSibling;
    if(stateLabel) stateLabel.textContent = enabled ? 'ON' : 'OFF';
    control.disabled = true;
    try {
        const url = `/api/board-power?id=${encodeURIComponent(boardId)}&enabled=${enabled ? 1 : 0}`;
        const response = await actionFetch(url, {method: 'POST'});
        if(!response.ok) throw new Error(`HTTP ${response.status}`);
        setTimeout(refresh, 350);
    } catch(error) {
        pendingPowerStates.delete(boardId);
        control.checked = previous;
        if(stateLabel) stateLabel.textContent = previous ? 'ON' : 'OFF';
        alert('电源命令发送失败，板卡可能已离线。');
        console.error(error);
    } finally {
        if(control.isConnected) control.disabled = false;
    }
}
async function setBoardFrequency(boardId, button) {
    const cell = button.closest('.freq-cell');
    const input = cell ? cell.querySelector('.freq-input') : null;
    const mhz = input ? Number.parseInt(input.value, 10) : NaN;
    if(!input || !Number.isInteger(mhz) || mhz < 0 || mhz > 600) {
        alert('Frequency must be an integer between 0 and 600 MHz.');
        if(input) input.focus();
        return;
    }
    try {
        const response = await actionFetch(
            `/api/board-frequency?id=${encodeURIComponent(boardId)}&mhz=${mhz}`,
            {method:'POST'});
        if(!response.ok) throw new Error(`HTTP ${response.status}`);
        setTimeout(refresh, 350);
    } catch(error) {
        alert('Frequency change failed. The board may be offline.');
        console.error(error);
    }
}

async function postLatencyTest(url, control) {
    control.disabled = true;
    try {
        const response = await actionFetch(url, {method: 'POST'});
        if(!response.ok) throw new Error(`HTTP ${response.status}`);
        setTimeout(refresh, 100);
    } catch(error) {
        alert('Latency test failed. The board may be offline.');
        console.error(error);
    } finally {
        if(control.isConnected) control.disabled = false;
    }
}
function testBoardLatency(boardId, control) {
    return postLatencyTest(
        `/api/board-latency?id=${encodeURIComponent(boardId)}`, control);
}
function testAllLatencies(control) {
    return postLatencyTest('/api/board-latency-all', control);
}
function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}
function updateUI(data) {
    const testMode = data.test_mode || '';
    const frequencyControl = data.frequency_control_enabled === true;
    document.getElementById('connectionLabel').textContent = testMode ? 'Mode' : 'Pool';
    document.getElementById('poolUrl').textContent = testMode || data.pool_url;
    const ps = document.getElementById('poolStatus');
    ps.textContent = testMode ? 'active' : (data.pool_connected ? 'online' : 'disconnected');
    ps.className = 'status ' + ((testMode || data.pool_connected) ? 'online' : 'offline');
    const poolLink = document.getElementById('poolManagementLink');
    if(data.pool_management_url) {
        poolLink.href = data.pool_management_url;
        poolLink.className = 'pool-link';
    } else {
        poolLink.removeAttribute('href');
        poolLink.className = 'pool-link disabled';
    }
    document.getElementById('hashrateTotal').textContent = formatHashrate(data.hashrate_total);
    document.getElementById('sharesAccepted').textContent = data.shares_accepted;
    document.getElementById('sharesRejected').textContent = data.shares_rejected;
    const onlineBoardCount = data.boards.filter(b=>b.online).length;
    document.getElementById('boardCount').textContent = onlineBoardCount;
    document.getElementById('latencyAllButton').disabled = onlineBoardCount === 0;
    document.getElementById('listenerSummary').textContent =
        `Board: ${data.board_port} | Web: ${data.dashboard_port} | Detect: ${(data.board_detection_interval_ms/1000).toFixed(data.board_detection_interval_ms%1000?1:0)}s`;
    uptimeBaseMs = Number(data.uptime_ms) || 0;
    uptimeSyncedAt = Date.now();
    updateUptime();

    const tableBody = document.getElementById('boardTableBody');
    const shareAges = {};
    data.boards.forEach(b => {
        const boardId = b.board_id_hex || Number(b.board_id).toString(16).toUpperCase();
        shareAges[boardId] = formatShareAge(b.last_share_accepted_age_seconds);
    });
    const boardTableHtml = data.boards.map(b => {
        const boardId = b.board_id_hex || Number(b.board_id).toString(16).toUpperCase();
        const online = b.online
            ? '<span class="device-ok">Online</span>'
            : '<span class="device-missing">Offline</span>';
        const latencyMs = Number(b.latency_ms);
        const latencyText = !b.online ? 'N/A' :
            (b.latency_pending ? 'Testing...' :
            (b.latency_timeout ? 'Timeout' :
            (b.latency_valid ? latencyMs.toFixed(2) : '-')));
        const latencyClass = b.latency_pending ? ' latency-pending' :
            (b.latency_timeout ? ' latency-timeout' :
            (b.latency_valid ? (latencyMs < 50 ? ' latency-good' :
            (latencyMs < 100 ? ' latency-medium' : ' latency-bad')) : ''));
        const latencyHtml = `<span class="latency-control"><span class="latency-value${latencyClass}">${latencyText}</span><button class="icon-button latency-test-button" data-latency-board-id="${boardId}" aria-label="Test latency for ${boardId}" title="Test latency" onclick="testBoardLatency('${boardId}',this)" ${b.online && !b.latency_pending?'':'disabled'}><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 14l4-4"/><path d="M3.34 19a10 10 0 1 1 17.32 0"/></svg></button></span>`;
        const testState = !testMode ? '-' :
            (b.nonces_returned === 0 ? 'WAITING' : (b.best_diff >= 256 ? 'PASS' : 'FAIL'));
        const power = b.tps_detected && b.tps_telemetry_valid
            ? Number(b.power_w).toFixed(2)
            : 'N/A';
        const temperature = b.tmp1075_detected && b.tmp1075_telemetry_valid
            ? Number(b.temperature_c).toFixed(2)
            : 'N/A';
        const vout = b.tps_telemetry_valid ? Number(b.voltage_v).toFixed(3) : 'N/A';
        const iout = b.tps_telemetry_valid ? Number(b.current_a).toFixed(3) : 'N/A';
        const tpsTemp = b.tps_telemetry_valid ? Number(b.tps_temperature_c).toFixed(2) : 'N/A';
        const fwVersion = b.fw_version
            ? `v${(b.fw_version >> 8) & 255}.${b.fw_version & 255}`
            : '-';
        const actualMhz = b.actual_frequency_mhz || 0;
        const targetMhz = b.target_frequency_mhz || 0;
        const editMhz = targetMhz || actualMhz || 200;
        const freqTitle = `Target: ${targetMhz || '?'} MHz / Actual: ${actualMhz || '?'} MHz / TPS temp: ${tpsTemp} °C / TPS status: 0x${Number(b.tps_status_word||0).toString(16).toUpperCase().padStart(4,'0')}`;
        const freqReadout = `<span class="number">${actualMhz || '-'}</span><span class="freq-target">${targetMhz ? 'set ' + targetMhz : ''}</span>`;
        const freqHtml = frequencyControl
            ? `<span class="freq-cell" title="${freqTitle}">${freqReadout}<input type="number" class="freq-input" min="0" max="600" step="1" value="${editMhz}" data-board-id="${boardId}" aria-label="Target frequency for ${boardId}" ${b.online ? '' : 'disabled'}><button class="mini-button" ${b.online ? '' : 'disabled'} onclick="setBoardFrequency('${boardId}',this)">SET</button></span>`
            : `<span class="freq-cell" title="${freqTitle}">${freqReadout}</span>`;
        let pending = pendingPowerStates.get(boardId);
        if(pending && (Date.now() >= pending.expiresAt ||
            (b.power_state_valid && b.power_enabled === pending.enabled))) {
            pendingPowerStates.delete(boardId);
            pending = undefined;
        }
        const canControl = b.online && b.tps_detected;
        const checked = pending ? pending.enabled :
            (b.power_state_valid ? b.power_enabled : true);
        const switchHtml = `<label class="power-control"><input class="power-toggle" data-board-id="${boardId}" aria-label="TPS power for ${boardId}" type="checkbox" ${checked?'checked':''} ${canControl?'':'disabled'} onchange="setBoardPower('${boardId}',this.checked,this)"><span>${checked?'ON':'OFF'}</span></label>`;
        return `<tr class="${b.online?'online':'offline'}">
            <td><strong>${boardId}</strong></td><td>${escapeHtml(b.ip_addr||'-')}</td><td>${fwVersion}</td><td>${online}</td><td class="latency-cell">${latencyHtml}</td><td>${testState}</td>
            <td class="number">${b.asic_count}</td><td>${freqHtml}</td><td class="number">${formatHashrate(b.hashrate ?? b.hashrate_1m)}</td>
            <td class="number">${vout}</td><td class="number">${iout}</td>
            <td class="number" title="TPS temp: ${tpsTemp} °C">${power}</td><td class="number">${temperature}</td>
            <td>${switchHtml}</td><td class="number">${b.jobs_sent}</td>
            <td class="number">${b.nonces_returned}</td><td class="number">${b.shares_accepted}/${b.shares_rejected}</td>
            <td class="number share-age" data-board-id="${boardId}" title="Time since the last pool-accepted share">-</td>
            <td class="number">${Number(b.best_diff).toFixed(3)}</td></tr>`;
    }).join('');
    if(boardTableHtml !== lastBoardTableHtml) {
        // The whole table is re-rendered from scratch, which would wipe any
        // value the user is typing into a frequency input. Save each input's
        // text AND caret position, then restore both, so 1-second refreshes
        // never interrupt editing and repeated SETs always send what the
        // user actually typed.
        const freqEdits = {};
        let activeFreqBoard = '';
        const activeElement = document.activeElement;
        if(activeElement && activeElement.classList &&
           activeElement.classList.contains('freq-input')) {
            activeFreqBoard = activeElement.getAttribute('data-board-id') || '';
        }
        tableBody.querySelectorAll('.freq-input').forEach(input => {
            const id = input.getAttribute('data-board-id') || '';
            if(id) freqEdits[id] = {
                value: input.value,
                selStart: input.selectionStart,
                selEnd: input.selectionEnd
            };
        });

        lastBoardTableHtml = boardTableHtml;
        tableBody.innerHTML = boardTableHtml;

        tableBody.querySelectorAll('.freq-input').forEach(input => {
            const id = input.getAttribute('data-board-id') || '';
            if(id && Object.prototype.hasOwnProperty.call(freqEdits, id)) {
                const edit = freqEdits[id];
                input.value = edit.value;
                if(id === activeFreqBoard) {
                    input.focus();
                    try {
                        const pos = (edit.selStart === null || edit.selStart === undefined)
                            ? input.value.length : edit.selStart;
                        input.setSelectionRange(pos,
                            (edit.selEnd === null || edit.selEnd === undefined)
                                ? pos : edit.selEnd);
                    } catch(e) { /* type=number can reject selection APIs in some browsers */ }
                }
            }
        });
        if(activeFreqBoard &&
           !tableBody.querySelector(`.freq-input[data-board-id="${activeFreqBoard}"]`)) {
            // The focused board row disappeared (board list changed); nothing to restore.
        }
    }
    document.querySelectorAll('.share-age').forEach(cell => {
        const boardId = cell.getAttribute('data-board-id') || '';
        cell.textContent = shareAges[boardId] || '-';
    });

    const events = data.events || [];
    const eventKey = events.length ? events[events.length-1].timestamp + '|' + events[events.length-1].message : 'empty';
    if(eventKey !== lastEventKey) {
        lastEventKey = eventKey;
        const terminal = document.getElementById('statusTerminal');
        terminal.innerHTML = events.map(e =>
            `<div class="terminal-line ${String(e.level).toLowerCase()}">${escapeHtml(e.timestamp)} [${escapeHtml(e.level)}] ${escapeHtml(e.message)}</div>`
        ).join('');
        terminal.scrollTop = terminal.scrollHeight;
    }
}
refresh();
updateUptime();
setInterval(updateUptime, 250);
setInterval(refresh, 1000);
</script>
</body>
</html>)html";

static const char* LOGIN_HTML = R"html(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>BTCMinerControl - Login</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
html{background:#d4d0c8}
body{font-family:Tahoma,Arial,sans-serif;background:#d4d0c8;color:#202020;font-size:13px;height:100vh;display:flex;align-items:center;justify-content:center}
.login-panel{width:340px;background:#ece9d8;border:2px outset #f4f2e9;padding:5px}
.login-title{background:#003b73;color:#fff;padding:8px 10px;font-size:18px;font-weight:bold}
.login-form{padding:16px}
.login-form label{display:block;margin:8px 0 4px;font-weight:bold}
.login-form input{width:100%;height:30px;border:2px inset #eee;padding:4px 6px;font:13px Tahoma}
.login-form button{height:30px;margin-top:14px;border:2px outset #f4f2e9;background:#e5e5e5;color:#202020;padding:5px 14px;font-weight:bold;cursor:pointer}
.login-form button:active{border-style:inset}
.login-error{display:none;margin-top:12px;font-weight:bold;color:#a11f1f}
.login-hint{color:#666;margin-top:10px;line-height:1.5}
</style>
</head>
<body>
<div class="login-panel">
  <div class="login-title">BTCMinerControl - Authentication</div>
  <form class="login-form" id="login-form">
    <label for="password">Dashboard password</label>
    <input id="password" name="password" type="password" autocomplete="current-password" autofocus>
    <div class="login-error" id="login-error">Password required or incorrect.</div>
    <button type="submit">Login</button>
    <div class="login-hint">The dashboard is private. Enter the password stored in config.json (dashboard_password).</div>
  </form>
</div>
<script>
document.getElementById('login-form').addEventListener('submit', async function(e){
    e.preventDefault();
    const password = document.getElementById('password').value;
    const error = document.getElementById('login-error');
    error.style.display = 'none';
    if(!password) { error.textContent = 'Password required.'; error.style.display = 'block'; return; }
    try {
        const response = await fetch('/api/login', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body: 'password=' + encodeURIComponent(password)
        });
        if(response.ok) { location.reload(); return; }
        if(response.status === 429) error.textContent = 'Too many failed attempts. Try again later.';
        else error.textContent = 'Password incorrect.';
        error.style.display = 'block';
    } catch(err) {
        error.textContent = 'Login failed: ' + err;
        error.style.display = 'block';
    }
});
</script>
</body>
</html>)html";

namespace {

uint64_t steady_time_us() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string header_value(const std::string& request,
                         const std::string& requested_name) {
    const size_t headers_end = request.find("\r\n\r\n");
    size_t line_start = request.find("\r\n");
    if (headers_end == std::string::npos || line_start == std::string::npos) {
        return std::string();
    }
    line_start += 2;
    while (line_start < headers_end) {
        const size_t line_end = request.find("\r\n", line_start);
        if (line_end == std::string::npos || line_end > headers_end) break;
        const size_t separator = request.find(':', line_start);
        if (separator != std::string::npos && separator < line_end) {
            std::string name = request.substr(line_start, separator - line_start);
            std::transform(name.begin(), name.end(), name.begin(),
                [](unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });
            if (name == requested_name) {
                size_t value_start = separator + 1;
                while (value_start < line_end &&
                       (request[value_start] == ' ' ||
                        request[value_start] == '\t')) {
                    ++value_start;
                }
                return request.substr(value_start, line_end - value_start);
            }
        }
        line_start = line_end + 2;
    }
    return std::string();
}

int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::string url_decode(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            decoded.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const int high = hex_digit(value[i + 1]);
            const int low = hex_digit(value[i + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                i += 2;
            } else {
                decoded.push_back(value[i]);
            }
        } else {
            decoded.push_back(value[i]);
        }
    }
    return decoded;
}

std::string form_value(const std::string& body, const std::string& name) {
    size_t start = 0;
    while (start <= body.size()) {
        const size_t end = body.find('&', start);
        const std::string field = body.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        const size_t separator = field.find('=');
        if (separator != std::string::npos &&
            url_decode(field.substr(0, separator)) == name) {
            return url_decode(field.substr(separator + 1));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return std::string();
}

bool parse_bounded_uint(const std::string& text, uint32_t minimum,
                        uint32_t maximum, uint32_t& value) {
    if (text.empty()) return false;
    try {
        size_t consumed = 0;
        const unsigned long parsed = std::stoul(text, &consumed, 10);
        if (consumed != text.size() || parsed < minimum || parsed > maximum) {
            return false;
        }
        value = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

std::string html_escape(const std::string& value) {
    std::string escaped;
    for (char character : value) {
        switch (character) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&#39;"; break;
            default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

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
                          const char* content_type, const std::string& body,
                          const std::string& set_cookie = std::string()) {
    return "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n"
           "Content-Type: " + content_type + "\r\n"
           "X-Content-Type-Options: nosniff\r\n"
           "Cache-Control: no-store\r\n" +
           (set_cookie.empty() ? std::string()
                               : "Set-Cookie: " + set_cookie + "\r\n") +
           "Connection: close\r\n"
           "Content-Length: " + std::to_string(body.size()) +
           "\r\n\r\n" + body;
}

std::string redirect_response(const std::string& location,
                              const std::string& cookie = std::string()) {
    std::string response = "HTTP/1.1 303 See Other\r\nLocation: " + location +
           "\r\nCache-Control: no-store\r\nConnection: close\r\n";
    if (!cookie.empty()) response += "Set-Cookie: " + cookie + "\r\n";
    response += "Content-Length: 0\r\n\r\n";
    return response;
}

std::string settings_page(uint16_t configured_board_port,
                          uint16_t configured_dashboard_port,
                          uint32_t detection_interval_ms,
                          uint16_t active_board_port,
                          uint16_t active_dashboard_port,
                          const std::string& message = std::string(),
                          bool error = false) {
    const std::string message_html = message.empty() ? std::string() :
        "<div class=\"message " + std::string(error ? "error" : "ok") + "\">" +
        html_escape(message) + "</div>";
    return "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Listener &amp; Detection Settings</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#d4d0c8;color:#202020;"
        "font:13px Tahoma,Arial,sans-serif;display:flex;align-items:center;"
        "justify-content:center;min-height:100vh}.panel{width:480px;background:#ece9d8;"
        "border:2px outset #f4f2e9;padding:5px}.title{background:#003b73;color:#fff;"
        "padding:8px 10px;font-size:18px;font-weight:bold}.form{padding:16px}"
        "label{display:block;margin:10px 0 4px;font-weight:bold}input{width:100%;"
        "height:30px;border:2px inset #eee;padding:4px 6px;font:13px Tahoma}"
        ".hint{color:#666;margin-top:5px;line-height:1.5}.active{margin-bottom:12px;"
        "padding:8px;background:#fff;border:1px solid #7f7f7f;font-family:'Courier New'}"
        ".actions{display:flex;gap:8px;margin-top:16px}button,a{height:30px;"
        "border:2px outset #f4f2e9;background:#e5e5e5;color:#202020;padding:5px 14px;"
        "text-decoration:none;font-weight:bold}.message{margin-top:12px;font-weight:bold}"
        ".error{color:#a11f1f}.ok{color:#247126}</style></head><body><div class=\"panel\">"
        "<div class=\"title\">Listener &amp; Detection Settings</div>"
        "<form class=\"form\" id=\"settings-form\" onsubmit=\"return saveSettings(event)\">"
        "<div class=\"active\">Active now: board " +
        std::to_string(active_board_port) + " / web " +
        std::to_string(active_dashboard_port) + "</div>"
        "<label for=\"board_port\">Board listener port</label>"
        "<input id=\"board_port\" name=\"board_port\" type=\"number\" min=\"1\" max=\"65535\" required value=\"" +
        std::to_string(configured_board_port) + "\">"
        "<label for=\"dashboard_port\">Web UI listener port</label>"
        "<input id=\"dashboard_port\" name=\"dashboard_port\" type=\"number\" min=\"1\" max=\"65535\" required value=\"" +
        std::to_string(configured_dashboard_port) + "\">"
        "<label for=\"detection_seconds\">Board detection period (seconds)</label>"
        "<input id=\"detection_seconds\" name=\"detection_seconds\" type=\"number\" min=\"1\" max=\"3600\" required value=\"" +
        std::to_string(detection_interval_ms / 1000) + "\">"
        "<div class=\"hint\">Each cycle probes all online boards and updates their latency. "
        "The detection period takes effect immediately. Listener port changes take effect "
        "after restarting BTCMinerControl.</div>" + message_html +
        "<div class=\"actions\"><button type=\"submit\">Save</button>"
        "<a href=\"/\">Back</a></div></form></div>"
        "<script>"
        "function saveSettings(e){"
        "e.preventDefault();"
        "const f=e.target;"
        "const body=new URLSearchParams();"
        "body.append('board_port',f.board_port.value);"
        "body.append('dashboard_port',f.dashboard_port.value);"
        "body.append('detection_seconds',f.detection_seconds.value);"
        "fetch('/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-BTCMiner-Control':'1'},body:body.toString()})"
        ".then(async function(r){if(r.status===401){location.reload();return;}const text=await r.text();document.open();document.write(text);document.close();})"
        ".catch(function(err){alert('Save failed: '+err);});"
        "return false;"
        "}"
        "</script></body></html>";
}

std::string hex_board_id(uint64_t board_id) {
    std::ostringstream out;
    out << std::hex << std::uppercase << board_id;
    return out.str();
}

bool has_control_header(const std::string& request) {
    return request.find("\r\nX-BTCMiner-Control: 1\r\n") != std::string::npos ||
           request.find("\r\nx-btcminer-control: 1\r\n") != std::string::npos;
}

std::string cookie_value(const std::string& request, const std::string& name) {
    const std::string cookies = header_value(request, "cookie");
    size_t offset = 0;
    while (offset <= cookies.size()) {
        const size_t end = cookies.find(';', offset);
        const std::string part = cookies.substr(
            offset, end == std::string::npos ? std::string::npos : end - offset);
        size_t value_start = part.find('=');
        if (value_start != std::string::npos) {
            std::string key = part.substr(0, value_start);
            while (!key.empty() && key.front() == ' ') key.erase(key.begin());
            while (!key.empty() && key.back() == ' ') key.pop_back();
            if (key == name) return part.substr(value_start + 1);
        }
        if (end == std::string::npos) break;
        offset = end + 1;
    }
    return std::string();
}

bool parse_board_id_query(const std::string& query, uint64_t& board_id) {
    const std::string id_key = "id=";
    size_t parameter_start = 0;
    while (parameter_start <= query.size()) {
        const size_t parameter_end = query.find('&', parameter_start);
        const std::string parameter = query.substr(
            parameter_start,
            parameter_end == std::string::npos ? std::string::npos
                                                : parameter_end - parameter_start);
        if (parameter.rfind(id_key, 0) == 0) {
            const std::string id_text = parameter.substr(id_key.size());
            if (id_text.empty() || id_text.size() > 16) return false;
            try {
                size_t consumed = 0;
                board_id = std::stoull(id_text, &consumed, 16);
                return consumed == id_text.size();
            } catch (...) {
                return false;
            }
        }
        if (parameter_end == std::string::npos) break;
        parameter_start = parameter_end + 1;
    }
    return false;
}

bool parse_frequency_query(const std::string& query, uint64_t& board_id,
                            uint16_t& frequency_mhz) {
    const std::string id_key = "id=";
    const std::string mhz_key = "mhz=";
    const size_t id_pos = query.find(id_key);
    const size_t mhz_pos = query.find(mhz_key);
    if (id_pos == std::string::npos || mhz_pos == std::string::npos) return false;

    const size_t id_start = id_pos + id_key.size();
    const size_t id_end = query.find('&', id_start);
    const std::string id_text = query.substr(
        id_start, id_end == std::string::npos ? std::string::npos : id_end - id_start);
    const size_t mhz_start = mhz_pos + mhz_key.size();
    const size_t mhz_end = query.find('&', mhz_start);
    const std::string mhz_text = query.substr(
        mhz_start, mhz_end == std::string::npos ? std::string::npos : mhz_end - mhz_start);

    if (id_text.empty() || id_text.size() > 16 || mhz_text.empty()) return false;
    try {
        size_t consumed = 0;
        board_id = std::stoull(id_text, &consumed, 16);
        if (consumed != id_text.size()) return false;
        consumed = 0;
        const unsigned long mhz = std::stoul(mhz_text, &consumed, 10);
        if (consumed != mhz_text.size() || mhz > 600) return false;
        frequency_mhz = static_cast<uint16_t>(mhz);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_power_query(const std::string& query, uint64_t& board_id,
                       bool& enabled) {
    const std::string id_key = "id=";
    const std::string enabled_key = "enabled=";
    const size_t id_pos = query.find(id_key);
    const size_t enabled_pos = query.find(enabled_key);
    if (id_pos == std::string::npos || enabled_pos == std::string::npos) return false;

    const size_t id_start = id_pos + id_key.size();
    const size_t id_end = query.find('&', id_start);
    const std::string id_text = query.substr(
        id_start, id_end == std::string::npos ? std::string::npos : id_end - id_start);
    const size_t enabled_start = enabled_pos + enabled_key.size();
    const size_t enabled_end = query.find('&', enabled_start);
    const std::string enabled_text = query.substr(
        enabled_start,
        enabled_end == std::string::npos ? std::string::npos : enabled_end - enabled_start);
    if (id_text.empty() || id_text.size() > 16 ||
        (enabled_text != "0" && enabled_text != "1")) return false;

    try {
        size_t consumed = 0;
        board_id = std::stoull(id_text, &consumed, 16);
        if (consumed != id_text.size()) return false;
    } catch (...) {
        return false;
    }
    enabled = enabled_text == "1";
    return true;
}

} // namespace

DashboardServer::DashboardServer() {}

DashboardServer::~DashboardServer() { stop(); }

DashboardServer::LoginStatus DashboardServer::tryLogin(
    const std::string& password, const std::string& client_ip,
    std::string& out_token) {
    out_token.clear();
    std::lock_guard<std::mutex> lock(m_auth_mutex);

    const uint64_t now = platform::tick_ms();
    auto& failure = m_login_failures[client_ip];
    if (failure.locked_until_ms > now) return LoginStatus::Locked;
    if (failure.first_failure_ms == 0 ||
        now - failure.first_failure_ms > 15ULL * 60ULL * 1000ULL) {
        failure.count = 0;
        failure.first_failure_ms = now;
        failure.locked_until_ms = 0;
    }

    bool password_ok = false;
    if (!m_dashboard_password_sha256.empty()) {
        std::vector<uint8_t> salt(m_dashboard_password_salt.size() / 2);
        if (hex2bin(m_dashboard_password_salt, salt.data(), salt.size())) {
            std::vector<uint8_t> input;
            input.reserve(salt.size() + password.size());
            input.insert(input.end(), salt.begin(), salt.end());
            input.insert(input.end(), password.begin(), password.end());
            const std::string computed =
                bin2hex(sha256::double_sha256(input.data(), input.size()));
            unsigned char difference = 0;
            for (size_t i = 0; i < computed.size(); ++i) {
                difference |= static_cast<unsigned char>(computed[i]) ^
                              static_cast<unsigned char>(
                                  m_dashboard_password_sha256[i]);
            }
            password_ok = difference == 0;
        }
    } else if (!m_dashboard_password.empty() &&
               password.size() == m_dashboard_password.size()) {
        unsigned char difference = 0;
        for (size_t i = 0; i < password.size(); ++i) {
            difference |= static_cast<unsigned char>(password[i]) ^
                          static_cast<unsigned char>(m_dashboard_password[i]);
        }
        password_ok = difference == 0;
    }

    if (!password_ok) {
        ++failure.count;
        if (failure.count >= 5) {
            failure.locked_until_ms = now + 15ULL * 60ULL * 1000ULL;
            failure.count = 0;
            failure.first_failure_ms = 0;
            return LoginStatus::Locked;
        }
        return LoginStatus::BadPassword;
    }

    m_login_failures.erase(client_ip);

    uint64_t token_a = platform::secure_random_u64();
    uint64_t token_b = platform::secure_random_u64();
    std::ostringstream token;
    token << std::hex << std::setfill('0')
          << std::setw(16) << token_a
          << std::setw(16) << token_b;
    out_token = token.str();
    m_sessions[out_token] = now + 24ULL * 60ULL * 60ULL * 1000ULL;
    return LoginStatus::Ok;
}

bool DashboardServer::isAuthorized(const std::string& request,
                                   const std::string& client_ip) {
    (void)client_ip;
    const std::string token = cookie_value(request, "btc_session");
    if (token.empty()) return false;

    std::lock_guard<std::mutex> lock(m_auth_mutex);
    const auto found = m_sessions.find(token);
    if (found == m_sessions.end()) return false;
    if (found->second <= platform::tick_ms()) {
        m_sessions.erase(found);
        return false;
    }
    return true;
}

void DashboardServer::logoutSession(const std::string& request) {
    const std::string token = cookie_value(request, "btc_session");
    if (token.empty()) return;
    std::lock_guard<std::mutex> lock(m_auth_mutex);
    m_sessions.erase(token);
}

bool DashboardServer::start(uint16_t port, BoardManager* board_mgr,
                            const std::string& bind_address,
                            const std::string& config_path,
                            uint16_t configured_board_port,
                            uint32_t detection_interval_ms,
                            const std::string& dashboard_password,
                            const std::string& password_sha256,
                            const std::string& password_salt,
                            bool frequency_control_enabled) {
    if (m_running || board_mgr == nullptr || port == 0) return false;
    m_boards = board_mgr;
    m_config_path = config_path;
    m_dashboard_password = dashboard_password;
    m_dashboard_password_sha256 = password_sha256;
    m_dashboard_password_salt = password_salt;
    m_frequency_control_enabled = frequency_control_enabled;
    std::transform(m_dashboard_password_sha256.begin(),
                   m_dashboard_password_sha256.end(),
                   m_dashboard_password_sha256.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    std::transform(m_dashboard_password_salt.begin(),
                   m_dashboard_password_salt.end(),
                   m_dashboard_password_salt.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    m_dashboard_port = port;
    m_configured_board_port = configured_board_port != 0
        ? configured_board_port : board_mgr->port();
    m_configured_dashboard_port = port;
    m_started_ms = platform::tick_ms();
    if (!m_boards->setDetectionIntervalMs(detection_interval_ms)) return false;

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
    platform::set_nonblocking(m_listen_sock, true);

    m_running = true;
    m_stop = false;
    m_thread = std::thread(&DashboardServer::acceptLoop, this);

    // Print a usable Web UI URL. When bound to the wildcard (0.0.0.0 / "::"),
    // the server listens on every interface, but "0.0.0.0" itself is not an
    // address a browser can connect to on Windows. Print "localhost" (always
    // works on this machine) plus each LAN IPv4 so remote clients know what
    // to type.
    const bool wildcard = bind_address.empty() || bind_address == "0.0.0.0" || bind_address == "::";
    if (wildcard) {
        std::cout << "[dashboard] Web UI:  http://localhost:" << port << std::endl;
        char host[256];
        if (gethostname(host, sizeof(host)) == 0) {
            addrinfo hints = {};
            hints.ai_family = AF_INET;
            addrinfo* res = nullptr;
            if (getaddrinfo(host, nullptr, &hints, &res) == 0) {
                for (addrinfo* p = res; p; p = p->ai_next) {
                    char ip[INET_ADDRSTRLEN] = {};
                    auto* sin = reinterpret_cast<sockaddr_in*>(p->ai_addr);
                    if (inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip)) &&
                        std::string(ip) != "127.0.0.1") {
                        std::cout << "[dashboard] LAN URL: http://" << ip << ':' << port << std::endl;
                    }
                }
                freeaddrinfo(res);
            }
        }
    } else {
        std::cout << "[dashboard] Web UI: http://" << bind_address << ':' << port << std::endl;
    }
    if (m_dashboard_password_sha256.empty() && m_dashboard_password.empty()) {
        std::cout << "[dashboard] Authentication: LOGIN DISABLED "
                     "(no dashboard password configured; UI is read-only)"
                  << std::endl;
    } else if (!m_dashboard_password_sha256.empty()) {
        std::cout << "[dashboard] Authentication: session login required; "
                     "salted SHA-256 password + brute-force lockout"
                  << std::endl;
    } else {
        std::cout << "[dashboard] Authentication: session login required; "
                     "plaintext config password + brute-force lockout"
                  << std::endl;
    }
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
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
    platform::set_recv_timeout(client, 5000);
    std::string request;
    while (request.find("\r\n\r\n") == std::string::npos &&
           request.size() < 8192) {
        int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        request.append(buf, static_cast<size_t>(n));
    }

    const size_t headers_end = request.find("\r\n\r\n");
    size_t content_length = 0;
    if (headers_end != std::string::npos) {
        const std::string length_text = header_value(request, "content-length");
        if (!length_text.empty()) {
            try {
                content_length = static_cast<size_t>(std::stoul(length_text));
            } catch (...) {
                content_length = 8193;
            }
        }
        if (content_length <= 8192) {
            const size_t expected_size = headers_end + 4 + content_length;
            while (request.size() < expected_size) {
                const int n = recv(client, buf, static_cast<int>(std::min<size_t>(
                    sizeof(buf), expected_size - request.size())), 0);
                if (n <= 0) break;
                request.append(buf, static_cast<size_t>(n));
            }
        }
    }
    const std::string body = headers_end == std::string::npos ? std::string() :
        request.substr(headers_end + 4,
            std::min(content_length, request.size() - (headers_end + 4)));

    std::string response;
    std::istringstream request_line(request.substr(0, request.find("\r\n")));
    std::string method, target, version;
    request_line >> method >> target >> version;
    const size_t query = target.find('?');
    const std::string query_string =
        query == std::string::npos ? std::string() : target.substr(query + 1);
    if (query != std::string::npos) target.erase(query);

    std::string client_ip = "unknown";
    {
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        if (getpeername(client, reinterpret_cast<sockaddr*>(&peer),
                        &peer_len) == 0) {
            char ip[INET_ADDRSTRLEN]{};
            if (inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip))) {
                client_ip = ip;
            }
        }
    }

    if (version.rfind("HTTP/", 0) != 0) {
        response = http_response(400, "Bad Request", "text/plain; charset=utf-8",
                                 "Bad Request\n");
    } else if (content_length > 8192) {
        response = http_response(413, "Payload Too Large",
                                 "text/plain; charset=utf-8",
                                 "Payload Too Large\n");
    } else if (method == "POST" && target == "/api/login") {
        const std::string password = form_value(body, "password");
        std::string token;
        const LoginStatus status = tryLogin(password, client_ip, token);
        if (status == LoginStatus::Ok) {
            const std::string cookie =
                "btc_session=" + token +
                "; HttpOnly; SameSite=Strict; Path=/; Max-Age=86400";
            response = http_response(200, "OK", "application/json; charset=utf-8",
                                     "{\"ok\":true}", cookie);
        } else if (status == LoginStatus::Locked) {
            response = http_response(429, "Too Many Requests",
                                     "application/json; charset=utf-8",
                                     "{\"ok\":false,\"error\":\"locked\"}");
        } else {
            response = http_response(401, "Unauthorized",
                                     "application/json; charset=utf-8",
                                     "{\"ok\":false,\"error\":\"bad password\"}");
        }
    } else if (method == "POST" && target == "/api/logout") {
        logoutSession(request);
        response = http_response(200, "OK", "application/json; charset=utf-8",
                                 "{\"ok\":true}",
                                 "btc_session=; HttpOnly; SameSite=Strict; "
                                 "Path=/; Max-Age=0");
    } else if (!isAuthorized(request, client_ip)) {
        if (target == "/" || target == "/settings") {
            response = http_response(200, "OK", "text/html; charset=utf-8",
                                     LOGIN_HTML);
        } else {
            response = http_response(401, "Unauthorized",
                                     "application/json; charset=utf-8",
                                     "{\"ok\":false,\"error\":\"authentication required\"}");
        }
    } else {
    if (version.rfind("HTTP/", 0) != 0) {
        response = http_response(400, "Bad Request", "text/plain; charset=utf-8",
                                 "Bad Request\n");
    } else if (content_length > 8192) {
        response = http_response(413, "Payload Too Large",
                                 "text/plain; charset=utf-8",
                                 "Payload Too Large\n");
    } else if (method == "GET" && target == "/settings") {
        response = http_response(200, "OK", "text/html; charset=utf-8",
            settings_page(m_configured_board_port,
                          m_configured_dashboard_port,
                          m_boards ? m_boards->detectionIntervalMs() : 5000,
                          m_boards ? m_boards->port() : 0,
                          m_dashboard_port));
    } else if (method == "POST" && target == "/settings") {
        if (!has_control_header(request)) {
            response = http_response(403, "Forbidden", "text/html; charset=utf-8",
                settings_page(m_configured_board_port,
                              m_configured_dashboard_port,
                              m_boards ? m_boards->detectionIntervalMs() : 5000,
                              m_boards ? m_boards->port() : 0,
                              m_dashboard_port,
                              "Missing control header.", true));
        } else {
            uint32_t board_port = 0;
            uint32_t dashboard_port = 0;
            uint32_t detection_seconds = 0;
            std::string validation_error;
            if (!parse_bounded_uint(form_value(body, "board_port"),
                                    1, 65535, board_port) ||
                !parse_bounded_uint(form_value(body, "dashboard_port"),
                                    1, 65535, dashboard_port) ||
                !parse_bounded_uint(form_value(body, "detection_seconds"),
                                    1, 3600, detection_seconds)) {
                validation_error = "Ports must be 1..65535 and the detection period must be 1..3600 seconds.";
            } else if (board_port == dashboard_port) {
                validation_error = "The board and Web listener ports must be different.";
            }

            const uint32_t detection_interval_ms = detection_seconds * 1000U;
            if (!validation_error.empty()) {
                response = http_response(400, "Bad Request",
                    "text/html; charset=utf-8",
                    settings_page(
                        static_cast<uint16_t>(board_port == 0 ? m_configured_board_port : board_port),
                        static_cast<uint16_t>(dashboard_port == 0 ? m_configured_dashboard_port : dashboard_port),
                        detection_interval_ms == 0
                            ? (m_boards ? m_boards->detectionIntervalMs() : 5000)
                            : detection_interval_ms,
                        m_boards ? m_boards->port() : 0, m_dashboard_port,
                        validation_error, true));
            } else if (m_config_path.empty() ||
                       !save_runtime_settings(
                           m_config_path, static_cast<uint16_t>(board_port),
                           static_cast<uint16_t>(dashboard_port),
                           detection_interval_ms) || !m_boards ||
                       !m_boards->setDetectionIntervalMs(detection_interval_ms)) {
                response = http_response(500, "Internal Server Error",
                    "text/html; charset=utf-8",
                    settings_page(static_cast<uint16_t>(board_port),
                        static_cast<uint16_t>(dashboard_port),
                        detection_interval_ms,
                        m_boards ? m_boards->port() : 0, m_dashboard_port,
                        "Unable to save settings to config.json.", true));
            } else {
                m_configured_board_port = static_cast<uint16_t>(board_port);
                m_configured_dashboard_port =
                    static_cast<uint16_t>(dashboard_port);
                response = http_response(200, "OK", "text/html; charset=utf-8",
                    settings_page(m_configured_board_port,
                        m_configured_dashboard_port, detection_interval_ms,
                        m_boards->port(), m_dashboard_port,
                        "Settings saved. Detection is active now; restart BTCMinerControl to apply listener port changes."));
            }
        }
    } else if (method == "POST" && target == "/api/board-latency") {
        uint64_t board_id = 0;
        if (!has_control_header(request)) {
            response = http_response(403, "Forbidden",
                "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"missing control header\"}");
        } else if (!parse_board_id_query(query_string, board_id)) {
            response = http_response(400, "Bad Request",
                "application/json; charset=utf-8", "{\"ok\":false}");
        } else if (!m_boards || !m_boards->testBoardLatency(board_id)) {
            response = http_response(409, "Conflict",
                "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"board offline\"}");
        } else {
            response = http_response(200, "OK", "application/json; charset=utf-8",
                                     "{\"ok\":true}");
        }
    } else if (method == "POST" && target == "/api/board-latency-all") {
        if (!has_control_header(request)) {
            response = http_response(403, "Forbidden",
                "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"missing control header\"}");
        } else if (!m_boards) {
            response = http_response(400, "Bad Request",
                "application/json; charset=utf-8", "{\"ok\":false}");
        } else {
            const size_t started = m_boards->testAllBoardLatencies();
            if (started == 0) {
                response = http_response(409, "Conflict",
                    "application/json; charset=utf-8",
                    "{\"ok\":false,\"error\":\"no online boards\"}");
            } else {
                response = http_response(200, "OK",
                    "application/json; charset=utf-8",
                    "{\"ok\":true,\"started\":" +
                    std::to_string(started) + "}");
            }
        }
    } else if (method == "POST" && target == "/api/board-frequency") {
        uint64_t board_id = 0;
        uint16_t frequency_mhz = 0;
        if (!m_frequency_control_enabled) {
            response = http_response(403, "Forbidden",
                "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"frequency control disabled\"}");
        } else if (!has_control_header(request)) {
            response = http_response(403, "Forbidden",
                "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"missing control header\"}");
        } else if (!parse_frequency_query(query_string, board_id, frequency_mhz)) {
            response = http_response(400, "Bad Request",
                "application/json; charset=utf-8", "{\"ok\":false}");
        } else if (!m_boards ||
                   !m_boards->setBoardFrequency(board_id, frequency_mhz)) {
            response = http_response(409, "Conflict",
                "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"board offline or frequency out of range\"}");
        } else {
            response = http_response(200, "OK", "application/json; charset=utf-8",
                                     "{\"ok\":true}");
        }
    } else if (method == "POST" && target == "/api/board-power") {
        uint64_t board_id = 0;
        bool enabled = false;
        if (!has_control_header(request)) {
            response = http_response(403, "Forbidden",
                "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"missing control header\"}");
        } else if (!parse_power_query(query_string, board_id, enabled)) {
            response = http_response(400, "Bad Request",
                "application/json; charset=utf-8", "{\"ok\":false}");
        } else if (!m_boards || !m_boards->setBoardPower(board_id, enabled)) {
            response = http_response(409, "Conflict",
                "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"board offline\"}");
        } else {
            response = http_response(200, "OK", "application/json; charset=utf-8",
                                     "{\"ok\":true}");
        }
    } else if (method != "GET") {
        response = http_response(405, "Method Not Allowed", "text/plain; charset=utf-8",
                                 "Method Not Allowed\n");
    } else if (target == "/api/stats") {
        // Build JSON stats
        json j;
        {
            std::lock_guard<std::mutex> lock(m_pool_mutex);
            j["test_mode"] = m_test_mode;
            j["pool_url"] = m_pool_url;
            j["pool_management_url"] = m_pool_management_url;
        j["frequency_control_enabled"] = m_frequency_control_enabled;
            j["pool_connected"] = m_pool_connected;
            j["shares_accepted"] = m_shares_accepted;
            j["shares_rejected"] = m_shares_rejected;
            j["hashrate_total"] = m_hashrate_total;
        }
        j["board_port"] = m_boards ? m_boards->port() : 0;
        j["dashboard_port"] = m_dashboard_port;
        j["board_detection_interval_ms"] =
            m_boards ? m_boards->detectionIntervalMs() : 0;
        j["uptime_ms"] = m_started_ms == 0
            ? 0 : platform::tick_ms() - m_started_ms;

        auto boards = m_boards ? m_boards->getStats() : std::vector<BoardStats>{};
        json jboards = json::array();
        const uint64_t latency_now_us = steady_time_us();
        const uint64_t stats_tick_ms = platform::tick_ms();
        for (auto& b : boards) {
            json jb;
            jb["board_id"] = b.info.board_id;
            jb["board_id_hex"] = hex_board_id(b.info.board_id);
            jb["online"] = b.online;
            jb["asic_count"] = b.info.asic_count;
            jb["fw_version"] = b.info.firmware_version;
            jb["target_frequency_mhz"] = b.info.target_frequency_mhz;
            jb["actual_frequency_mhz"] = b.info.actual_frequency_mhz;
            jb["shares_accepted"] = b.info.shares_accepted;
            jb["shares_rejected"] = b.info.shares_rejected;
            jb["last_share_accepted_ms"] = b.last_share_accepted_ms;
            jb["last_share_accepted_age_seconds"] =
                b.last_share_accepted_ms == 0 ||
                        stats_tick_ms < b.last_share_accepted_ms
                    ? -1.0
                    : static_cast<double>(
                          stats_tick_ms - b.last_share_accepted_ms) / 1000.0;
            jb["hashrate"] = b.info.current_hashrate;
            jb["hashrate_1m"] = b.hashrate_1m;
            jb["hashrate_10m"] = b.hashrate_10m;
            jb["hashrate_updated"] = b.hashrate_updated;
            jb["jobs_sent"] = b.jobs_sent;
            jb["nonces_returned"] = b.nonces_returned;
            jb["best_diff"] = b.best_diff;
            jb["ip_addr"] = b.info.ip_addr;
            jb["tps_detected"] = b.telemetry.tpsDetected();
            jb["tmp1075_detected"] = b.telemetry.tmp1075Detected();
            jb["tps_telemetry_valid"] = b.telemetry.tpsValid();
            jb["tmp1075_telemetry_valid"] = b.telemetry.tmp1075Valid();
            jb["tps_address"] = b.telemetry.tps_address;
            jb["tmp1075_address"] = b.telemetry.tmp1075_address;
            jb["voltage_v"] = b.telemetry.vout_mv / 1000.0;
            jb["current_a"] = b.telemetry.iout_ma / 1000.0;
            jb["power_w"] = b.telemetry.power_mw / 1000.0;
            jb["temperature_c"] =
                b.telemetry.tmp1075_temperature_centi_c / 100.0;
            jb["tps_temperature_c"] =
                b.telemetry.tps_temperature_centi_c / 100.0;
            jb["tps_status_word"] = b.telemetry.tps_status_word;
            jb["power_state_valid"] = b.telemetry.powerStateValid();
            jb["power_enabled"] = b.telemetry.powerEnabled();
            jb["telemetry_updated"] = b.telemetry_updated;
            const bool latency_timeout = b.online && b.latency_pending &&
                b.latency_started_us != 0 &&
                latency_now_us >= b.latency_started_us &&
                latency_now_us - b.latency_started_us >= 2000000ULL;
            jb["latency_pending"] =
                b.online && b.latency_pending && !latency_timeout;
            jb["latency_timeout"] = latency_timeout;
            jb["latency_valid"] = b.online && b.latency_valid;
            jb["latency_ms"] = b.latency_ms;
            jb["latency_updated"] = b.latency_updated;
            jboards.push_back(jb);
        }
        j["boards"] = jboards;

        json jevents = json::array();
        if (m_boards) {
            for (const auto& event : m_boards->getEvents()) {
                json je;
                je["timestamp"] = event.timestamp;
                je["level"] = event.level;
                je["board_id"] = event.board_id;
                je["message"] = event.message;
                jevents.push_back(je);
            }
        }
        j["events"] = jevents;

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

void DashboardServer::setPoolManagementUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(m_pool_mutex);
    m_pool_management_url = url;
}

void DashboardServer::setTestMode(const std::string& mode) {
    std::lock_guard<std::mutex> lock(m_pool_mutex);
    m_test_mode = mode;
}
