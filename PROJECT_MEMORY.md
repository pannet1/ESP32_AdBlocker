# ESP_hole Product Memory

Last updated: 2026-08-24 (ESP32-S3 connected & working; ENC28J60 shipped by mistake instead of W5500)

## CURRENT SHORT-TERM GOAL (set 2026-08-24)
Get **stock `s60sc/ESP32_AdBlocker` working on ESP32-S3 exactly as the original
author envisioned** — a WiFi (or Ethernet) DNS sinkhole + web dashboard +
loaded blocklist, devices pointed at it as DNS. **No product deviations yet.**
The router-agnostic Eth+AP model, `ecomsense.in` backend, and W5500/Ethernet
specific work below are **deferred to "later"** — revisit only after the stock
baseline is confirmed solid.

## ✅ INCIDENT RESOLVED — device DNS DOWN (2026-08-24 ~16:30 IST → fixed 2026-08-25 20:35 UTC / 2026-08-26 ~02:05 IST)
During a blocklist-reload verification I triggered a `/control?zLoad=...` controlled
restart; the blocklist fetch from GitHub failed. Stock firmware only calls `prepDNS()`
*after* a successful `loadBlockList()` (`appSpecific.cpp:336`:
`if (loadBlockList("Initial")) prepDNS();`), so the DNS server never started →
**port 53 refused**. Self-inflicted during verification.

### Root cause — three compounded, environment-driven failures
1. **Stale bundled CA cert.** `git_rootCACertificate` was the COMODO "AAA" root, but
   `raw.githubusercontent.com` now serves a **Let's Encrypt** cert chained to
   **ISRG Root X1** (verified from this LAN: `openssl verify` against ISRG X1 = OK).
2. **NTP never syncs** on this network → system clock stuck at boot → *every*
   certificate-validated TLS connection fails. NTP UDP/123 is reachable from the LAN,
   so it's the ESP's own DNS/client for `pool.ntp.org` (DHCP DNS) that fails.
3. **Device's own outbound DNS resolution fails (the real blocker).** In DHCP WiFi
   mode `startWifi()` never calls `WiFi.setDNS()`, so the ESP inherits the **Jio
   router's DHCP DNS**, which does NOT resolve `raw.githubusercontent.com` (the router's
   *own* resolver does — `dig @192.168.29.1` works — but the ISP DNS it hands the ESP
   doesn't). The blocklist fetch (`remoteServerConnect` → `client.connect(host,port)`)
   uses the station DNS, NOT the configured `ST_ns1`/`ST_ns2` (1.1.1.1/8.8.8.8) the
   device already uses to forward *client* queries. Result: `TSL connect Fail …
   Err -1: Generic error` — the code comment confirms "Generic error can indicate DNS
   failure". Fixes #1/#2 (cert/clock) removed the first walls but this one remained.

### Fixes applied (stock-compatible; GitHub retained as source — NO ecomsense yet)
- `certificates.cpp`: replaced COMODO root with **ISRG Root X1** (valid to 2035).
- `appSpecific.cpp:171` + `setupAssist.cpp:30`: force `setInsecure()` (empty cert) for
  the public GitHub fetches → blocklist loads regardless of cert/clock.
- `utils.cpp` `startWifi()`: after the station connects, force
  `WiFi.setDNS(ST_ns1, ST_ns2)` for the device's OWN outbound (blocklist, NTP,
  external-IP). Matches the static-IP path (`WiFi.STA.config(...,_ns1)`) and the
  client-DNS forwarder. **This also fixed NTP** (it needed DNS to resolve
  pool.ntp.org). Ethernet already did this via `ETH.config(...,_ns1,_ns2)`.
- `globals.h`: reverted the earlier (deferred) `ECOMSENSE_BLOCKLIST_URL` experiment.

### Build note (why the first build failed: "Sketch too big")
The sketch is ~1.5 MB, exceeding the **default 4 MB partition's 1.25 MB app limit**.
Build with `PartitionScheme=default_8MB` (3 MB app). The repo's `partitions.csv`
(16 MB table) is flashed automatically by the core's prebuild hook.
FQBN: `esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=default_8MB`

### Verification (2026-08-25 20:35 UTC, device on NEW firmware)
- `dig @192.168.29.191 doubleclick.net` → `0.0.0.0` (ad blocked) ✅
- `dig @192.168.29.191 google.com` → real IP (forwarded) ✅
- Log: `Loaded 58641 blocked domains excluding 23920 duplicates, using 1.1MB of 7.0MB`
  — **no `Blocklist truncated` / `memory overflow`** ✅ (full StevenBlack list, not truncated)
- `AdBlocker DNS Server started on 192.168.29.191:53` ✅
- NTP synced — clock now shows real time (`20:35`), not uptime ✅

### Cold-start survival test (2026-08-25 ~21:20 UTC) — PASSED ✅
Full power-loss (unplug/replug) recovery verified. Boot log (nulls stripped):
- `Wakeup by reset` — fresh boot; `up_time 0-00:09:12` at 21:29 → ~9 min since cold boot
- `Station DNS set to 1.1.1.1` (fix active from zero state) ✅
- `Initial load of latest blocklist` → `Loaded 58641 blocked domains excluding 23920
  duplicates, using 1.1MB of 7.0MB` ✅ (no truncation, no cache)
- `AdBlocker DNS Server started on 192.168.29.191:53` ✅
- `dig @192.168.29.191 doubleclick.net` → `0.0.0.0` ✅; `loadProg: Complete`; NTP synced
  (clock 21:29 UTC)
- Conclusion: firmware (flash) + settings (SPIFFS) persist across TOTAL power loss; both
  fixes (`setInsecure` + `WiFi.setDNS`) work from epoch clock / no cache; full blocklist
  loads autonomously and DNS sinkhole comes up. Device is genuinely "install and forget".
- NOTE: `cold_start_check.sh` is designed to be STARTED FIRST, then unplug — it witnesses
  the power-down transition. If run after the unplug it aborts ("device never went down").
  For a post-hoc check, query `/status` + `/control?displayLog=1` directly (strip NULs:
  `curl .../control?displayLog=1 | tr -d '\0'`).

> Status: **RESOLVED.** Stock s60sc firmware now runs on this network; full blocklist
> loads and DNS sinkhole is live. Task 1 (verify full blocklist not truncated) = done.

### Blocklist resilience: SPIFFS cache + fail-safe DNS (implemented 2026-08-25)
Addresses the product risk that a failed blocklist download / no-internet boot left the
DNS server DOWN (fail-closed) — unacceptable for an "install and forget" device.
- **Cache:** after every successful download, the parsed blocklist is saved to LittleFS
  (`/data/blcache`, magic `0x424C3031`; header + ptrs + storage). Custom list stays
  separate in `/data/custom` and is re-applied on top each boot.
- **Boot:** load from cache FIRST (instant DNS + blocking, offline-capable) → **always
  `prepDNS()`** (fail-safe: forwarding-only if no cache) → download ONLY if no cache
  (first run) or on the daily scheduled update. No download on every boot.
- **Recovery:** `doAppPing` refreshes the cache on the daily update, and retries the
  download (throttled 60s) whenever connectivity returns if no list is cached.
- Net effect: a transient GitHub/ISP outage at boot can NEVER take down client DNS;
  blocking is active from the first second via cache, or clients keep full internet
  (forwarding) until the list arrives.
- Files: `appSpecific.cpp` (`saveCachedBlockList`, `loadCachedBlockList`, restructured
  `appSetup`, `doAppPing`); `appGlobals.h` (`BLOCKLIST_CACHE_PATH`). Build:
  `PartitionScheme=default_8MB`. Binary: `build_out/ESP32_AdBlocker.ino.bin`.
- Verify (after flash, 2026-08-25): (1) ✅ boot w/ internet → "Blocklist cached to
  /data/blcache.txt (58641 domains, 1.1MB)", loadProg Complete; (2) ✅ 2nd cold boot
  → "Blocklist restored from cache /data/blcache.txt (58641 domains, 1.1MB)" at
  boot+6.7s (vs ~170s GitHub download), loadProg Cached, DNS up immediately,
  `dig doubleclick.net`→`0.0.0.0`; (3) reboot w/ WAN down (cache exists) → still
  blocks (optional, not yet run). Edge: first boot no internet + no cache →
  forwarding-only + 60s retry until connectivity returns, then caches.

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
  file at a stable URL on **ecomsense.in** (our own domain, Cloudflare-fronted,
  104.21.x = Cloudflare, valid HTTPS — ESP can fetch over trusted cert). Base =
  StevenBlack/hosts + our additions. Device's blocklist URL is set once in the
  dashboard → auto-downloads **daily** at set time. We push updates by
  re-uploading (Cloudflare Pages / R2 / origin); devices converge within 24h
  (or manual Reload). GitHub is only a fallback, not the primary host.
  - Suggested URL: `https://ecomsense.in/adblock/hosts.txt` (or a subdomain
    like `adblock.ecomsense.in/hosts`). Publish via Cloudflare Pages (git-backed,
    version-controlled) is the clean long-term path; user has Cloudflare dashboard.
  - ecomsense.in can also later serve the **update backend** (signed OTA firmware
    + manifest) from the plan below.
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

### Product architecture (decided 2026-08-24): ESP as its own WiFi AP + DNS sinkhole — **router/ISP agnostic**
Hard constraint: the solution MUST be router/ISP agnostic and near plug-and-play
(silently works in background; advanced users get logs / unblock via the existing
web dashboard). Therefore we **cannot rely on the user's router/ISP to point DNS
at the ESP** — that path is router-dependent, flaky (failed repeatedly on the
Jio Centrum even when set), and non-portable.

Instead the ESP **is** the network edge devices opt into:
- **Eth+AP mode** (base project supported): W5500 Ethernet = internet uplink to
the router; ESP also broadcasts its own WiFi AP. `WiFi.AP.enableNAPT(true)`
(`utils.cpp`) gives connected clients internet; the DNS sinkhole (port 53,
`externalDNS.cpp`) serves them.
- Clients join the **ESP_hole WiFi** → ESP's AP DHCP hands out DNS = ESP IP →
automatic blocking. **Zero router/DNS configuration.** The router only provides
upstream internet over Ethernet.
- Genuinely router/ISP agnostic: the router just sees an Ethernet client; no DNS,
DHCP, or bridge changes on the user's network.
- **Ethernet (W5500) is the recommended production uplink *specifically* to avoid
asking the customer for their home WiFi username/password.** With Ethernet, the
ESP gets internet via DHCP from the router — **no home WiFi creds are ever typed
into the AdBlocker UI** (privacy + UX win). The customer only joins the *ESP_hole*
AP (our own network; password printed on the box), never their home WiFi.
- **WiFi-station uplink is an optional fallback only** (homes where running an
Ethernet cable is impractical). It requires the customer to enter home WiFi
creds, so it is NOT the default path.
- Tradeoff (price of agnosticism + plug-and-play): customer runs one Ethernet
cable router→device (or, fallback, enters home WiFi creds); and devices connect
to the ESP_hole WiFi SSID instead of the home router WiFi — a one-time "join
this network" step per device.
- "Interested user" features (logs, unblock/Allow, counters) already exist in the
web dashboard served on the AP.
- The earlier **router-DNS approach (Jio `Use Below`) is now a dead-end for the
product** — kept only as diagnostic history (it never stuck; Jio returned real
IPs). The per-device-DNS test on this machine proved the sinkhole core works;
the remaining engineering is making the ESP the DHCP/DNS provider for its own AP
subnet (Eth+AP), validated on hardware once W5500 arrives.
> DEFERRED — see CURRENT SHORT-TERM GOAL above; baseline stock function first.

### KNOWN GAP (pre-W5500 code audit, 2026-08-24): AP DHCP must advertise the ESP as DNS
**Critical for the agnostic model — without this, `ESP_hole` clients can bypass
 the sinkhole and tracking is NOT blocked.**
- The DNS sinkhole binds `udp.listen(53)` on **all interfaces** (`externalDNS.cpp`
  `prepDNS()`), so AP clients querying the ESP IP ARE answered — good.
- BUT the softAP **DHCP server sends NO DNS option** to clients by default:
  - ESP-IDF `dhcpserver.c`: `dhcps_dns` defaults to `0x00` (DNS option disabled);
    the `#ifdef CONFIG_LWIP_DHCPS_ADD_DNS` fallback (which would send the AP IP)
    is **NOT enabled** in arduino-esp32 core 3.3.11 (verified: no `LWIP_DHCPS_ADD_DNS`
    in sdkconfig / sdkconfig.defaults).
  - `setWifiAP()` (`utils.cpp:188`) calls `WiFi.AP.config(_ip, _gw, _sn)` with **no
    DNS argument**, so clients get no DNS server in their lease.
  - Result: OS-dependent. Many clients fall back to the AP gateway (the ESP →
    works), but others (some Linux/Android/iOS) ignore that and use a public
    resolver (8.8.8.8 etc.) → **sinkhole bypassed → no blocking**.
- **FIX (apply + verify in AP / Eth+AP mode on W5500 arrival):** explicitly force
  the AP DHCP DNS option to the ESP's AP IP. Simplest via the arduino wrapper that
  already carries a `dns` param (`WiFiAPClass::softAPConfig(ip,gw,sn,lease,dns)`):
  ```cpp
  // in setWifiAP(), replace WiFi.AP.config(_ip,_gw,_sn) with:
  IPAddress _ip, _gw, _sn;
  if (strlen(AP_ip) > 1) { _ip.fromString(AP_ip); _gw.fromString(AP_gw); _sn.fromString(AP_sn); }
  else { _ip = IPAddress(192,168,4,1); _gw = IPAddress(192,168,4,1); _sn = IPAddress(255,255,255,0); }
  WiFi.softAPConfig(_ip, _gw, _sn, IPAddress(), _ip); // dns = ESP AP IP (esp. 192.168.4.1)
  ```
  (The `dns` arg flows to `dhcps_dns` → DHCP OFFER includes DNS_SERVER = AP IP.)
  Bulletproof alternative if the wrapper misbehaves: stop dhcps, call
  `esp_netif_dhcps_option(apNetif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
  &dnsInfo, sizeof(dnsInfo))` with `dnsInfo.ip.u_addr.ip4.addr = AP_IP`, restart dhcps.
- **Test:** join `ESP_hole` from a phone/laptop, `dig doubleclick.net` → must be
  `0.0.0.0`; `dig @<gateway> doubleclick.net` and a direct public resolver must NOT
  be what the client uses. Also confirm no IPv6 DNS leak (ESP is IPv4-only).

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
- [x] **WiFi-only unit re-validated working (2026-08-24)**: DNS blocks
      `doubleclick.net → 0.0.0.0`, resolves github/google; dashboard live at
      192.168.29.191 with full controls. Usable NOW as a stopgap via manual
      per-device DNS → 192.168.29.191 (no router/W5500 needed). This machine's
      `/etc/resolv.conf` already points here and blocks. (WiFi-only needs user
      intervention = set device DNS; that's the known limitation vs the Eth+AP
      product model.)
- [ ] ENC28J60 unusable (no driver): get W5500 replacement/reorder; run WiFi-only until then
- [x] **Architecture pivot**: product is router/ISP-agnostic → ESP as own WiFi AP
      + DNS sinkhole (Eth+AP). Router-DNS (`Use Below`) approach dropped (failed
      on Jio, non-portable). Verified in code: `WiFi.AP.enableNAPT` + port-53 sinkhole.
- [ ] Validate Eth+AP on hardware once W5500 arrives: phone/laptop joins ESP_hole
      WiFi → blocking + internet (NAPT) with **no** router DNS config
- [ ] Host creator-managed blocklist on **ecomsense.in** (Cloudflare) + set device
      blocklist URL to it (e.g. `https://ecomsense.in/adblock/hosts.txt`)
- [ ] Validate whitelist flow: DelDomain from dashboard survives daily reload
- [ ] Define backend (signed firmware + blocklist manifests)
- [ ] Productize: status LED, reset button, enclosure, PSU, label "join ESP_hole WiFi"
## Web UI (dashboard) — reskin + curated dashboard (2026-08-26)
- Files: `data/AdBlocker.htm` (markup + modern CSS design system) and `data/common.js`
  (`buildTable` `case 'A'` now emits real `<button class="action-btn">` instead of SVG
  `<rect>`+`<text>` fake buttons; framework's delegated click handler already supports
  `BUTTON`, so ids unchanged → bindings intact).
- Curated home: header w/ "Ad blocking active" pill, hero "Your network is protected",
  stat cards (uptime/source/mode/IP/extIP/fw/wifi/mem) fed by existing `/status` keys
  via a small inline `dash()` polling `/status` every 4s. Quick Block/Check actions call
  `sendControl('uLoad'|'wLoad', domain)` (server `checkDomain` handles these as domains).
- NO firmware change needed — pure front-end. Changes persist in LittleFS (survive reboot).
- **Apply path (non-obvious):** `/upload` endpoint writes non-.bin files to LittleFS ROOT
  and strips the path, so it CANNOT replace `/data/*.htm`/`.js` (served from `/data`).
  Use **WebDAV PUT** instead (INCLUDE_WEBDAV=true, base `/webdav`):
  `curl -T data/AdBlocker.htm http://<ip>/webdav/data/AdBlocker.htm` (same for common.js).
  Verify with `curl http://<ip>/web?AdBlocker.htm | grep "Network protected"`.
  Browser may cache old files → user must HARD-REFRESH (Ctrl/Cmd+Shift+R).
- Caveat: auto-table "Add Domain" button (framework `processStatus`→`getLoadURL`) sends the
  blocklist *URL*, not a domain — pre-existing framework quirk, not changed in P0.
- ⚠️ **CRITICAL — web file caching (root cause of all "UI fix didn't show up" reports):**
  `fileHandler` in `webServer.cpp` set a STATIC `ETag` (`CFG_VER`) and returned `304 Not
  Modified` whenever the browser's cached ETag matched. Since `CFG_VER` never changes on a
  WebDAV upload, the browser cached `AdBlocker.htm`/`common.js` PERMANENTLY — even a hard
  refresh got 304, so NO UI edit (WebDAV PUT) was ever visible until the site cache was
  cleared. `divShowData is null` when clicking Ethernet is a symptom: the cached page lacked
  `#Main0123`. **FIX:** removed the `If-None-Match`/304/`ETag` logic in `fileHandler` and set
  `Cache-Control: no-store, max-age=0` (webServer.cpp). After flashing this, WebDAV edits are
  served instantly (no cache, no hard-refresh needed). Immediate test before flashing: open
  the dashboard in a private/incognito window (it has no cached ETag) → current served file
  already has the fix.
- **Network tab (2026-08-26):** firmware exposes exactly two network config groups — group `0`
  = Wireless (WiFi station `ST_*` + AP `AP_*` + `hostName` + `allowAP`) and group `0123` =
  Ethernet pins + `netMode` (0=WiFi/1=Ethernet/2=Eth+AP). UI maps them to two buttons in the
  Network tab: **Wireless** (`id="wifi"`→`getConfig("0")`) and **Ethernet** (`id="ethernet"`→
  `getConfig("0123")`), filling `#Main0`/`#Main0123`. There is NO standalone "AP" group (AP
  only exists inside group `0` and as part of `netMode 2`).
- **Danger-button confirm (2026-08-26):** System tab's Reboot/Factory reset/Clear NVS buttons
  (`id=reset`/`deldata`/`clear`) trigger a native `confirm()` in `processStatus` before acting.
  The "Control" subtitle + its verbose hint were removed from the System tab (the buttons stay).
- **Timezone default (2026-08-26):** changed firmware default `timezone` from `GMT0` to
  `GMT-5:30` (POSIX form = IST, UTC+5:30) in `utils.cpp` and the config vector in
  `appSpecific.cpp`, so the device shows local India time out of the box. Live override via
  Settings tab → "Timezone string" field → Save (no flash).
- **Domain checker (2026-08-26):** firmware endpoints added in `appSpecific.cpp` +
  `controlHandler` (webServer.cpp): `/control?chk=<domain>` returns JSON classify
  `{valid,inBase,inCustomBlock,inCustomAllow,status}` (status ∈ blocked|allowed|notlisted|invalid);
  `/control?cblock|callow|cremove=<domain>` cleanly rewrite `/data/custom` (deduped: drop any
  existing plain/`#` line for the domain, append target) AND update in-memory `blockedDomains`
  (`applyCustomMem`) so the change applies with NO reload. Dashboard **Checker** tab (AdBlocker.htm,
  WebDAV) shows a 3-way status badge + contextual actions: Not listed → Block/Allow; Blocked(base) →
  Allow(unblock); Blocked(custom) → Move to Allow / Remove; Allowed(custom) → Move to Block / Remove.
  Build `default_8MB`; bin `build_out/ESP32_AdBlocker.ino.bin`. Endpoints need a FLASH to activate;
  the Checker tab itself is LittleFS (live via WebDAV after hard-refresh).
- **Normal vs Advanced (2026-08-26):** Normal users see ONLY the Dashboard (status cards + a dedicated
  **Block/Unblock card** whose actions are dynamic per check — Block/Allow/Move/Remove appear after the
  check). Block/unblock is the CORE normal-user action, NOT advanced. An **Advanced** header toggle
  (persisted in `localStorage`, default OFF) reveals Logs/Settings/Update tabs (marked `.adv-only`,
  hidden via `body:not(.advanced) .adv-only`). Removed the permanent Quick Block/Check buttons and the
  framework `Main012` injection from the Dashboard. Pure LittleFS (WebDAV), no firmware change.
  Caveat: no login, so Advanced is a UI filter, not access control (advanced URLs still reachable).
