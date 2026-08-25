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

## Dashboard access control

The dashboard now requires one login before use.

### Recommended: salted SHA-256 password in config

Generate the config values once on the Ubuntu host:

```bash
./build/btcminercontrol --hash-password 'your-strong-password'
```

Copy the two printed fields into `config.json` and leave the plaintext field
empty:

```json
{
  "dashboard_password": "",
  "dashboard_password_sha256": "f07a...8890",
  "dashboard_password_salt": "8ac0...586f"
}
```

### Legacy: plaintext password

```json
{
  "dashboard_password": "change-me"
}
```

Both modes support the same behavior:

- `GET /` shows a login page until a valid session cookie exists.
- Login posts the password once to `/api/login`; the server sets an
  `HttpOnly; SameSite=Strict` session cookie valid for 24 hours.
- Subsequent page loads and POST actions use that cookie automatically.
- Five failed login attempts from one IP lock that IP for 15 minutes.
- If neither password mode is configured, login is disabled and the UI is
  read-only.

There is no username. Only the web login password needs protection; pool
credentials remain separate in the `pool` section.

## ASIC frequency

Frequency adjustment is temporarily controlled by:

```json
{
  "frequency_control_enabled": false
}
```

- `false`: the Freq column is read-only, the board-connect
  `SetParams` frequency command is not sent, and
  `POST /api/board-frequency` returns `403`.
- `true`: the inline number box and `SET` button are shown.


The Web UI shows each board's target and actual ASIC frequency in the
`Freq (MHz)` column. Enter a value from 0 to 600 MHz in the number box and click `SET`. The host
sends `0x0C SetFrequency`; the F107 writes the new PLL divider, re-applies the
BM1366 hash-counting/nonce-space operating point and immediately re-sends the
current job — no full chip reset is required.

## Share age display

Each board row shows `Last Share` as `xxh xxm xxs`, measured from the last
share accepted by the pool. The value is derived from the same monotonic clock
used by board timeouts, so it remains stable across dashboard refreshes.

## Hashrate display

The board samples the BM1366 total hash counter (register `0x8C`) every five
seconds and sends the measured MH/s value to the host. The dashboard no longer
estimates hashrate from nonce-return rate, which was inflated by the old
`2^32`-per-nonce assumption.

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
