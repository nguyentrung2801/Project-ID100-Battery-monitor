#include "web_manager.h"
#include "record_manager.h"

#include <Arduino.h>
#include <WebServer.h>

WebServer server(80);

void handleRoot()
{
    String html = "";

    html += "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>GDO Data Monitor</title>";

    html += "<style>";
    html += "body{font-family:Arial,Helvetica,sans-serif;background:#f4f6f8;margin:0;color:#222;}";
    html += ".header{background:#1f2937;color:white;padding:18px 24px;}";
    html += ".header h1{margin:0;font-size:24px;}";
    html += ".header p{margin:6px 0 0;color:#d1d5db;font-size:14px;}";
    html += ".container{padding:20px;}";
    html += ".cards{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:18px;}";
    html += ".card{background:white;border-radius:10px;padding:14px 18px;box-shadow:0 2px 8px rgba(0,0,0,0.08);min-width:160px;}";
    html += ".card-title{font-size:13px;color:#6b7280;margin-bottom:6px;}";
    html += ".card-value{font-size:22px;font-weight:bold;color:#111827;}";
    html += ".table-wrap{background:white;border-radius:10px;box-shadow:0 2px 8px rgba(0,0,0,0.08);overflow:auto;}";
    html += "table{border-collapse:collapse;width:100%;min-width:760px;}";
    html += "th{background:#2563eb;color:white;text-align:left;padding:12px;font-size:14px;position:sticky;top:0;}";
    html += "td{padding:10px 12px;border-bottom:1px solid #e5e7eb;font-size:14px;vertical-align:top;}";
    html += "tr:nth-child(even){background:#f9fafb;}";
    html += "tr:hover{background:#eef4ff;}";
    html += ".no{font-weight:bold;color:#374151;}";
    html += ".time{white-space:nowrap;color:#111827;}";
    html += ".raw{font-family:Consolas,monospace;font-size:13px;color:#374151;white-space:nowrap;}";
    html += ".msg{font-weight:500;}";
    html += ".error{color:#dc2626;font-weight:bold;}";
    html += ".normal{color:#059669;font-weight:bold;}";
    html += ".footer{font-size:12px;color:#6b7280;margin-top:12px;}";
    html += "@media(max-width:600px){.container{padding:10px}.header h1{font-size:20px}.card{width:100%;}.raw{font-size:12px}}";
    html += ".run{color:green;font-weight:bold;font-size:18px;min-height:22px;text-align:right;}";
    html += "label{font-size:14px;font-weight:bold;text-align:left;display:block;}";
    html += "label input{margin-top:4px;}";
    html += ".auto{background:#ff9800;}";
    html += "</style>";

    html += "</head><body>";

    html += "<div class='header'>";
    html += "<h1>GDO Frame Data Monitor</h1>";
    html += "<p>ESP32-C3 Web Dashboard | Real-time UART Frame Monitoring</p>";
    html += "</div>";

    html += "<div class='container'>";

    html += "<div class='cards'>";
    html += "<div class='card'><div class='card-title'>Stored Records</div><div class='card-value'>" + String(recordCount) + "/50</div></div>";
    html += "<div class='card'><div class='card-title'>Status</div><div id='device-status' class='card-value'>Checking...</div></div>";
    html += "</div>";

    // Reserved clear button; the endpoint remains available for existing clients.
    // html += "<form action='/clear' method='POST' style='margin-bottom:16px;'>";
    // html += "<button type='submit'>Clear Records</button>";
    // html += "</form>";

    html += "<div class='table-wrap'>";
    html += "<table>";
    html += "<tr>";
    html += "<th>No.</th>";
    html += "<th>Time</th>";
    html += "<th>Raw Frame</th>";
    html += "<th>Message</th>";
    html += "</tr>";

    for (int i = recordCount - 1; i >= 0; i--)
    {
        String msgClass = records[i].message.indexOf("Error") >= 0 ||
                          records[i].message.indexOf("Blocked") >= 0 ||
                          records[i].message.indexOf("Fault") >= 0
                              ? "error"
                              : "normal";

        html += "<tr>";
        html += "<td class='no'>" + String(i + 1) + "</td>";
        html += "<td class='time'>" + records[i].timestamp + "</td>";
        html += "<td class='raw'>" + records[i].rawFrame + "</td>";
        html += "<td class='msg " + msgClass + "'>" + records[i].message + "</td>";
        html += "</tr>";
    }

    html += "</table>";
    html += "</div>";

    html += "<div class='footer'>";
    html += "The table stores the latest 50 records. When full, the oldest record is removed using FIFO Shift.";
    html += "</div>";

    html += "</div>";

    // Reload only when the device returns online or the record version changes.
    html += "<script>";
    html += "let lastVersion=-1;";
    html += "let offlineCount=0;";
    html += "let wasOffline=false;";

    html += "function setOnline(){";
    html += "  const el=document.getElementById('device-status');";
    html += "  if(el){el.innerHTML='Online';el.className='card-value normal';}";
    html += "}";

    html += "function setOffline(){";
    html += "  const el=document.getElementById('device-status');";
    html += "  if(el){el.innerHTML='Offline';el.className='card-value error';}";
    html += "}";

    html += "async function checkStatus(){";
    html += "  try{";
    html += "    const controller=new AbortController();";
    html += "    const timeout=setTimeout(()=>controller.abort(),1500);";
    html += "    const res=await fetch('/status?ts='+Date.now(),{cache:'no-store',signal:controller.signal});";
    html += "    clearTimeout(timeout);";

    html += "    if(!res.ok){throw new Error('status failed');}";

    html += "    const data=await res.json();";
    html += "    offlineCount=0;";
    html += "    setOnline();";

    html += "    if(wasOffline){location.reload();return;}";
    html += "    if(lastVersion!==-1 && data.version!==lastVersion){location.reload();return;}";

    html += "    lastVersion=data.version;";
    html += "  }catch(e){";
    html += "    offlineCount++;";
    html += "    if(offlineCount>=3){";
    html += "      wasOffline=true;";
    html += "      setOffline();";
    html += "    }";
    html += "  }";
    html += "}";

    html += "checkStatus();";
    html += "setInterval(checkStatus,1000);";
    html += "</script>";

    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleStatus()
{
    String json = "{";
    json += "\"online\":true,";
    json += "\"version\":" + String(recordVersion) + ",";
    json += "\"count\":" + String(recordCount);
    json += "}";

    server.send(200, "application/json", json);
}

void handleVersion()
{
    server.send(200, "text/plain", String(recordVersion));
}

void handleClearRecords()
{
    clearRecords();

    server.sendHeader("Location", "/");
    server.send(303);
}

void setupWebServer()
{
    server.on("/", handleRoot);
    server.on("/status", handleStatus);
    server.on("/version", handleVersion);
    server.on("/clear", handleClearRecords);

    server.begin();

    Serial.println("[WEB] Server started");
}

void handleWebClient()
{
    server.handleClient();
}
