# ESP_hole Product Memory

Last updated: 2026-08-16 (base project switched to s60sc/ESP32_AdBlocker; prototype order PLACED)

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
- **Ethernet: W5500 SPI module** — correct for S3 because the **ESP32-S3 has
  NO RMII Ethernet MAC** (Espressif removed EMAC on the S3; confirmed in
  arduino-esp32 issue #10977). A LAN8720 would NOT work. W5500 is a complete
  controller (MAC + PHY + hardwired TCP/IP, 8 sockets, 80MHz SPI) and is the
  exact controller this project tests on the S3.

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

Router admin: `http://192.168.29.1/platform.cgi`
- Login: user `admin` / pass stored locally (not in repo).
- Firmware `SRCMTF1_JCOW404_R3.16`; LAN `192.168.29.1`, DHCP on
  (192.168.29.2–.254, 24h). WAN IP at check time `10.85.8.242` (DHCP).
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
- [x] Hardware decided & **ordered**: ESP32-S3-N16R8 + W5500 + breadboard kit
      (₹1,100, order placed)
- [x] Cloned s60sc/ESP32_AdBlocker; verified build config for ESP32-S3 N16R8
      (see "Build config (verified)" section) — actual compile not yet run
- [ ] Install arduino-cli + ESP32 core 3.1.1 and compile-verify the sketch
- [ ] When kit arrives: wire W5500 → S3 per pin map; configure pins in
      Edit Config → Ethernet
- [ ] Validate on Jio Centrum: set router DNS `Use Below` = ESP static IP
- [ ] Confirm DNS binding works over W5500 (AsyncUDP on port 53)
- [ ] Sketch firmware partition/flash/build changes (OTA + blocklist coexist)
- [ ] Define backend (signed firmware + blocklist manifests)
- [ ] Productize: status LED, reset button, enclosure, PSU