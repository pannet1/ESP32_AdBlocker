
// Assist setup for new app installations
// original provided by gemi254
// 
// s60sc 2023

#include "appGlobals.h"

#if (!INCLUDE_CERTS)
const char* git_rootCACertificate = "";
#endif

static bool wgetFile(const char* filePath) {
  // download required data file from github repository and store
  bool res = false;
  if (STORAGE.exists(filePath)) {
    // if file exists but is empty, delete it to allow re-download
    File f = STORAGE.open(filePath, FILE_READ);
    size_t fSize = f.size();
    f.close();
    if (!fSize) STORAGE.remove(filePath);
  }
  if (!STORAGE.exists(filePath)) {
    char downloadURL[150];
    snprintf(downloadURL, 150, "%s%s", GITHUB_PATH, filePath);
    File f = STORAGE.open(filePath, FILE_WRITE);
    if (f) {
      NetworkClientSecure wclient;
      // public default config: skip cert validation (clock may be wrong pre-NTP)
      if (remoteServerConnect(wclient, GITHUB_HOST, HTTPS_PORT, "", SETASSIST)) {
        HTTPClient https;
        if (https.begin(wclient, GITHUB_HOST, HTTPS_PORT, downloadURL)) {
          LOG_INF("Downloading %s from %s", filePath, downloadURL);
          int httpCode = https.GET();
          int fileSize = 0;
          if (httpCode == HTTP_CODE_OK) {
            fileSize = https.writeToStream(&f);
            if (fileSize <= 0) {
              LOG_WRN("Download failed: writeToStream - %s", https.errorToString(fileSize).c_str());
              httpCode = 0;
            } else LOG_INF("Downloaded %s, size %s", filePath, fmtSize(fileSize));       
          } else LOG_WRN("Download failed, error: %s", https.errorToString(httpCode).c_str());    
          https.end();
          f.close();
          if (httpCode == HTTP_CODE_OK) {
            if (!strcmp(filePath, CONFIG_FILE_PATH)) doRestart("Config file downloaded");
            res = true;
          } else {
            LOG_WRN("HTTP Get failed with code: %d", httpCode);
            STORAGE.remove(filePath);
          }
        }
      }
      remoteServerClose(wclient);
    } else LOG_WRN("Open failed: %s", filePath);
  } else res = true;
  return res;
}

bool checkDataFiles() {
  // Download any missing data files
  bool res = false;
  if (strlen(GITHUB_PATH)) {
    res = wgetFile(COMMON_JS_PATH); 
    if (res) res = wgetFile(INDEX_PAGE_PATH); 
    if (res) res = appDataFiles(); 
  } else res = true; // no download needed
  return res;
}

const char* setupPage_html = R"~(
<!doctype html>
<html>
<head>
<meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Application setup</title>
<style>
  body{background-color:#e0f7fa;font-family:Arial,sans-serif}
  .dropdown{position:relative;display:inline-block;width:calc(100%)}
  .dropdown-content{display:none;position:absolute;background-color:#fff;width:100%;box-shadow:0 8px 16px 0 rgba(0,0,0,0.2);z-index:1;overflow:hidden;white-space:nowrap}
  .dropdown-content div,.dropdown-content button{color:black;padding:12px 16px;text-decoration:none;display:flex;justify-content:space-between;align-items:center;width:100%;border:0;background:0;cursor:pointer}
  .dropdown-content div:hover,.dropdown-content button:hover{background-color:#f1f1f1}
  .network-details{display:flex;align-items:center;gap:8px}
  .signal-icon{width:16px;height:16px;background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAGAAAAAQCAMAAADeZIrLAAAAJFBMVEX///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADHJj5lAAAAC3RSTlMAIjN3iJmqu8zd7vF8pzcAAABsSURBVHja7Y1BCsAwCASNSVo3/v+/BUEiXnIoXkoX5jAQMxTHzK9cVSnvDxwD8bFx8PhZ9q8FmghXBhqA1faxk92PsxvRc2CCCFdhQCbRkLoAQ3q/wWUBqG35ZxtVzW4Ed6LngPyBU2CobdIDQ5oPWI5nCUwAAAAASUVORK5CYII=');background-size:96px 16px;position:absolute;right:16px}
  .wpa-text{position:absolute;right:64px;font-size:.3em}
  .encryption-icon{position:absolute;right:32px;font-size:1em}
  .input{width:calc(100% - 32px);padding:12px 16px;border:1px solid #ccc;border-radius:4px;box-sizing:border-box;display:inline-block}
  .center-button{display:flex;justify-content:center}
  .styled-button{background-color:#007bff;color:white;border:0;padding:10px 20px;text-align:center;text-decoration:none;display:inline-block;font-size:16px;margin:4px 2px;cursor:pointer;border-radius:4px}
  .styled-button:hover{background-color:#0056b3}
</style>
</head>
<script>
let ssidList=[];

function fetchSSIDs(){
  const ssidSelect=document.getElementById('ST_SSID');
  ssidSelect.placeholder='Loading...';
  const scanButton=document.getElementById('scan-button');
  if(scanButton){scanButton.textContent='Scanning...';scanButton.disabled=true;}

  fetch('/wifi').then(response=>response.json()).then(data=>{
    ssidList=data.networks;
    updateDropdown();
    ssidSelect.placeholder='Select SSID from dropdown';
    if(scanButton){scanButton.textContent='Scan';scanButton.disabled=false;}
  }).catch(error=>{console.error('Error fetching SSIDs:',error);
  ssidSelect.placeholder='Select SSID from dropdown';
  if(scanButton){scanButton.textContent='Scan';scanButton.disabled=false;}
  });
}

function updateDropdown(){
  const dropdownContent=document.getElementById('dropdown-content');
  dropdownContent.innerHTML='';
  let longestSSID='';
  ssidList.forEach(network=>{if(network.ssid.length>longestSSID.length){longestSSID=network.ssid;}});
  const tempElement=document.createElement('span');
  tempElement.style.visibility='hidden';
  tempElement.style.whiteSpace='nowrap';
  tempElement.textContent=longestSSID;
  document.body.appendChild(tempElement);
  const dropdownWidth=tempElement.offsetWidth+100;
  document.body.removeChild(tempElement);
  dropdownContent.style.width=`${dropdownWidth}px`;
  ssidList.forEach(network=>{
    let signalStrength;
    if(network.strength>=-65){signalStrength=4;}
    else if(network.strength>=-75){signalStrength=3;}
    else if(network.strength>=-85){signalStrength=2;}
    else if(network.strength>=-95){signalStrength=1;}
    else{signalStrength=0;}
    const div=document.createElement('div');
    const encryptionStatus=network.encryption==='Open'?'🔓':'🔒';
    div.innerHTML=`<span>${network.ssid}</span><div class="network-details"><span class="encryption-icon">${encryptionStatus}</span><span class="signal-icon"style="background-position: -${signalStrength * 16}px 0;"alt="Signal Strength"></span></div>`;div.onclick=()=>{document.getElementById('ST_SSID').value=network.ssid;dropdownContent.style.display='none';};
    dropdownContent.appendChild(div);
  });
  const scanButton=document.createElement('button');
  scanButton.id='scan-button';
  scanButton.textContent='Scan';
  canButton.classList.add('center-button');
  scanButton.onclick=fetchSSIDs;
  dropdownContent.appendChild(scanButton);
}

function toggleDropdown(){
  const dropdownContent=document.getElementById('dropdown-content');
  dropdownContent.style.display=dropdownContent.style.display==='block'?'none':'block';
}

function hideDropdown(event){
  const dropdownContent=document.getElementById('dropdown-content');
  if(!event.target.closest('.dropdown')){dropdownContent.style.display='none';}

}

function Config(){
  if(!window.confirm('This will reboot the device to activate new settings.'))return false;
  fetch('/control?ST_SSID='+encodeURI(document.getElementById('ST_SSID').value)).then(r=>{
    console.log(r);
    return fetch('/control?ST_Pass='+encodeURI(document.getElementById('ST_Pass').value))}).then(r=>{console.log(r);
    return fetch('/control?save=1')}).then(r=>{console.log(r);
    return fetch('/control?reset=1')}).then(r=>{console.log(r);
  });
  return false;
}

window.onload=fetchSSIDs;
document.addEventListener('click',hideDropdown);
</script>
<body>
<br>
<center>
<table border=0>
<tr>
<th colspan=3>Wifi setup..</th>
</tr>
<tr>
<td colspan=3></td>
</tr>
<tr>
<td colspan=3>
<label for=ST_SSID>SSID</label>
<div class=dropdown>
<input id=ST_SSID name=ST_SSID placeholder=Loading... class=input onclick=toggleDropdown() autocomplete=off>
<div id=dropdown-content class=dropdown-content></div>
</div>
</td>
</tr>
<tr>
<td colspan=3>
<label for=ST_Pass>Password</label>
<input id=ST_Pass name=ST_Pass length=64 placeholder="Router password" class=input autocomplete=off>
</td>
</tr>
<tr>
<td colspan=3></td>
</tr>
<tr>
<td colspan=3 align=center>
<button type=button class=styled-button onClick="return Config()">Connect</button>
<button type=button class=styled-button onclick=window.location.reload()>Cancel</button>
</td>
</tr>
</table>
<br /><br /><a href=/web?OTA.htm><button class=styled-button>OTA
Update</button></a>
</center>
</body>
</html>
)~";

const char* otaPage_html = R"~(
<!DOCTYPE html>
<html>
  <head>
    <title>OTA</title>
    <style>
      :root{
        --bg:#eef2f7; --card:#fff; --text:#0f172a; --muted:#64748b; --border:#e2e8f0;
        --accent:#0f766e; --accent-700:#115e59; --warn:#ef4444;
        --radius:14px; --shadow:0 1px 3px rgba(15,23,42,.08);
        --font:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
      }
      *{box-sizing:border-box}
      body{margin:0;font-family:var(--font);background:var(--bg);color:var(--text);line-height:1.5}
      .topbar{display:flex;align-items:center;gap:10px;padding:12px 18px;
        background:linear-gradient(120deg,var(--accent-700),var(--accent));color:#fff}
      .logo{font-size:22px}.brand{font-weight:700;font-size:17px}
      main{max-width:640px;margin:0 auto;padding:18px}
      .card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);
        padding:20px;box-shadow:var(--shadow);margin-top:18px}
      h3{margin:0 0 14px;font-size:17px}
      .hint{color:var(--muted);font-size:13px;margin:0 0 14px}
      .filebtn{display:inline-block;padding:12px 18px;border:2px dashed var(--border);border-radius:10px;
        background:#f8fafc;cursor:pointer;font-weight:600;color:var(--accent-700)}
      .filebtn:hover{background:#f1f5f9}
      input[type=file]{display:none}
      progress{width:100%;height:10px;border:0;appearance:none;display:block;margin:14px 0 6px}
      progress::-webkit-progress-bar{background:#e2e8f0;border-radius:999px}
      progress::-webkit-progress-value{background:var(--accent);border-radius:999px}
      progress::-moz-progress-bar{background:var(--accent)}
      #status{font-weight:600;margin:4px 0}
      #loaded_n_total{color:var(--muted);font-size:13px}
      .link{color:var(--accent-700);font-weight:600;text-decoration:none}
    </style>
  </head>
  <body>
    <header class="topbar">
      <span class="logo">🛡️</span><span class="brand">ESP_hole — Update</span>
    </header>
    <main>
      <div class="card">
        <h3>Update firmware or data</h3>
        <p class="hint">Upload an application <code>.bin</code> to update the firmware, or a data file (e.g. <code>AdBlocker.htm</code>, <code>common.js</code>) to replace a web file. Progress and result appear below.</p>
        <label class="filebtn" for="otafile">Choose file…</label>
        <input type="file" name="otafile" id="otafile" onchange="otaUploadFile()">
        <progress id="progressOta" value="0" max="100"></progress>
        <div id="status"></div>
        <div id="loaded_n_total"></div>
        <p style="margin-top:16px"><a class="link" href="javascript:history.back()">← Go back</a></p>
      </div>
    </main>
    
    <script>
      const defaultPort = window.location.protocol == 'http:' ? 80 : 443; 
      const webPort = !window.location.port ? defaultPort : window.location.port; // in case alternative ports specified
      const webServer = window.location.protocol + '//' + document.location.hostname + ':' + webPort;
      const $ = document.querySelector.bind(document);
   
      async function otaUploadFile() {
        // notify server to start ota 
        let file = $("#otafile").files[0];
        const response = await fetch('/control?startOTA=' + file.name);
        if (response.ok) {
          // submit file for uploading
          let xhr = new XMLHttpRequest();
          xhr.upload.addEventListener("progress", progressHandler, false);
          xhr.addEventListener("load", completeHandler, false);
          xhr.addEventListener("error", errorHandler, false);
          xhr.addEventListener("abort", abortHandler, false);
          xhr.open("POST", webServer +  '/upload');
          xhr.send(file);
        } else alert(response.status + ": " + response.statusText); 
      }

      function progressHandler(event) {
        $("#loaded_n_total").innerHTML = "Uploaded " + event.loaded + " of " + event.total + " bytes";
        let percent = (event.loaded / event.total) * 100;
        $("#progressOta").value = Math.round(percent);
        $("#status").innerHTML = Math.round(percent) + "% transferred";
        if (event.loaded  == event.total) $("#status").innerHTML = 'Uploaded, wait for completion result';
      }

      function completeHandler(event) {
        $("#status").innerHTML = event.target.responseText;
        $("#progressOta").value = 0;
      }

      function errorHandler(event) {
        $("#status").innerHTML = "Upload Failed";
        $("#progressOta").value = 0;
      }

      function abortHandler(event) {
        $("#status").innerHTML = "Upload Aborted";
        $("#progressOta").value = 0;
      }
    </script>
  </body>
</html>
)~";

const char* failPageS_html = R"~(
<!DOCTYPE html>
<html>
  <head>
    <title>Startup Failure</title>
    <script>
      function getLog() {
        fetch('/control?displayLog=1')
        .then(response => response.text())
        .then(logdata => { document.getElementById('appLog').innerText = logdata;})
        .catch(error => alert('Error fetching log:', error));
      }
    </script>
  </head>
  <body>
    <h2>
)~";

const char* failPageE_html = R"~(
    </h2>
    <h3><a href="#" onclick="getLog(); return false;"><button class=styled-button>Check log</button></a></h3>
    <h3><a href=/control?reset=1><button class=styled-button>Reboot ESP after fix</button></a></h3>
    <h3><a href=/web?OTA.htm><button class=styled-button>OTA Update</button></a></h3>
    <div id="appLog"></div>
  </body>
</html>
)~";
