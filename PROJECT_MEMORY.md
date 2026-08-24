# ESP_hole Product Memory

Last updated: 2026-08-24 (ESP32-S3 connected & working; ENC28J60 shipped by mistake instead of W5500)

## What this project is

Building a consumer ad-blocking DNS device for **non-technical end users**,
"install once and forget it". Base project is now
**s60sc/ESP32_AdBlocker** (https://github.com/s60sc/ESP32_AdBlocker) — chosen
because it is actively maintained (pushed 2026-07, 373 stars, not archived)
and targets the **latest ESP32-S3**.

## Base project facts (s60sc/ESP32_AdBlocker)

- **DNS sinkhole (Pi-hole style)**: returns `0.0.0.0` for domains in the
  blocklist, otherwise forwards to an external DNS server to resolve.
- **Requirements**: needs **PSRAM**. ESP32-S3 with 8MB PSRAM hosts the current
  blocklist; checks take **<50 µs** (ESP32 w/ 4MB PSRAM may truncate, <100 µs).
- **Arduino sketch**, core **min v3.1.1**, **PSRAM enabled**, partition scheme:
  - ESP32-S3: `8M with spiffs (...)`  ← user's board is 16MB flash (N16R8),
    so confirm the right 16MB partition option at build time.
  - ESP32: `Minimal SPIFFS (...)`
- **First boot** = WiFi AP mode, SSID `ESP32_AdBlocker_...`, configure router
  SSID/password via web page at `192.168.4.1`. Config saved (passwords excepted);
  web pages auto-downloaded from GitHub to `/data` when online.
- **Web UI** (control + monitor): Allowed/Blocked domain counters, enter new
  blocklist URL + Reload, Add/Del/Check a domain, StopLoad, Clear custom
  blocklist, Enable/Disable ad blocking, **OTA Upload** tab, Edit Config,
  Show Log (with Verbose mode).
- **Blocklist**: downloads one consolidated file (HOSTS or Adblock format,
  e.g. StevenBlack/hosts — https://github.com/StevenBlack/hosts). Keep file
  smaller than PSRAM. Auto-reloads daily at a set time. Custom add/remove
  entries stored locally.
- **Ethernet support**: W5500 controller **tested on ESP32-S3** (our combo!).
  Configurable SPI pins. Network modes: `WiFi` / `Ethernet` / `Eth+AP`
  (Ethernet + ESP Access Point).
- **DNS implementation**: `AsyncUDP` listening on port 53
  (`externalDNS.cpp`). Configurable DNS servers (`ST_ns1`, `ST_ns2`).
- **Static IP** supported (`ST_ip`) — recommended so clients always find it.

### List management design (decided 2026-08-24)
- **Blacklist = creator-managed, remote**: we host ONE consolidated HOSTS-format
  file at a stable URL (our own GitHub repo / release asset; base =
  StevenBlack/hosts + our additions). Device's blocklist URL is set once in the
  dashboard → auto-downloads **daily** at set time. We push updates by
  committing upstream; devices converge within 24h (or manual Reload).
- **Whitelist = end-user managed, from dashboard**: use built-in DelDomain —
  deletions persist to `/data/custom` as `#domain` lines and are re-applied
  after EVERY blocklist reload (`loadCustom()` appSpecific.cpp:251), so user
  unblocks survive daily blacklist refreshes. AddDomain works inversely.
  Optional later: a Whitelist tab listing `/data/custom` contents for visibility.

## The core product design problem

The ESP is just a DNS server — devices must be told to use it. Non-technical
users won't configure each device. So enforcement (how the whole network ends
up using the ESP) is the key product decision.

## SELECTED PLAN (recommendation + user's actual choice)

### Enforcement model: point DNS at the ESP (router-level, one-time)
AdBlocker has **no DHCP server** — you make it the network's DNS by setting the
router's DNS to the ESP's static IP. On the **Jio Centrum** router this is the
`DNSServers → Use Below` setting (Primary DNS = ESP IP). Clients keep using the
router as DNS proxy, which forwards to the ESP → network-wide blocking with
**one setup step at the router** (installer-assisted, ~2 min).
(Not DHCP takeover — that plan is dropped since AdBlocker has no DHCP server.)

### Hardware decision: ESP32-S3-N16R8 + W5500 (SPI Ethernet)
- **MCU: ESP32-S3 DevKit-N16R8** — 16MB flash + **8MB PSRAM**, dual-core LX7,
  native USB-C. 8MB PSRAM = the recommended config for the current blocklist.
  **STATUS: board received, connected, working (2026-08-24).**
- Ethernet: W5500 SPI module — correct for S3 because the **ESP32-S3 has
  NO RMII Ethernet MAC**. A LAN8720 would NOT work.
- ⚠️ **Seller shipped ENC28J60 by mistake instead of W5500** (2026-08-24).
  ❌ **ENC28J60 is UNUSABLE with this project**: ESP-IDF & arduino-esp32
  (verified core 3.3.11) support only DM9051 / W5500 / KSZ8851SNL SPI ethernet
  — no ENC28J60 driver exists at all. Patch attempt reverted; `utils.cpp` keeps
  `#define ETH_SPI_PHY_TYPE ETH_PHY_W5500`.
  - Action: request W5500 replacement from seller / reorder (~₹309).
  - Meanwhile: run on **WiFi** (`netMode` default) — fully functional for DNS.
  - Core note: in 3.3.11 the ETH API lives in `libraries/Ethernet/src/ETH.h`;
    `ETH_PHY_SPI_FREQ_MHZ = 20`; FQBN options verified:
    `FlashSize=16M,PSRAM=opi,PartitionScheme=custom` (+ our partitions.csv).

### Prototype order PLACED (₹1,100 total)
| Component                             | Qty | Price |
| ------------------------------------- | --: | ----: |
| ESP32-S3 DevKit-N16R8                 |   1 |  ₹599 |
| W5500 SPI Ethernet Network Module     |   1 |  ₹309 |
| 400-point breadboard                  |   1 |   ₹30 |
| 6×6×5 tactile buttons, 10-pack        |   1 |   ₹17 |
| 220 Ω ¼-W resistors, 100-pack         |   1 |   ₹39 |
| M-M + M-F + F-F jumper wires, 20 each |   1 |  ₹106 |
| **Total**                             |     | **₹1,100** |

### W5500 ↔ ESP32-S3 wiring (SPI)
This project configures pins in `Edit Config → Ethernet`:
`ethCS, ethInt, ethRst, ethSclk, ethMiso, ethMosi` (all -1 = not used).
Reference pin map (mischianti Core 3 guide):
| ESP32-S3 | W5500 |
| -------- | ----- |
| GPIO13   | MISO  |
| GPIO12   | SCLK  |
| GPIO11   | MOSI  |
| GPIO10   | CS    |
| GPIO9 (optional) | INT |
| GPIO3 (optional) | RST |
| 3V3      | 3V3   |
| GND      | GND   |

### Production path (after prototype validation)
- Design a **custom PCB** around a bare **ESP32-S3-WROOM-1-N16R8** module
  (~₹350–500 bulk) + **W5500** (bare IC ~₹80 or W5500-lite module) + RJ45
  w/ magnetics + PSU. India PCB assembly (LionCircuits, E-Bitz, PCBLayer) or
  JLCPCB.
- Estimated unit cost at 1,000 units: **~₹600–800/unit**.
- Bulk-buy modules from DigiKey/Mouser India or LCSC for genuine silicon.

### "Forget it" product requirements (target architecture)
- **OTA already built in** (OTA Upload tab) — ship signed updates from backend.
- Backend = dumb static-file service (S3 + signed manifests); device pulls
  firmware + blocklist updates (blocklist already auto-reloads daily).
- mDNS hostname, status LED, reset button; web dashboard already has the
  allowlist ("Add/Del domain") — kills the top support ticket.
- Note for support docs: browsers must have **Secure DNS disabled**, and IPv6
  DNS should be disabled on router/devices (AdBlocker is IPv4-only).

## Jio Centrum Home Gateway check (JCOW404, JioFiber) — for the setup step

**STATUS 2026-08-24: router DNS switch NOT yet working — deep dive in progress**
- ESP side verified good: `dig @192.168.29.191 doubleclick.net` → `0.0.0.0` ✓,
  `github.com` resolves ✓.
- Manual "Use Below" attempt + router reboot did NOT take effect:
  `dig @192.168.29.1 doubleclick.net` still returns real IPs → router not
  proxying to ESP. Either Save silently failed or reboot reverted it.
- Router reboots kill the admin web session → fresh login required each time.
- Login page (TeamF1 UI): fields `users.username` / `users.password`
  (ids `tf1_userName` / `tf1_password`), form POST to platform.cgi. Admin user
  is `admin`; password known to user + agent session only — NOT stored here.
- NEXT STEPS when resuming: complete login, open
  `platform.cgi?page=lanIPv4Config.html`, dump the form's real submit JS
  (Save button handler + POST fields), apply DNSServers=`Use Below`,
  Primary=`192.168.29.191`, Secondary empty; verify immediately with
  `dig @192.168.29.1 doubleclick.net` → expect `0.0.0.0`. If UI keeps
  reverting, fall back to per-device DNS or DHCP takeover redesign.

Router admin: `http://192.168.29.1/platform.cgi`
- Firmware `SRCMTF1_JCOW404_R3.16`; LAN `192.168.29.1`, DHCP on
  (192.168.29.2–.254, 24h).
- NOTE: router pages never fire a full `load` event → browser "wait for load"
  / click timeout quirks. Use `gotoLinks('page.html')` via JS or eval, then
  wait ~2–3s. Page URL pattern: `platform.cgi?page=<page>.html`.
- **Settings exist** under `NETWORK → LAN → LAN IPv4 Configuration`
  (`lanIPv4Config.html`):
  1. **Change DNS — YES**: `DNSServers` select: `Use DNS Proxy` (current,
      hands out router itself + proxies) / `Use DNS from ISP` / `Use Below`
      (enables Primary + Secondary DNS fields).
  2. **Disable DHCP — YES** (`dhcpMode`: `DHCP Server` / `None`) — NOT needed
      for the current plan (AdBlocker has no DHCP server).
- For this project the setup is: set `DNSServers → Use Below` with **Primary
  DNS = ESP static IP** (device stays on router's network; router proxies DNS
  to the ESP → whole network filtered).

## Build config (verified, arduino-esp32 core 3.1.1)

**Board: ESP32S3 Dev Module** (generic `esp32s3`)

| Setting | Value |
| --- | --- |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | **Custom** — see CSV below |
| PSRAM | Enabled (required: `NEED_PSRAM=true`, `MIN_PSRAM=4`; N16R8 has 8MB) |
| Core | min v3.1.1 |

Key facts:
- Blocklist is held in **PSRAM**, not flash (`appSpecific.cpp:187` streams it
  into memory). Flash only needs OTA app slots + small `/data` (web pages,
  config, log, custom blocklist).
- The generic `esp32s3` board in core 3.1.1 exposes **no ready-made 16MB+spiffs
  partition** — the 16M menu items are FATFS (`16M Flash 2MB APP/12.5MB FATFS`,
  `3MB APP/9.9MB FATFS`) or `esp_sr_16` (spiffs but 3MB app slots + 2.9MB model
  partition). Use the **Custom** scheme instead.
- Sketch uses **LittleFS** as `STORAGE` (appGlobals.h), which mounts a `spiffs`
  partition fine.
- `default_16MB` exists as a named scheme only on third-party S3 boards
  (adafruit_metro_esp32s3 etc.), not the generic S3 board.

Custom `partitions.csv` (modeled on `default_16MB.csv`):
```
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x640000,
app1,     app,  ota_1,   0x650000,0x640000,
spiffs,   data, spiffs,  0xc90000,0x360000,
coredump, data, coredump,0xFF0000,0x10000,
```
(6.25MB app ×2 OTA + 3.43MB spiffs)

## Status / next steps
- [x] Base project decided: s60sc/ESP32_AdBlocker (active, tested on
      ESP32-S3 + W5500 — matches planned hardware)
- [x] Enforcement model decided: router DNS → ESP static IP (Use Below)
- [x] Hardware ordered: ESP32-S3-N16R8 + breadboard kit (₹1,100)
- [x] ESP32-S3 received & connected — working (2026-08-24)
- [x] arduino-cli 1.5.1 + esp32 core 3.3.11 installed; partitions.csv created
- [x] **DEPLOYED on WiFi (2026-08-24)**: flashed v3.3, joined HomeAlone5 at
      **192.168.29.191** (ESP32_AdBlocker.local), default StevenBlack blocklist,
      daily reload 04:00, web UI verified from LAN. WiFi creds were pushed
      programmatically via `/control?ST_SSID=..&ST_Pass=..&save=1&reset=1`
      (responses are empty 200s by design). NOTE: full blocklist re-downloads +
      reprocesses (~4 min) after every boot.
- [ ] ENC28J60 unusable (no driver in ESP-IDF/arduino): get W5500
      replacement/reorder; run WiFi-only until then
- [ ] Set Jio router DNS `Use Below` = 192.168.29.191 → network-wide blocking;
      give device a DHCP reservation for that IP
- [ ] Host creator-managed blacklist file (GitHub) + set device blocklist URL
- [ ] Host creator-managed blacklist file (GitHub) + set device blocklist URL
- [ ] Validate whitelist flow: DelDomain from dashboard survives daily reload
- [ ] Install arduino-cli / Arduino IDE + ESP32 core 3.1.1 and compile-verify
- [ ] Validate on Jio Centrum: router DNS `Use Below` = ESP static IP
- [ ] Define backend (signed firmware + blocklist manifests)
- [ ] Productize: status LED, reset button, enclosure, PSU