// AdBlocker specific functions
//
// s60sc 2020, 2023, 2026

#include "appGlobals.h"
#include <Ticker.h>  // arch-review #1: periodic dashboard status push over WebSocket
static Ticker dashTicker;

const size_t prvtkey_len = 0;
const size_t cacert_len = 0;
const char* prvtkey_pem = "";
const char* cacert_pem = "";

static size_t maxDomains; // for reserving ptrs memory
static size_t minMemory; // min free memory after vector populated
static const uint16_t maxLineLen = 1024; // max length of line processed in downloaded blocklists
static uint8_t maxDomLen; // max length of domain name in blocklist
static char fileURL[IN_FILE_NAME_LEN] = {0};
static char fmtStorageSize[FILE_NAME_LEN];

static int timeoutVal = 10000; // 10 secs on download stream data being available
static size_t blocklistSize = 0;
static uint8_t domainLine[maxLineLen];
static uint32_t blockCnt = 0, allowCnt = 0, itemsLoaded = 0, duplicates = 0;
static bool stopLoad = false;
static bool downloading = false;
static bool adBlockOn = true; // whether app is set to block or not by user

size_t storageSize;
uint32_t* ptrs; // ordered pointers to domain names
char* storage; // linear domain name storage

static uint32_t binarySearch(const char* searchStr, bool doUpdate) {
  // binary split search
  // for an update, return 0 if found (duplicate) else return ptr
  // for a check, return ptr if found else return 0
  int first = 0, ptr = 0;
  int last = itemsLoaded - 1;
  while (first <= last) {
    ptr = (first + last) / 2;
    int diff = strcmp(storage + ptrs[ptr], searchStr);
    if (diff < 0) first = ptr + 1;
    else if (diff > 0) last = ptr - 1;
    else return doUpdate ? 0 : ptr; // found (diff = 0)
  }
  // not found
  return doUpdate ? ptr : 0;
}

static size_t formatDomain(char* domName) {
  // format input domain name by removing whitespace, www. prefix and converting to lowercase
  trim(domName);
  toCase(domName);
  size_t domLen = strlen(domName);
  int wwwOffset = (strncmp(domName, "www.", 4) == 0) ? 4 : 0;  // remove any leading "www."
  memmove(domName, domName + wwwOffset,  domLen + 1 - wwwOffset);
  return domLen - wwwOffset;
}

static void addDomain(uint32_t ptr, const char* domainStr, size_t domLen) {
  // domain names stored linearly in 'storage' in order received
  // pointer to each domain stored in 'ptrs' sorted alphabetically by corresponding domain
  // the number of unique domains may be lower than the source file which may have
  // entries of the form www .vinted-pl-id002c.celebx.top and vinted-pl-id002c.celebx.top
  // which are treated in this app as a single entry as the www is ignored

  // check what is already at location
  int diff = strcmp(storage + ptrs[ptr], domainStr);             
  // append domain name to storage
  memcpy(storage + blocklistSize, domainStr, domLen);
  // make space for new domain pointer at identified location by shifting following locations
  if (diff < 0) ptr++; // to insert after 
  memmove(&ptrs[ptr + 1], &ptrs[ptr], (itemsLoaded - ptr) * sizeof(uint32_t));

  // insert new domain pointer
  ptrs[ptr] = blocklistSize; // points to latest domain name in 'storage'
  blocklistSize += domLen + 1; // add terminator
  itemsLoaded++;
}

static bool updateCustomFile(char* domainName, bool doDelete) {
  // user supplied domain to add to or delete from blocklist
  File file = STORAGE.open(CUSTOM_FILE_PATH, FILE_APPEND);
  if (file) {
    if (doDelete) file.print("#"); // mark as deleted
    file.println(domainName);
    file.close();
    return true;
  } else LOG_ERR("Failed to open %s", CUSTOM_FILE_PATH);
  return false;
}

IPAddress checkBlocklist(const char* domainName) {
  // called from externalDNS.cpp
  bool blocked = false;
  if (adBlockOn) {
    static char blockedDomain[FILE_NAME_LEN] = {0};
    uint64_t usElapsed = micros();
    // check if received domain name same as previous blocked domain to skip search
    blocked = !strcmp(domainName, blockedDomain) ? true : (bool)binarySearch(domainName, false);
    if (blocked) strcpy(blockedDomain, domainName);
    blocked ? ++blockCnt : ++allowCnt;
    uint64_t checkTime = micros() - usElapsed;
    LOG_VRB("Check %s %s in %lluus", domainName, (blocked) ? "*Blocked*" : "Allowed", checkTime);
  }
  return blocked ? IPAddress(0, 0, 0, 0) : resolveDomain(domainName);
}

static void checkDomain(const char* inName, bool doUpdate, bool doDelete) {
  // check if user supplied domain name is present or update user supplied name
  char domName[IN_FILE_NAME_LEN];
  strcpy(domName, inName);
  if (size_t domLen = formatDomain(domName); domLen > 0) {
    if (domLen >= maxDomLen) LOG_ALT("Domain name %s is too long to process", domName);
    else {
      uint32_t blPtr = binarySearch(domName, doUpdate);
      if (doUpdate) { // addition
        if (blPtr) {
          // not found, so insert domain if resolves at blPtr location
          if (resolveDomain(domName) != IPAddress(0, 0, 0, 0)) {
            // resolved
            addDomain(blPtr, domName, domLen);
            if (updateCustomFile(domName, false)) LOG_ALT("Domain name %s IS added to blocklist", domName);
          } else LOG_ALT("Domain name %s NOT added to blocklist as not resolved", domName);
        } else LOG_ALT("Domain name %s NOT added to blocklist as duplicate", domName);
      } else {
        // delete or just check
        if (doDelete) { // deletion
          if (blPtr) {
            // found, so delete
            *(storage + ptrs[blPtr]) = 0; // set domain name empty 
            if (updateCustomFile(domName, true)) LOG_ALT("Domain name %s IS deleted", domName);
          } else LOG_ALT("Domain name %s NOT deleted as not in blocklist", domName);
        } else LOG_ALT("Domain name %s %s in blocklist", domName, blPtr ? "IS" : "NOT"); // check only
      }
    }
  } else LOG_ALT("No domain name entered");
}

// ---- ESP_hole Domain checker: classify + clean custom-list edit ----
static void scanCustom(const char* domName, bool& inBlock, bool& inAllow) {
  // determine if domName is present in /data/custom as block (plain) or allow (#)
  inBlock = false; inAllow = false;
  if (!STORAGE.exists(CUSTOM_FILE_PATH)) return;
  File f = STORAGE.open(CUSTOM_FILE_PATH, FILE_READ);
  if (!f) return;
  char nm[IN_FILE_NAME_LEN];
  while (f.available()) {
    String s = f.readStringUntil('\n');
    s.trim();
    if (!s.length()) continue;
    if (s.charAt(0) == '#') { strcpy(nm, s.substring(1).c_str()); if (!strcasecmp(nm, domName)) inAllow = true; }
    else { strcpy(nm, s.c_str()); if (!strcasecmp(nm, domName)) inBlock = true; }
  }
  f.close();
}

static void rewriteCustom(const char* domName, int mode) {
  // mode 0=block(plain), 1=allow(#), 2=remove(both): drop existing lines for domName, then append target
  String keep = "";
  if (STORAGE.exists(CUSTOM_FILE_PATH)) {
    File f = STORAGE.open(CUSTOM_FILE_PATH, FILE_READ);
    if (f) {
      while (f.available()) {
        String s = f.readStringUntil('\n');
        s.trim();
        if (!s.length()) continue;
        String cmp = (s.charAt(0) == '#') ? s.substring(1) : s;
        if (strcasecmp(cmp.c_str(), domName) == 0) continue;
        keep += s + "\n";
      }
      f.close();
    }
  }
  STORAGE.remove(CUSTOM_FILE_PATH);
  File wf = STORAGE.open(CUSTOM_FILE_PATH, FILE_WRITE);
  if (wf) {
    if (keep.length()) wf.print(keep);
    if (mode == 0) wf.println(domName);
    else if (mode == 1) wf.println("#" + String(domName));
    wf.close();
  }
}

static void applyCustomMem(const char* domName, int mode) {
  // keep in-memory blockedDomains consistent with the new custom state (no reload needed)
  uint32_t blPtr = binarySearch(domName, false);
  bool inBase = (blPtr != 0);
  if (mode == 1) { if (inBase) *(storage + ptrs[blPtr]) = 0; } // allow: unblock if blocked
  else if (mode == 0) { if (!inBase && resolveDomain(domName) != IPAddress(0,0,0,0)) { uint32_t ins = binarySearch(domName, true); if (ins) addDomain(ins, domName, strlen(domName)); } }
  else { if (!inBase && blPtr) *(storage + ptrs[blPtr]) = 0; } // remove: revert non-base to not-listed
}

void classifyDomain(httpd_req_t* req, const char* inName) {
  char domName[IN_FILE_NAME_LEN];
  strcpy(domName, inName);
  bool valid = formatDomain(domName) > 0;
  bool inBase = false, inBlock = false, inAllow = false;
  const char* status = "invalid";
  if (valid) {
    inBase = (binarySearch(domName, false) != 0);
    scanCustom(domName, inBlock, inAllow);
    if (inAllow) status = "allowed";
    else if (inBase || inBlock) status = "blocked";
    else status = "notlisted";
  }
  char buf[200];
  snprintf(buf, sizeof(buf),
    "{\"domain\":\"%s\",\"valid\":%s,\"inBase\":%s,\"inCustomBlock\":%s,\"inCustomAllow\":%s,\"status\":\"%s\"}",
    domName, valid?"true":"false", inBase?"true":"false", inBlock?"true":"false", inAllow?"true":"false", status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, buf);
}

void setCustomDomain(httpd_req_t* req, const char* inName, int mode) {
  char domName[IN_FILE_NAME_LEN];
  strcpy(domName, inName);
  if (formatDomain(domName) == 0) { httpd_resp_set_type(req, "application/json"); httpd_resp_sendstr(req, "{\"domain\":\"\",\"status\":\"invalid\"}"); return; }
  rewriteCustom(domName, mode);
  applyCustomMem(domName, mode);
  const char* status = (mode == 1) ? "allowed" : (mode == 0 ? "blocked" : "notlisted");
  char buf[120];
  snprintf(buf, sizeof(buf), "{\"domain\":\"%s\",\"status\":\"%s\"}", domName, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, buf);
}

static void extractBlocklist() {
  // extract domain names from downloaded blocklist file
  char* saveItem = NULL;
  char* tokenItem;
  char* domStr = (char*)domainLine;

  // for each line
  if (strncmp(domStr, "127.0.0.1", 9) == 0 || strncmp(domStr, "0.0.0.0", 7) == 0) {
    // HOSTS file format matched, extract domain name
    tokenItem = strtok_r(domStr, " \t", &saveItem); // skip over first token
    if (tokenItem != NULL) tokenItem = strtok_r(NULL, " \t", &saveItem); // domain in second token
  } else {
    if (strncmp(domStr, "||", 2) == 0) tokenItem = strtok_r(domStr, "|^", &saveItem); // Adblock format - domain in first token
    else tokenItem = NULL; // no match         
  }

  if (tokenItem != NULL) {
    // write processed domain to storage
    size_t domLen = formatDomain(tokenItem); 
    if (domLen && (domLen < maxDomLen)) {
      uint32_t ptr = binarySearch(tokenItem, true);
      if (ptr) addDomain(ptr, tokenItem, domLen);
      else duplicates++;
    }
  }
}

static bool downloadBlockList() {
  // download blocklist file from github
  bool res = false;
  NetworkClientSecure wclient;
  // blocklist is a public, non-sensitive file: skip cert validation so the load
  // succeeds even if the device clock is wrong (NTP may fail on some networks)
  if (remoteServerConnect(wclient, GITHUB_HOST, HTTPS_PORT, "", BLOCKLIST)) {
    HTTPClient https;
    size_t downloadSize = 0;
    char progStr[10];

    if (https.begin(wclient, fileURL)) {
      downloading = true;
      LOG_INF("Downloading %s\n", fileURL);
      int httpCode = https.GET();
      if (httpCode > 0) {
        uint32_t loadTime = millis();
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          // file available for download
          // get length of content (is -1 when Server sends no Content-Length header)
          int left = https.getSize();
          if (left > 0) LOG_INF("File size: %s", fmtSize(left));
          else LOG_WRN("File size unknown");
          LOG_INF("%s memory available for download", fmtStorageSize);
          if (left > storageSize) LOG_WRN("File is larger than memory, may get truncated");
          WiFiClient* stream = https.getStreamPtr(); // stream data to client
          uint32_t lastRead = millis();
          size_t lineCnt = 0;

          while (https.connected() && (left > 0 || left == -1)) {
            if (stopLoad) break;
            if (stream->available()) {
              size_t lineSize = stream->readBytesUntil('\n', domainLine, maxLineLen);
              domainLine[lineSize] = 0;
              lineSize++; // add in count for terminator
              downloadSize += lineSize;
              if (left > 0) left -= lineSize;
              extractBlocklist();
              if (itemsLoaded >= maxDomains) {
                LOG_ALT("Blocklist truncated as domain limit reached %u", maxDomains);
                break;
              }
              if (++lineCnt % 1000 == 0) {
                // periodically check remaining memory
                size_t remaining = storageSize - blocklistSize;
                if (remaining < maxLineLen) {
                  LOG_ALT("Blocklist truncated to avoid memory overflow, %u bytes remaining\n", remaining);
                  break;
                }
                // show progress
                if (left > 0) {
                  float loadProg = (float)(downloadSize * 100.0 / (downloadSize + left));
                  LOG_SEND("%0.1f%%\n", loadProg);
                  sprintf(progStr, "%0.1f%%", loadProg);
                  updateConfigVect("loadProg", progStr);
                }
              }
              lastRead = millis();
            } else if (millis() - lastRead > timeoutVal) {
              // timed out on read
              if (left > 0) LOG_WRN("Timeout on download, %s unread", fmtSize(left));
              break;
            }
          }
          ptrs[itemsLoaded] = blocklistSize;
          LOG_INF("Download complete, processed %s in %lu secs", fmtSize(downloadSize), (millis() - loadTime) / 1000);
          LOG_ALT("Loaded %lu blocked domains excluding %lu duplicates, using %s of %s", itemsLoaded - 2, duplicates, fmtSize(blocklistSize), fmtStorageSize);
          res = true;
        } else LOG_WRN("Unexpected result code %u %s", httpCode, https.errorToString(httpCode).c_str());
      } else LOG_ERR("Connection failed with error: %s", https.errorToString(httpCode).c_str());
    } else {
      char errBuf[100] = {0};
      wclient.lastError(errBuf, 100);
      LOG_ERR("Could not connect to %s, err: %s", fileURL, errBuf);
    }
    https.end();
  } 

  remoteServerClose(wclient);
  if (stopLoad) {
    LOG_ALT("Blocklist load stopped by user request");
    updateConfigVect("loadProg", "Stopped");
    res = true;
  } else if (res) updateConfigVect("loadProg", "Complete");
  else updateConfigVect("loadProg", "Failed");
  return res;
}

static void loadCustom() {
  // process custom blocklist file entries
  File file;
  static uint32_t customAdded = 0, customDeleted = 0;
  if (!STORAGE.exists(CUSTOM_FILE_PATH)) {
    // create file on first call
    file = STORAGE.open(CUSTOM_FILE_PATH, FILE_WRITE);
    if (file) file.close();
    else LOG_WRN("Failed to create file %s", CUSTOM_FILE_PATH);
  } else {
    // read in entries
    file = STORAGE.open(CUSTOM_FILE_PATH, FILE_READ);
    char domName[IN_FILE_NAME_LEN];
    while (file.available()) {
      bool doAdd = true;
      String customLineStr = file.readStringUntil('\n');
      customLineStr.trim(); 
      if (customLineStr.length()) {
        if (customLineStr.charAt(0) == '#') {
          doAdd = false;  // deletion
          strcpy(domName, customLineStr.substring(1).c_str());
        } else strcpy(domName, customLineStr.c_str()); // addition
        uint32_t blPtr = binarySearch(domName, doAdd);
        if (blPtr) {
          if (doAdd) {
            // addition
            addDomain(blPtr, domName, strlen(domName));
            customAdded++;
          } else {
            // deletion
            *(storage + ptrs[blPtr]) = 0; // set domain name empty 
            customDeleted++;
          }
        } else LOG_WRN("Ignored custom %s of %s", doAdd ? "addition" : "deletion", domName);
      }
    }
    file.close();
  }
  LOG_ALT("Loaded %lu custom blocked domains, unblocked %lu domains", customAdded, customDeleted);
}

static void showBlockList(int maxItems = 0) {
  // for info
  if (!maxItems) maxItems = itemsLoaded;
  for (int i = 0; i < maxItems; i++) LOG_SEND("%d: %s\n", i, storage + ptrs[i]);
  LOG_SEND("Total %lu items\n", itemsLoaded);
}

static bool loadBlockList(const char* reason) {
  // load or refresh blocklist file
  if (!downloading) {
    duplicates = 0;
    updateConfigVect("loadProg", "0.0%");
    LOG_INF("%s load of latest blocklist", reason);
    if (netIsConnected()) {
      if (!downloadBlockList()) {
        snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Blocklist URL %s failed to load", fileURL);
        LOG_WRN("%s", startupFailure);
        LOG_INF("*** Enter on browser: http://<AdBlocker IP>/control?zLoad=<valid blocklist URL>");
        downloading = false;
      }
      loadCustom();
    } else LOG_WRN("Internet download not available");
  } else LOG_WRN("Ignore request as download in progress");
  return downloading;
}

static const uint32_t BL_CACHE_MAGIC = 0x424C3031;  // "BL01" blocklist cache marker

static bool saveCachedBlockList() {
  // persist the parsed main blocklist to LittleFS so future boots work offline
  if (itemsLoaded <= 2) return false;  // nothing meaningful to cache
  File f = STORAGE.open(BLOCKLIST_CACHE_PATH, FILE_WRITE);
  if (!f) { LOG_WRN("Cannot open %s for cache write", BLOCKLIST_CACHE_PATH); return false; }
  uint32_t hdr[5] = { BL_CACHE_MAGIC, 1, itemsLoaded, blocklistSize, duplicates };
  size_t wrote = 0;
  wrote += f.write((uint8_t*)hdr, sizeof(hdr));
  wrote += f.write((uint8_t*)ptrs, (itemsLoaded + 1) * sizeof(uint32_t));
  wrote += f.write((uint8_t*)storage, blocklistSize);
  f.close();
  size_t expect = sizeof(hdr) + (itemsLoaded + 1) * sizeof(uint32_t) + blocklistSize;
  if (wrote != expect) { LOG_WRN("Blocklist cache write incomplete (%u/%u)", wrote, expect); return false; }
  LOG_INF("Blocklist cached to %s (%lu domains, %s)", BLOCKLIST_CACHE_PATH, itemsLoaded - 2, fmtSize(blocklistSize));
  return true;
}

static bool loadCachedBlockList() {
  // restore parsed main blocklist from LittleFS (fast, offline-capable)
  if (!STORAGE.exists(BLOCKLIST_CACHE_PATH)) return false;
  File f = STORAGE.open(BLOCKLIST_CACHE_PATH, FILE_READ);
  if (!f) return false;
  uint32_t hdr[5];
  if (f.read((uint8_t*)hdr, sizeof(hdr)) != sizeof(hdr)) { LOG_WRN("Blocklist cache header unreadable"); f.close(); return false; }
  if (hdr[0] != BL_CACHE_MAGIC) { LOG_WRN("Blocklist cache magic mismatch"); f.close(); return false; }
  uint32_t cachedItems = hdr[2], cachedSize = hdr[3], cachedDup = hdr[4];
  if (cachedItems == 0 || cachedItems > maxDomains || cachedSize == 0 || cachedSize > storageSize) {
    LOG_WRN("Blocklist cache rejected (items=%lu size=%u, limit %u)", cachedItems, cachedSize, storageSize); f.close(); return false;
  }
  size_t needPtrs = (cachedItems + 1) * sizeof(uint32_t);
  if (f.read((uint8_t*)ptrs, needPtrs) != needPtrs) { LOG_WRN("Blocklist cache ptrs unreadable"); f.close(); return false; }
  if (f.read((uint8_t*)storage, cachedSize) != cachedSize) { LOG_WRN("Blocklist cache storage unreadable"); f.close(); return false; }
  f.close();
  itemsLoaded = cachedItems;
  blocklistSize = cachedSize;
  duplicates = cachedDup;
  LOG_INF("Blocklist restored from cache %s (%lu domains, %s)", BLOCKLIST_CACHE_PATH, itemsLoaded - 2, fmtSize(blocklistSize));
  return true;
}

void appSetup() {
  while (!strlen(fileURL)) {
    LOG_ALT("Enter blocklist URL on web page ...");
    delay(30000); // wait for file URL to be entered
  }
  ptrs = (uint32_t*)ps_calloc((maxDomains + 2), sizeof(uint32_t));
  storageSize = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) - minMemory;
  strcpy(fmtStorageSize, fmtSize(storageSize));
  storage = (char*)ps_calloc(storageSize, sizeof(char));

  // prime domain storage for binary search to prevent pointer 0 being returned
  memcpy(storage, "!", 1); // always first so ptrs[0] = 0
  blocklistSize = 2;
  itemsLoaded = 1;
  addDomain(0, "#", 1);

  updateConfigVect("blockCnt", "0");
  updateConfigVect("allowCnt", "0");

  // 1) Restore blocklist from LittleFS cache (fast, works with no internet)
  bool fromCache = loadCachedBlockList();
  if (fromCache) {
    loadCustom();
    updateConfigVect("loadProg", "Cached");
    LOG_INF("Blocklist ready from cache (%lu domains); DNS + blocking up", itemsLoaded - 2);
  }

  // 2) ALWAYS start the DNS server now (fail-safe). With a cached list, blocking is
  //    active immediately; without one, it runs forwarding-only until a list arrives.
  prepDNS();

  // 3) If we have no cache yet, this is first-run / cache-missing: download once to
  //    populate it. We deliberately do NOT download on every boot.
  if (!fromCache) {
    if (netIsConnected()) {
      if (loadBlockList("Initial")) {
        saveCachedBlockList();   // loadBlockList already applied custom
        updateConfigVect("loadProg", "Complete");
      } else {
        updateConfigVect("loadProg", "Failed");
        snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Blocklist unavailable (no cache, no internet)");
        LOG_WRN("No blocklist available; forwarding-only DNS until connectivity returns");
      }
    } else {
      LOG_INF("No internet at first boot and no cache; forwarding-only until connectivity returns");
    }
  }
  // arch-review #1: start periodic minimal dashboard status push over WebSocket (5s).
  // wsAsyncSendJson() no-ops when no browser is connected, so this is cheap when idle.
  dashTicker.attach(5, sendWsDashStatus);
}

/************************ webServer callbacks *************************/

bool updateAppStatus(const char* variable, const char* value, bool fromUser) {
  // update vars from configs and browser input
  bool res = true;
  int intVal = atoi(value);
  if (!strcmp(variable, "custom")) {
    // update config for latest stats to return on next main page call
    char cntStr[20];
    sprintf(cntStr, "%lu", blockCnt);
    updateConfigVect("blockCnt", cntStr);
    sprintf(cntStr, "%lu", allowCnt);
    updateConfigVect("allowCnt", cntStr);
  }
  else if (!strcmp(variable, "fileURLc")) strncpy(fileURL, value, IN_FILE_NAME_LEN - 1);
  else if (!strcmp(variable, "maxDomains")) maxDomains = intVal * 1000;
  else if (!strcmp(variable, "minMemory")) minMemory = intVal * 1024;
  else if (!strcmp(variable, "maxDomLen")) maxDomLen = intVal;
  else if (!strcmp(variable, "showBL")) showBlockList(intVal); // not on web page
  else if (fromUser && !strcmp(variable, "xStop")) {
    stopLoad = true;
    LOG_ALT("Blocklist load being stopped");
  }
  // add user supplied domain name to blocklist unless a duplicate or invalid
  else if (fromUser && !strcmp(variable, "uLoad")) checkDomain(value, true, false);
  // delete user supplied domain name from blocklist if present
  else if (fromUser && !strcmp(variable, "vLoad")) checkDomain(value, false, true);
  // check if user supplied domain name in blocklist
  else if (fromUser && !strcmp(variable, "wLoad")) checkDomain(value, false, false);
  else if (fromUser && !strcmp(variable, "zLoad")) {
    // reload or load new blocklist
    stopLoad = false;
    if (strlen(value)) { 
      strncpy(fileURL, value, IN_FILE_NAME_LEN - 1);
      updateConfigVect("fileURLc", value);
      updateStatus("save", "0");
    } 
    doRestart("Reload blocklist request");
  } 
  else if (fromUser && !strcmp(variable, "zzCustom")) {
    STORAGE.remove(CUSTOM_FILE_PATH);
    LOG_ALT("Deleted custom blocklist file");
  }
  else if (!strcmp(variable, "zzzAdblockOn")) {
    adBlockOn = (bool)intVal;
    if (adBlockOn) LOG_ALT("Ad blocking enabled");
    else LOG_WRN("Ad blocking disabled");
  }
  return res;
}

void appSpecificWsBinHandler(uint8_t* wsMsg, size_t wsMsgLen) {
  LOG_ERR("Unexpected websocket binary frame");
}

void appSpecificWsHandler(const char* wsMsg) {
  // message from web socket
  int wsLen = strlen(wsMsg) - 1;
  switch ((char)wsMsg[0]) {
    case 'X':
    break;
    case 'H':
      // keepalive heartbeat, return status
    break;
    case 'S':
      // status request
      buildJsonString(wsLen); // required config number
      LOG_SEND("%s\n", jsonBuff);
    break;
    case 'U':
      // update or control request
      memcpy(jsonBuff, wsMsg + 1, wsLen); // remove 'U'
      parseJson(wsLen);
    break;
    case 'K':
      // kill websocket connection
      killSocket();
    break;
    default:
      LOG_WRN("unknown command %c", (char)wsMsg[0]);
    break;
  }
}

char* buildAppJsonString(bool filter) {
  // build app specific part of json string
  char* p = jsonBuff + 1;
  return p;
}

esp_err_t appSpecificWebHandler(httpd_req_t *req, const char* variable, const char* value) {
  return ESP_FAIL;
}

esp_err_t appSpecificSustainHandler(httpd_req_t* req) {
  return ESP_OK;
}

void externalAlert(const char* subject, const char* message) {
  // alert any configured external servers
}

bool appDataFiles() {
  // callback from setupAssist.cpp, for any app specific files
  return true;
}

void doAppPing(bool timeSynced) {
  // if daily alarm occurs, load latest blocklist from host site
  if (checkAlarm() && strlen(fileURL)) {
    if (loadBlockList("Scheduled")) saveCachedBlockList();  // refresh cache on update
  }
  // If we still have no usable blocklist (e.g. first boot with no internet), retry the
  // download whenever connectivity is present so we eventually cache + start blocking.
  if (itemsLoaded <= 2 && netIsConnected() && !downloading) {
    static uint32_t lastRetryMs = 0;
    if (millis() - lastRetryMs > 60000) {  // at most one attempt per minute
      lastRetryMs = millis();
      LOG_INF("Retrying blocklist download (no list cached yet)...");
      if (loadBlockList("Retry")) saveCachedBlockList();
    }
  }
}

void sendWsDashStatus() {
  // arch-review #1: push a MINIMAL dashboard status over WebSocket so the browser no
  // longer needs to poll GET /status. Keys match the dashboard DOM element ids.
  char timeBuff[20];
  formatElapsedTime(timeBuff, millis());
  int rssi = netRSSI();
  const char* modeLabel = (netMode == 0) ? "WiFi station"
                        : (netMode == 1) ? "Ethernet"
                        : (netMode == 2) ? "Ethernet + AP"
                        : "AP only";
  char dashJson[420];
  snprintf(dashJson, sizeof(dashJson),
    "\"dash_uptime\":\"%s\",\"dash_mode\":\"%s\",\"dash_ip\":\"%s\",\"dash_extip\":\"%s\",\"dash_rssi\":\"%i dBm\",\"dash_host\":\"%s\",\"abToggle\":\"%d\"",
    timeBuff, modeLabel, formatIPstr(), extIP, rssi, hostName, adBlockOn ? 1 : 0);
  wsAsyncSendJson("dash", dashJson);
}

void OTAprereq() {
  stopPing();
}

/************** default app configuration **************/
const char* appConfig = R"~(
restart~~99~T~na
ST_SSID~~0~T~Wifi SSID name
ST_Pass~~0~T~Wifi SSID password
ST_ip~~0~T~Static IP address
ST_gw~~0~T~Router IP address
ST_sn~255.255.255.0~0~T~Router subnet
ST_ns1~1.1.1.1~0~T~DNS server
ST_ns2~8.8.8.8~0~T~Alt DNS server
AP_Pass~~0~T~AP Password
AP_ip~~0~T~AP IP Address if not 192.168.4.1
AP_sn~~0~T~AP subnet
AP_gw~~0~T~AP gateway
useHttps~0~0~C~Enable HTTPS connection to app
allowAP~0~0~C~Allow simultaneous AP
timezone~GMT-5:30~1~T~Timezone string: tinyurl.com/TZstring
logType~0~99~N~Output log selection
Auth_Name~~0~T~Optional user name for web page login
Auth_Pass~~0~T~Optional web page password
formatIfMountFailed~0~1~C~Format file system on failure
wifiTimeoutSecs~30~0~N~WiFi connect timeout (secs)
alarmHour~4~1~N~Hour of day for blocklist update
usePing~1~0~C~Use ping
maxDomains~200~1~N~Max number of domains (* 1000)
minMemory~128~1~N~Minimum free memory (KB)
maxDomLen~100~1~N~Max length of domain name
allowCnt~0~2~D~Allowed domains
blockCnt~0~2~D~Blocked domains
fileURLc~https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts~2~D~Current URL for blocklist file
fileURLn~~2~X~Enter new URL for blocklist file or domain
loadProg~0~2~D~Blocklist download progress
netMode~0~3~S:WiFi:Ethernet:Eth+AP~Network interface selection
wLoad~Check Domain~2~A~Check if domain name is blocked
uLoad~Add Domain~2~A~Add to blocklist
vLoad~Del Domain~2~A~Delete from blocklist
zLoad~Reload~2~A~Reload Blocklist
xStop~Stop Load~2~A~Stop Blocklist Load
zzCustom~Clear~2~A~Clear custom blocklist
zzzAdblockOn~1~2~C~Enable AdBlocker 
ethCS~-1~3~N~Ethernet CS pin
ethInt~-1~3~N~Ethernet Interrupt pin
ethRst~-1~3~N~Ethernet Reset pin
ethSclk~-1~3~N~Ethernet SPI clock pin
ethMiso~-1~3~N~Ethernet SPI MISO pin
ethMosi~-1~3~N~Ethernet SPI MOSI pin
)~";
