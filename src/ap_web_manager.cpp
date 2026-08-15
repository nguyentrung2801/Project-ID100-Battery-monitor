#include "ap_web_manager.h"
#include "config.h"
#include "record_manager.h"
#include "relay_manager.h"
#include "smo_simulator.h"
#include "telegram_manager.h"

#include <WiFi.h>
#include <WebServer.h>

extern WebServer server;
extern uint32_t getUartByteCounter();
extern uint32_t getUartFrameCounter();
extern uint32_t getUartFooterDropCounter();
extern uint32_t getUartChecksumDropCounter();
extern uint32_t getUartFilterDropCounter();
extern unsigned long getLastUartByteAgeMs();
extern unsigned long getLastUartFrameAgeMs();

void handleAPClearLogs();
void handleAPPress();
void handleAPCounts();
void handleAPSave();
void handleAPReset();
void handleSmoPage();
void handleSmoToggle();
void handleSmoStatus();
void handleSmoLogs();
void handleSmoClearLogs();
void handleSmoClearCycles();
void handleWifiInfoPage();
void handleAPStatus();
void handleSaveWifi();
void handleResetWifi();

//==================================================
// AP WEB PAGE
//==================================================
void handleAPRoot()
{
    Serial.println("[AP WEB] Root requested");
    String html = "";
html += "<script>";
html += "let lastTouchEnd=0;";
html += "document.addEventListener('touchend',function(event){";
html += "let now=(new Date()).getTime();";
html += "if(now-lastTouchEnd<=300){";
html += "event.preventDefault();";
html += "}";
html += "lastTouchEnd=now;";
html += "},false);";
html += "</script>";

html += "<!DOCTYPE html><html>";
html += "<head>";
html += "<meta charset='UTF-8'>";
html += "<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no'>";
html += "<title>Auto Click Control</title>";

html += "<style>";
html += "body{font-family:Arial,Helvetica,sans-serif;background:#f5f7fb;margin:0;padding:12px;color:#111827;}";
html += "h2{text-align:center;font-size:28px;margin:18px 0 24px;color:#111827;}";
html += ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;max-width:760px;margin:auto;}";
html += ".card{background:#fff;border:1px solid #e5e7eb;border-radius:18px;padding:14px;box-shadow:0 4px 14px rgba(0,0,0,0.10);}";
html += ".top{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:14px;}";
html += ".title{font-size:22px;font-weight:800;color:#0d47a1;line-height:1.15;}";
html += ".pin{font-size:14px;color:#64748b;margin-top:4px;font-weight:700;}";
html += ".run{font-size:12px;font-weight:800;color:#15803d;background:#dcfce7;padding:5px 9px;border-radius:999px;min-width:42px;text-align:center;}";
html += ".count-box{background:#eef3fb;border-radius:14px;padding:12px;margin-bottom:14px;}";
html += ".count-label{font-size:12px;color:#6b7280;font-weight:800;letter-spacing:0.5px;}";
html += ".cnt{font-size:34px;font-weight:900;color:#111827;margin-top:5px;}";
html += ".input-row{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:12px 0;}";
html += "label{font-size:13px;font-weight:800;color:#374151;}";
html += "input{width:100%;box-sizing:border-box;margin-top:5px;padding:11px;font-size:18px;text-align:center;border:1px solid #cbd5e1;border-radius:12px;background:white;}";
html += "button{width:100%;border:none;border-radius:12px;padding:12px;font-size:15px;font-weight:800;color:white;margin-top:9px;}";
html += ".manual{background:#2563eb;}";
html += ".save{background:#22c55e;}";
html += ".reset{background:#e11d48;}";
html += ".log{display:flex;justify-content:center;gap:14px;flex-wrap:wrap;margin:26px auto 18px;}";
html += ".log a{background:#374151;color:white;text-decoration:none;padding:13px 24px;border-radius:12px;display:inline-block;font-weight:800;min-width:180px;text-align:center;}";
html += "@media(max-width:720px){body{padding:10px;}h2{font-size:26px;margin:18px 0 20px;}.grid{grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;max-width:760px;}.card{padding:12px;border-radius:16px;}.top{margin-bottom:12px;}.title{font-size:22px;}.pin{font-size:15px;}.run{font-size:12px;padding:5px 9px;min-width:42px;}.count-box{padding:13px;margin-bottom:14px;}.count-label{font-size:12px;}.cnt{font-size:38px;}button{font-size:14px;padding:12px 6px;}.input-row{gap:9px;margin:13px 0;}label{font-size:13px;}input{font-size:20px;padding:10px 5px;}.log{gap:12px;}.log a{min-width:220px;}}";
html += "</style>";

html += "</head><body>";

html += "<h2>";
html += DEVICE_NAME;
#if ENABLE_RF_CONTROL
html += " Auto Click Control";
#else
html += " Error Logger";
#endif
html += "</h2>";

#if ENABLE_RF_CONTROL
html += "<div class='grid'>";
for (uint8_t i = 0; i < getRelayCountSize(); i++)
{
    html += "<div class='card'>";
    html += "<div class='top'>";
    html += "<div>";
    html += "<div class='title'>Button " + String(i + 1) + "</div>";
    html += "<div class='pin'>(PIN " + String(getRelayPin(i)) + ")</div>";
    html += "</div>";
    html += "<div class='run' id='run" + String(i) + "'></div>";
    html += "</div>";
    html += "<div class='count-box'>";
    html += "<div class='count-label'>COUNT</div>";
    html += "<div class='cnt' id='cnt" + String(i) + "'>" + String(getRelayCount(i)) + "</div>";
    html += "</div>";
    html += "<button class='manual' onclick='pressRelay(" + String(i) + ")'>MANUAL CLICK</button>";
    html += "<div class='input-row'>";
    html += "<label>Cycle (s)<input id='cyc" + String(i) + "' type='number' min='0' value='" + String(getRelayCycle(i)) + "'></label>";
    html += "<label>Limit<input id='lim" + String(i) + "' type='number' min='0' value='" + String(getRelayLimit(i)) + "'></label>";
    html += "</div>";
    html += "<button class='save' onclick='saveConfig(" + String(i) + ")'>SAVE CONFIG</button>";
    html += "<button class='reset' onclick='resetCount(" + String(i) + ")'>RESET COUNT</button>";
    html += "</div>";
}
html += "</div>";
#else
    html += "<div style='max-width:760px;margin:0 auto;background:white;border:1px solid #e5e7eb;border-radius:16px;padding:18px;box-shadow:0 4px 14px rgba(0,0,0,0.10);'>";
    html += "<div style='font-size:18px;font-weight:800;color:#111827;margin-bottom:8px;'>SMO Error Logger</div>";
    html += "<div style='font-size:14px;color:#64748b;font-weight:700;'>RF control and SMO simulator are disabled for this firmware.</div>";
    html += "</div>";
#endif

    html += "<div class='log'>";
#if ENABLE_SMO_SIMULATOR
    html += "<a href='/smo' style='background:#0f766e;'>SMO SIMULATOR</a>";
#endif
    html += "<a href='/wifiinfo' style='background:#0f766e;'>WIFI SETTINGS</a>";
    html += "<a href='/aplogs'>VIEW ERROR LOG</a>";
    html += "</div>";

#if ENABLE_RF_CONTROL
    html += "<script>";

    html += "function pressRelay(id){";
    html += "fetch('/appress?id='+id).then(()=>updateCounts());";
    html += "}";

    html += "function saveConfig(id){";
    html += "let cycle=document.getElementById('cyc'+id).value;";
    html += "let limit=document.getElementById('lim'+id).value;";
    html += "fetch('/apsave?id='+id+'&cycle='+cycle+'&limit='+limit).then(()=>updateCounts());";
    html += "}";

    html += "function resetCount(id){";
    html += "fetch('/apreset?id='+id).then(()=>updateCounts());";
    html += "}";

    html += "function updateCounts(){";
    html += "fetch('/apcounts').then(r=>r.json()).then(data=>{";
    html += "for(let i=0;i<data.length;i++){";
    html += "document.getElementById('cnt'+i).innerText=data[i].count;";
    html += "let st=document.getElementById('run'+i);";
    html += "if(data[i].full){st.innerText='FULL';st.style.color='#dc2626';}";
    html += "else if(data[i].pressed){st.innerText='OK';st.style.color='#16a34a';}";
    html += "else{st.innerText='WAIT';st.style.color='#f59e0b';}";
    html += "}";
    html += "});";
    html += "}";

    html += "setInterval(updateCounts,1000);";
    html += "updateCounts();";

    html += "</script>";
#endif

    html += "</body></html>";

    server.send(200, "text/html", html);
}

//==================================================
// SMO SIMULATOR PAGE
//==================================================
void handleSmoPage()
{
    String html = "";

    html += "<!DOCTYPE html><html>";
    html += "<head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no'>";
    html += "<title>SMO Simulator</title>";

    html += "<style>";
    html += "body{font-family:Arial,Helvetica,sans-serif;background:#f5f7fb;margin:0;padding:14px;color:#111827;}";
    html += "h2{text-align:center;font-size:26px;margin:18px 0 18px;color:#111827;}";
    html += ".back{display:inline-block;text-decoration:none;background:#2563eb;color:white;padding:10px 18px;border-radius:8px;font-weight:800;}";
    html += ".panel{max-width:900px;margin:0 auto;background:#fff;border:1px solid #e5e7eb;border-radius:12px;padding:14px;box-shadow:0 4px 14px rgba(0,0,0,0.08);}";
    html += ".status{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-bottom:12px;}";
    html += ".box{background:#eef3fb;border-radius:10px;padding:10px;min-height:52px;}";
    html += ".label{font-size:11px;color:#64748b;font-weight:800;}";
    html += ".value{font-size:15px;color:#111827;font-weight:900;margin-top:4px;word-break:break-word;}";
    html += ".actions{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin:12px 0;}";
    html += "button{width:100%;border:none;border-radius:10px;padding:12px;font-size:14px;font-weight:900;color:white;}";
    html += ".toggle{background:#0f766e;}";
    html += ".clear{background:#d32f2f;}";
    html += "table{width:100%;border-collapse:collapse;background:white;margin-top:12px;}";
    html += "th,td{border-bottom:1px solid #e5e7eb;padding:8px;text-align:left;font-size:13px;vertical-align:top;}";
    html += "th{background:#263238;color:white;}";
    html += "code{font-family:Consolas,monospace;font-size:12px;word-break:break-word;}";
    html += ".empty{text-align:center;color:#64748b;font-weight:800;padding:18px;}";
    html += "@media(max-width:700px){.status,.actions{grid-template-columns:1fr 1fr;}th,td{font-size:12px;}}";
    html += "</style>";

    html += "</head><body>";
    html += "<a class='back' href='/ap'>Back</a>";
    html += "<h2>SMO - SB001 Simulator</h2>";

    html += "<div class='panel'>";
    html += "<div class='status'>";
    html += "<div class='box'><div class='label'>RUN</div><div class='value' id='smoRun'>--</div></div>";
    html += "<div class='box'><div class='label'>STATE</div><div class='value' id='smoState'>--</div></div>";
    html += "<div class='box'><div class='label'>LAST TX</div><div class='value' id='smoCmd'>--</div></div>";
    html += "<div class='box'><div class='label'>CYCLES</div><div class='value' id='smoCycles'>0</div></div>";
    html += "</div>";
    html += "<div class='actions'>";
    html += "<button class='toggle' onclick='toggleSmo()'>START / STOP SIMULATION</button>";
    html += "<button class='toggle' onclick='clearSmoCycles()'>CLEAR CYCLES</button>";
    html += "<button class='clear' onclick='clearSmoLogs()'>CLEAR ERROR SUMMARY</button>";
    html += "</div>";
    html += "<table>";
    html += "<thead><tr><th>Error</th><th>Frame</th><th>Count</th></tr></thead>";
    html += "<tbody id='smoLogRows'><tr><td class='empty' colspan='3'>Loading...</td></tr></tbody>";
    html += "</table>";
    html += "</div>";

    html += "<script>";
    html += "function toggleSmo(){fetch('/smotoggle').then(()=>updateSmo());}";
    html += "function clearSmoLogs(){fetch('/smoclearlogs').then(()=>updateSmo());}";
    html += "function clearSmoCycles(){fetch('/smoclearcycles').then(()=>updateSmo());}";
    html += "function esc(v){return String(v||'').replace(/[&<>\\\"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','\\\"':'&quot;'}[c];});}";
    html += "function updateSmo(){";
    html += "fetch('/smostatus').then(r=>r.json()).then(s=>{";
    html += "document.getElementById('smoRun').innerText=s.running?'RUNNING':'STOPPED';";
    html += "document.getElementById('smoRun').style.color=s.running?'#15803d':'#dc2626';";
    html += "document.getElementById('smoState').innerText=s.state;";
    html += "document.getElementById('smoCmd').innerText=s.lastCommand;";
    html += "document.getElementById('smoCycles').innerText=s.cycles;";
    html += "});";
    html += "fetch('/smologs').then(r=>r.json()).then(logs=>{";
    html += "let rows='';";
    html += "for(let i=0;i<logs.length;i++){";
    html += "rows+='<tr><td>'+esc(logs[i].event)+'</td><td><code>'+esc(logs[i].frame)+'</code></td><td>'+logs[i].count+'</td></tr>';";
    html += "}";
    html += "if(rows===''){rows='<tr><td class=\"empty\" colspan=\"3\">No simulator errors</td></tr>';}";
    html += "document.getElementById('smoLogRows').innerHTML=rows;";
    html += "});";
    html += "}";
    html += "setInterval(updateSmo,1000);";
    html += "updateSmo();";
    html += "</script>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleAPPress()
{
#if !ENABLE_RF_CONTROL
    server.send(403, "text/plain", "RF control disabled");
    return;
#endif
    if (!server.hasArg("id"))
    {
        server.send(400, "text/plain", "Missing id");
        return;
    }

    uint8_t id = server.arg("id").toInt();
    if (id >= getRelayCountSize())
    {
        server.send(400, "text/plain", "Invalid id");
        return;
    }

    manualPressRelay(id);
    server.send(200, "text/plain", "OK");
}

void handleAPCounts()
{
    String json = "[";

    for (uint8_t i = 0; i < getRelayCountSize(); i++)
    {
        if (i > 0)
            json += ",";

        json += "{";
        json += "\"count\":" + String(getRelayCount(i)) + ",";
        json += "\"cycle\":" + String(getRelayCycle(i)) + ",";
        json += "\"limit\":" + String(getRelayLimit(i)) + ",";
        json += "\"running\":";
        json += isRelayAutoRunning(i) ? "true" : "false";
        json += ",";
        json += "\"pressed\":";
        json += isRelayRecentlyPressed(i) ? "true" : "false";
        json += ",";
        json += "\"full\":";
        json += isRelayFull(i) ? "true" : "false";
        json += "}";
    }

    json += "]";
    server.send(200, "application/json", json);
}

void handleAPSave()
{
#if !ENABLE_RF_CONTROL
    server.send(403, "text/plain", "RF control disabled");
    return;
#endif
    if (!server.hasArg("id") || !server.hasArg("cycle") || !server.hasArg("limit"))
    {
        server.send(400, "text/plain", "Missing parameter");
        return;
    }

    uint8_t id = server.arg("id").toInt();
    uint32_t cycle = server.arg("cycle").toInt();
    uint32_t limit = server.arg("limit").toInt();

    if (id >= getRelayCountSize())
    {
        server.send(400, "text/plain", "Invalid id");
        return;
    }

    setRelayConfig(id, cycle, limit);
    server.send(200, "text/plain", "OK");
}

void handleAPReset()
{
#if !ENABLE_RF_CONTROL
    server.send(403, "text/plain", "RF control disabled");
    return;
#endif
    if (!server.hasArg("id"))
    {
        server.send(400, "text/plain", "Missing id");
        return;
    }

    uint8_t id = server.arg("id").toInt();
    if (id >= getRelayCountSize())
    {
        server.send(400, "text/plain", "Invalid id");
        return;
    }

    resetRelayCount(id);
    server.send(200, "text/plain", "OK");
}

//==================================================
// ERROR LOG PAGE
//==================================================
void handleAPLogs()
{
    String html = "";

    html += "<!DOCTYPE html><html>";
    html += "<head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no'>";
    html += "<title>SMO Error Log</title>";

    html += "<style>";
    html += "body{font-family:Arial;background:#f4f6f8;padding:20px;}";
    html += "h2{text-align:center;color:#333;}";
    html += "table{width:100%;border-collapse:collapse;background:white;box-shadow:0 3px 10px rgba(0,0,0,0.12);}";
    html += "th,td{border:1px solid #ccc;padding:8px;text-align:left;font-size:14px;}";
    html += "th{background:#263238;color:white;}";
    html += "tr:nth-child(even){background:#f2f2f2;}";
    html += "code{font-size:13px;}";
    html += ".back{display:inline-block;margin-bottom:15px;text-decoration:none;background:#1976d2;color:white;padding:10px 18px;border-radius:8px;}";
    html += "</style>";

    html += "</head><body>";

    html += "<a class='back' href='/ap'>← Back</a>";
    html += "<a class='back' style='background:#d32f2f;margin-left:10px;' href='/apclearlogs'>Clear Log</a>";

    html += "<h2>SMO Error Log</h2>";

    html += "<p>Stored Records: ";
    html += String(getRecordCount());
    html += "/50</p>";

    html += "<table>";
    html += "<tr>";
    html += "<th>No.</th>";
    html += "<th>Source</th>";
    html += "<th>Time</th>";
    html += "<th>Raw Frame</th>";
    html += "<th>Message</th>";
    html += "</tr>";

    for (int i = getRecordCount() - 1; i >= 0; i--)
    {
        html += "<tr>";

        html += "<td>" + String(i + 1) + "</td>";
        html += "<td>" + getRecordSource(i) + "</td>";
        html += "<td>" + getRecordTimestamp(i) + "</td>";
        html += "<td><code>" + getRecordRawFrame(i) + "</code></td>";
        html += "<td>" + getRecordMessage(i) + "</td>";

        html += "</tr>";
    }

    html += "</table>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleWifiInfoPage()
{
    String html = "";
    html += "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no'>";
    html += "<title>WiFi Settings</title>";
    html += "<style>";
    html += "body{font-family:Arial,Helvetica,sans-serif;background:#f5f7fb;margin:0;padding:14px;color:#111827;}";
    html += "h2{text-align:center;font-size:28px;margin:18px 0 24px;color:#111827;}";
    html += ".back{display:inline-block;text-decoration:none;background:#2563eb;color:white;padding:10px 18px;border-radius:8px;font-weight:800;}";
    html += ".panel{max-width:760px;margin:18px auto;background:white;border:1px solid #e5e7eb;border-radius:16px;padding:18px;box-shadow:0 4px 14px rgba(0,0,0,0.10);}";
    html += ".title{font-size:20px;font-weight:900;color:#111827;margin-bottom:12px;}";
    html += ".info{background:#eef3fb;border-radius:12px;padding:14px;font-size:16px;color:#111827;font-weight:800;line-height:1.8;}";
    html += "label{display:block;margin-top:12px;font-size:14px;font-weight:800;color:#374151;}";
    html += "input{width:100%;box-sizing:border-box;margin-top:5px;padding:12px;font-size:16px;border:1px solid #cbd5e1;border-radius:10px;}";
    html += "button{width:100%;border:none;border-radius:12px;padding:13px;font-size:15px;font-weight:900;color:white;margin-top:14px;background:#d32f2f;}";
    html += ".connect{background:#16a34a;}";
    html += ".note{font-size:13px;color:#64748b;margin-top:10px;line-height:1.5;}";
    html += "</style></head><body>";
    html += "<a class='back' href='/ap'>Back</a>";
    html += "<h2>";
    html += DEVICE_NAME;
    html += " WiFi Settings</h2>";
    html += "<div class='panel'>";
    html += "<div class='title'>Network Status</div>";
    html += "<div class='info'>";
    html += "<div>STA WiFi: <span id='staState'>--</span></div>";
    html += "<div>STA SSID: <span id='staSSID'>--</span></div>";
    html += "<div>STA IP: <span id='staIP'>--</span></div>";
    html += "<div>RSSI: <span id='staRSSI'>--</span></div>";
    html += "<div>Telegram Chat: <span id='tgChat'>--</span></div>";
    html += "<div>Telegram Send: <span id='tgSend'>--</span></div>";
    html += "</div>";
    html += "<div class='title' style='margin-top:20px;'>Router Connection</div>";
    html += "<label>WiFi SSID<input id='wifiSSID' type='text' maxlength='32' autocomplete='off'></label>";
    html += "<label>WiFi Password<input id='wifiPassword' type='password' maxlength='64' autocomplete='new-password'></label>";
    html += "<button class='connect' onclick='saveWifi()'>SAVE AND CONNECT</button>";
    html += "<div class='note'>The local JIG access point remains available while the ESP32 connects to this router.</div>";
    html += "<button onclick='resetWifi()'>RESET WIFI SETTINGS</button>";
    html += "</div>";
    html += "<script>";
    html += "function updateAPStatus(){fetch('/apstatus').then(r=>r.json()).then(s=>{";
    html += "let e=document.getElementById('staState');if(e){e.innerText=s.staConnected?'CONNECTED':'DISCONNECTED';e.style.color=s.staConnected?'#15803d':'#dc2626';}";
    html += "e=document.getElementById('staSSID');if(e)e.innerText=s.staSSID||'-';";
    html += "e=document.getElementById('staIP');if(e)e.innerText=s.staIP||'-';";
    html += "e=document.getElementById('staRSSI');if(e)e.innerText=s.rssi+' dBm';";
    html += "e=document.getElementById('tgChat');if(e)e.innerText=s.chatId;";
    html += "e=document.getElementById('tgSend');if(e)e.innerText=s.telegramSendStatus;";
    html += "}).catch(()=>{});}";
    html += "function saveWifi(){";
    html += "const ssid=document.getElementById('wifiSSID').value.trim();";
    html += "const password=document.getElementById('wifiPassword').value;";
    html += "if(!ssid){alert('Enter the router WiFi SSID.');return;}";
    html += "fetch('/savewifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(password)})";
    html += ".then(r=>r.text()).then(t=>alert(t)).catch(()=>alert('Failed to save WiFi settings.'));";
    html += "}";
    html += "function resetWifi(){if(confirm('Erase the saved router WiFi settings?')){fetch('/resetwifi').then(()=>alert('Device restarting. Reconnect to the JIG AP and open http://4.4.4.4/ap'));}}";
    html += "setInterval(updateAPStatus,2000);updateAPStatus();";
    html += "</script></body></html>";

    server.send(200, "text/html", html);
}
//==================================================
// SETUP AP WEB SERVER
//==================================================
void setupAPWebServer()
{
    IPAddress apIP(4, 4, 4, 4);
    IPAddress gateway(4, 4, 4, 4);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.mode(WIFI_AP_STA);

    if (!WiFi.softAPConfig(apIP, gateway, subnet))
    {
        Serial.println("[AP WEB] Failed to configure AP network");
    }

    if (!WiFi.softAP(AP_SSID, AP_PASS))
    {
        Serial.println("[AP WEB] Failed to start access point");
    }

    server.on("/ap", handleAPRoot);
    server.on("/wifiinfo", handleWifiInfoPage);
    server.on("/apstatus", handleAPStatus);
    server.on("/savewifi", HTTP_POST, handleSaveWifi);
    server.on("/resetwifi", handleResetWifi);
#if ENABLE_RF_CONTROL
    server.on("/appress", handleAPPress);
    server.on("/apcounts", handleAPCounts);
#endif
    server.on("/aplogs", handleAPLogs);
#if ENABLE_SMO_SIMULATOR
    server.on("/smo", handleSmoPage);
#endif
#if ENABLE_RF_CONTROL
    server.on("/apsave", handleAPSave);
    server.on("/apreset", handleAPReset);
#endif
    server.on("/apclearlogs", handleAPClearLogs);
#if ENABLE_SMO_SIMULATOR
    server.on("/smotoggle", handleSmoToggle);
    server.on("/smostatus", handleSmoStatus);
    server.on("/smologs", handleSmoLogs);
    server.on("/smoclearlogs", handleSmoClearLogs);
    server.on("/smoclearcycles", handleSmoClearCycles);
#endif

    Serial.println("[AP WEB] Started");
    Serial.print("[AP WEB] SSID: ");
    Serial.println(AP_SSID);
    Serial.println("[AP WEB] URL : http://4.4.4.4/ap");
}

//==================================================
// HANDLE AP WEB CLIENT
//==================================================

const char *getAPSSID()
{
    return AP_SSID;
}

const char *getAPPassword()
{
    return AP_PASS;
}

String getAPURL()
{
    return "http://4.4.4.4/ap";
}

void handleAPClearLogs()
{
    clearRecords();

    server.sendHeader("Location", "/aplogs");
    server.send(303);
}

static String jsonEscape(String value)
{
    value.replace("\\", "\\\\");
    value.replace("\"", "\\\"");
    value.replace("\n", "\\n");
    value.replace("\r", "");
    return value;
}

void handleSmoToggle()
{
    toggleSmoSimulator();
    server.send(200, "text/plain", "OK");
}

void handleSmoStatus()
{
    String json = "{";
    json += "\"running\":";
    json += isSmoSimulatorRunning() ? "true" : "false";
    json += ",\"state\":\"" + jsonEscape(getSmoSimulatorState()) + "\"";
    json += ",\"lastCommand\":\"" + jsonEscape(getSmoLastCommand()) + "\"";
    json += ",\"cycles\":" + String(getSmoCycleCount());
    json += ",\"logCount\":" + String(getSbErrorSummaryCount());
    json += "}";

    server.send(200, "application/json", json);
}

void handleSmoLogs()
{
    String json = "[";

    for (uint8_t i = 0; i < getSbErrorSummaryCount(); i++)
    {
        if (i > 0)
            json += ",";

        SbErrorSummary entry = getSbErrorSummary(i);
        json += "{";
        json += "\"event\":\"" + jsonEscape(entry.event) + "\"";
        json += ",\"frame\":\"" + jsonEscape(entry.frame) + "\"";
        json += ",\"count\":" + String(entry.count);
        json += "}";
    }

    json += "]";

    server.send(200, "application/json", json);
}

void handleSmoClearLogs()
{
    clearSbLogs();
    server.send(200, "text/plain", "OK");
}

void handleSmoClearCycles()
{
    clearSmoCycleCount();
    server.send(200, "text/plain", "OK");
}

void handleAPStatus()
{
    String json = "{";
    json += "\"device\":\"" + String(DEVICE_NAME) + "\"";
    json += ",\"staConnected\":";
    json += (WiFi.status() == WL_CONNECTED) ? "true" : "false";
    json += ",\"staSSID\":\"" + jsonEscape(WiFi.SSID()) + "\"";
    json += ",\"staIP\":\"" + WiFi.localIP().toString() + "\"";
    json += ",\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
    json += ",\"apSSID\":\"" + String(AP_SSID) + "\"";
    json += ",\"chatId\":\"" + String(CHAT_ID) + "\"";
    json += ",\"commandSuffix\":\"" + String(TELEGRAM_COMMAND_SUFFIX) + "\"";
    json += ",\"telegramSendStatus\":\"" + jsonEscape(getLastTelegramSendStatus()) + "\"";
    json += ",\"uartBytes\":" + String(getUartByteCounter());
    json += ",\"uartFrames\":" + String(getUartFrameCounter());
    json += ",\"uartFooterDrops\":" + String(getUartFooterDropCounter());
    json += ",\"uartChecksumDrops\":" + String(getUartChecksumDropCounter());
    json += ",\"uartFilterDrops\":" + String(getUartFilterDropCounter());
    json += ",\"lastUartByteAgeMs\":" + String(getLastUartByteAgeMs());
    json += ",\"lastUartFrameAgeMs\":" + String(getLastUartFrameAgeMs());
    json += "}";

    server.send(200, "application/json", json);
}

void handleResetWifi()
{
    server.send(200, "text/plain", "RESETTING_WIFI");
    delay(300);
    WiFi.disconnect(true, true);
    delay(500);
    ESP.restart();
}

void handleSaveWifi()
{
    if (!server.hasArg("ssid"))
    {
        server.send(400, "text/plain", "Missing WiFi SSID");
        return;
    }

    String ssid = server.arg("ssid");
    String password = server.hasArg("password") ? server.arg("password") : "";
    ssid.trim();

    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64)
    {
        server.send(400, "text/plain", "Invalid WiFi credentials");
        return;
    }

    // WiFi.begin stores the station credentials while WIFI_AP_STA keeps the
    // local JIG network and Web interface running during the connection attempt.
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    server.send(200, "text/plain", "WiFi settings saved. Connecting in background; the JIG AP remains available.");
}
