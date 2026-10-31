#include "web_gateway.h"

WebGateway Gateway;

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Vision BMS Monitor</title>
    <style>
        :root {
            --bg: #0f172a; --card: #1e293b; --border: #334155;
            --text: #f8fafc; --muted: #94a3b8; --accent: #38bdf8;
            --green: #22c55e; --yellow: #eab308; --red: #ef4444;
        }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 20px; }
        .container { max-width: 1000px; margin: 0 auto; }
        .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); padding-bottom: 15px; margin-bottom: 20px; }
        .badge { background: #0369a1; color: white; padding: 4px 10px; border-radius: 999px; font-size: 12px; font-weight: 600; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin-bottom: 25px; }
        .card { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 18px; }
        .val { font-size: 28px; font-weight: 700; color: var(--accent); margin-top: 5px; }
        .sub { font-size: 13px; color: var(--muted); }
        .pack-card { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 20px; margin-bottom: 20px; }
        .pack-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }
        .cells-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(80px, 1fr)); gap: 8px; }
        .cell-box { background: #0f172a; border: 1px solid var(--border); border-radius: 8px; padding: 10px; text-align: center; }
        .cell-num { font-size: 11px; color: var(--muted); margin-bottom: 4px; }
        .cell-v { font-size: 15px; font-weight: 600; }
        .bal { color: var(--yellow); font-size: 10px; font-weight: bold; }
        .c-min { border-color: #3b82f6; }
        .c-max { border-color: #f97316; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <div>
                <h2 style="margin:0;">⚡ Vision BMS Multi-Pack Monitor</h2>
                <span class="sub">JC3248W535C Web Gateway</span>
            </div>
            <div><span class="badge" id="statusBadge">Опитування...</span></div>
        </div>

        <div class="grid">
            <div class="card"><div class="sub">Напруга банку</div><div class="val" id="totalV">-- V</div></div>
            <div class="card"><div class="sub">Струм системи</div><div class="val" id="totalI">-- A</div></div>
            <div class="card"><div class="sub">Потужність</div><div class="val" id="totalP">-- kW</div></div>
            <div class="card"><div class="sub">Середній SoC</div><div class="val" id="avgSoc">-- %</div></div>
        </div>

        <div id="packsContainer"></div>
    </div>

    <script>
        async function updateData() {
            try {
                const res = await fetch('/api/data');
                const data = await res.json();
                
                document.getElementById('statusBadge').innerText = data.demo ? 'Демо-режим (2 АКБ)' : 'RS485 Онлайн';
                document.getElementById('totalV').innerText = data.summary.voltage.toFixed(2) + ' V';
                document.getElementById('totalI').innerText = (data.summary.current >= 0 ? '+' : '') + data.summary.current.toFixed(1) + ' A';
                document.getElementById('totalP').innerText = data.summary.power.toFixed(2) + ' kW';
                document.getElementById('avgSoc').innerText = data.summary.soc.toFixed(0) + ' %';

                let html = '';
                data.packs.forEach((p, idx) => {
                    let cellsHtml = '';
                    p.cells.forEach((c, cIdx) => {
                        let isBal = (p.balance_mask & (1 << cIdx)) !== 0;
                        let cls = '';
                        if (c === p.min_cell) cls = 'c-min';
                        if (c === p.max_cell) cls = 'c-max';
                        cellsHtml += `
                            <div class="cell-box ${cls}">
                                <div class="cell-num">#${cIdx+1} ${isBal ? '<span class="bal">[BAL]</span>' : ''}</div>
                                <div class="cell-v">${(c / 1000).toFixed(3)}V</div>
                            </div>
                        `;
                    });

                    html += `
                        <div class="pack-card">
                            <div class="pack-header">
                                <h3 style="margin:0;">🔋 Акумулятор #${idx+1} (${p.status})</h3>
                                <span class="sub">U: ${p.voltage.toFixed(2)}V | I: ${p.current.toFixed(1)}A | SoC: ${p.soc}% | Δ: ${p.diff} мВ</span>
                            </div>
                            <div class="cells-grid">${cellsHtml}</div>
                        </div>
                    `;
                });
                document.getElementById('packsContainer').innerHTML = html;
            } catch (e) {
                console.error(e);
            }
        }
        setInterval(updateData, 1000);
        updateData();
    </script>
</body>
</html>
)rawliteral";

void WebGateway::begin(BatteryPack* packs, SemaphoreHandle_t mutex) {
    _packs = packs;
    _mutex = mutex;

    // Start WiFi in Station mode
    WiFi.mode(WIFI_STA);
    WiFi.begin(); // Will connect to saved credentials in NVS if any

    _server = new AsyncWebServer(80);
    setupRoutes();
    _server->begin();
}

void WebGateway::setupRoutes() {
    _server->on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", INDEX_HTML);
    });

    _server->on("/api/data", HTTP_GET, [this](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(4096);
        
        float totV = 0.0f, totI = 0.0f, totSoc = 0.0f;
        int count = 0;

        JsonArray packsArray = doc.createNestedArray("packs");

        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            for (int i = 0; i < MAX_BATTERIES; i++) {
                if (_packs[i].isOnline) {
                    count++;
                    totV = _packs[i].voltage; // Parallel pack voltage
                    totI += _packs[i].current;
                    totSoc += _packs[i].soc;

                    JsonObject pObj = packsArray.createNestedObject();
                    pObj["id"] = i + 1;
                    pObj["voltage"] = _packs[i].voltage;
                    pObj["current"] = _packs[i].current;
                    pObj["soc"] = _packs[i].soc;
                    pObj["soh"] = _packs[i].soh;
                    pObj["status"] = _packs[i].statusName;
                    pObj["diff"] = _packs[i].cellDiff;
                    pObj["min_cell"] = _packs[i].cellMin;
                    pObj["max_cell"] = _packs[i].cellMax;
                    pObj["balance_mask"] = _packs[i].balanceMask;

                    JsonArray cellsArr = pObj.createNestedArray("cells");
                    for (int c = 0; c < CELLS_PER_BATTERY; c++) {
                        cellsArr.add(_packs[i].cells[c]);
                    }
                }
            }
            xSemaphoreGive(_mutex);
        }

        JsonObject summary = doc.createNestedObject("summary");
        summary["voltage"] = totV;
        summary["current"] = totI;
        summary["power"] = (totV * totI) / 1000.0f;
        summary["soc"] = (count > 0) ? (totSoc / count) : 0.0f;
        summary["online"] = count;

        doc["demo"] = (count > 0 && _packs[0].isOnline && _packs[1].isOnline && !_packs[2].isOnline);

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });
}

String WebGateway::getIpAddress() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "Не підключено";
}

bool WebGateway::isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

