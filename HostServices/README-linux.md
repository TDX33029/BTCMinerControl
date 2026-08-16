# BTCMinerControl on Ubuntu

The same C++ mining proxy that previously ran on Windows now builds natively on
Ubuntu. It keeps the embedded Web UI (`/` and `/settings`), the board TCP
listener, and the Stratum V1 client. No third-party runtime dependencies are
required beyond the C++ standard library and POSIX sockets.

## Quick start

```bash
cd HostServices
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run self-tests (no hardware needed)
./build/btcminercontrol --self-test

# Edit the example config
cp BTCMinerControl/config.json my-config.json
nano my-config.json

# Run in the foreground
./build/btcminercontrol my-config.json
```

Open `http://<server-ip>:8080/` for the dashboard. The default dashboard bind
is `127.0.0.1`; set `dashboard_bind` to `0.0.0.0` only after considering the
LAN exposure.

## Install with systemd

```bash
sudo apt update
sudo apt install -y build-essential cmake

cd HostServices
./scripts/install_ubuntu.sh build /usr/local /etc/btcminercontrol

sudo cp packaging/btcminercontrol.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now btcminercontrol
```

Edit `/etc/btcminercontrol/config.json` before enabling the service:

```json
{
  "board_port": 4200,
  "dashboard_port": 8080,
  "dashboard_bind": "0.0.0.0",
  "pool": {
    "host": "stratum.braiins.com",
    "port": 3333,
    "user": "username.worker",
    "pass": "x"
  }
}
```

Useful service commands:

```bash
journalctl -u btcminercontrol -f
systemctl restart btcminercontrol
```

## Debug builds and verbose logs

Pool/share diagnostics were intentionally quieted after the byte-order fix.
To build a chatty binary:

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug \
      -DVERIFY_DIAG=ON -DSCHEDULER_DIAG=ON
cmake --build build-debug -j$(nproc)
```

The F107 firmware has its own `BM1366_REG_DIAG` and `BM1366_LOG_RESULTS`
switches in `BTCMinerMS/Core/Src/main.c`.

## Point the STM32 boards at this server

The F107 firmware does not use DNS for the host address yet. In
`BTCMinerMS/Core/Src/main.c` set the Ubuntu server IPv4 address and rebuild
the firmware:

```c
#define PC_IP0 10   // first octet
#define PC_IP1 8    // ...
#define PC_IP2 1
#define PC_IP3 3
#define PC_PORT 4200
```

`PC_PORT` must match `board_port` in `config.json` (default 4200).

## Ports and firewall

| Port | Purpose |
|---|---|
| `board_port` (4200) | STM32 boards connect to this host |
| `dashboard_port` (8080) | Web UI |

If `dashboard_bind` is `0.0.0.0`, allow the dashboard port in the firewall only
for trusted LAN clients:

```bash
sudo ufw allow from 10.8.1.0/24 to any port 8080 proto tcp
```
