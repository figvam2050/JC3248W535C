#include "web_gateway.h"

WebGateway Gateway;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
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
        .btn { background: #2563eb; color: white; text-decoration: none; padding: 6px 14px; border-radius: 8px; font-size: 13px; font-weight: 500; }
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
        .bal { color: #f472b6; font-size: 10px; font-weight: bold; }
        .c-min { border-color: #3b82f6; }
        .c-max { border-color: #f59e0b; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <div>
                <h2 style="margin:0;">⚡ Vision BMS Multi-Pack Monitor</h2>
                <span class="sub">JC3248W535C Web Gateway</span>
            </div>
            <div style="display:flex; gap:10px; align-items:center;">
                <span class="badge" id="statusBadge">Опитування...</span>
                <a href="/wifi" class="btn">⚙ Wi-Fi</a>
            </div>
        </div>

        <div class="grid">
            <div class="card"><div class="sub">Напруга банку</div><div class="val" id="totalV">-- V</div></div>
            <div class="card"><div class="sub">Струм системи</div><div class="val" id="totalI">-- A</div></div>
            <div class="card"><div class="sub">Потужність</div><div class="val" id="totalPwr">-- kW</div></div>
            <div class="card"><div class="sub">Середній SoC</div><div class="val" id="avgSoc">-- %</div></div>
        </div>

        <div id="packsContainer"></div>
    </div>

    <script>
        async function updateData() {
            try {
                const res = await fetch('/api/data');
                const data = await res.json();
                
                document.getElementById('statusBadge').innerText = data.demo ? 'ДЕМО РЕЖИМ (2 АКБ)' : 'ONLINE (RS485)';
                document.getElementById('statusBadge').style.background = data.demo ? '#d97706' : '#16a34a';

                document.getElementById('totalV').innerText = data.summary.voltage.toFixed(2) + ' V';
                document.getElementById('totalI').innerText = (data.summary.current >= 0 ? '+' : '') + data.summary.current.toFixed(1) + ' A';
                document.getElementById('totalPwr').innerText = data.summary.power.toFixed(2) + ' kW';
                document.getElementById('avgSoc').innerText = data.summary.soc.toFixed(0) + ' %';

                const container = document.getElementById('packsContainer');
                container.innerHTML = '';

                data.packs.forEach(p => {
                    const card = document.createElement('div');
                    card.className = 'pack-card';

                    let cellsHtml = '';
                    p.cells.forEach((v, idx) => {
                        let cClass = '';
                        if (v === p.min_cell && p.max_cell > p.min_cell) cClass = 'c-min';
                        if (v === p.max_cell && p.max_cell > p.min_cell) cClass = 'c-max';
                        
                        const isBal = (p.balance_mask & (1 << idx)) !== 0;
                        cellsHtml += `
                            <div class="cell-box ${cClass}">
                                <div class="cell-num">C${idx + 1}</div>
                                <div class="cell-v">${v}</div>
                                ${isBal ? '<div class="bal">[BAL]</div>' : ''}
                            </div>
                        `;
                    });

                    card.innerHTML = `
                        <div class="pack-header">
                            <div>
                                <h3 style="margin:0;">Батарея #${p.id}</h3>
                                <span class="sub">${p.status} | Дельта: ${p.diff} mV | T: ${p.temp_avg || 25}°C</span>
                            </div>
                            <div style="text-align:right;">
                                <div style="font-size:18px; font-weight:bold; color:var(--accent);">${p.voltage.toFixed(2)} V | ${p.current.toFixed(1)} A</div>
                                <div class="sub">SoC: ${p.soc}%</div>
                            </div>
                        </div>
                        <div class="cells-grid">
                            ${cellsHtml}
                        </div>
                    `;
                    container.appendChild(card);
                });
            } catch(e) {
                console.error(e);
            }
        }
        setInterval(updateData, 1000);
        updateData();
    </script>
</body>
</html>
)rawliteral";

static const char WIFI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Налаштування Wi-Fi</title>
    <style>
        body { font-family: -apple-system, sans-serif; background: #0f172a; color: #f8fafc; margin: 0; padding: 20px; display: flex; justify-content: center; }
        .box { background: #1e293b; border: 1px solid #334155; border-radius: 12px; padding: 25px; width: 100%; max-width: 400px; }
        h2 { margin-top: 0; color: #38bdf8; }
        input[type=text], input[type=password] { width: 100%; padding: 10px; margin: 8px 0 16px; border: 1px solid #334155; border-radius: 6px; background: #0f172a; color: #fff; box-sizing: border-box; }
        input[type=submit] { width: 100%; padding: 12px; border: none; border-radius: 6px; background: #2563eb; color: #fff; font-size: 16px; font-weight: bold; cursor: pointer; }
        .back { display: block; text-align: center; margin-top: 15px; color: #94a3b8; text-decoration: none; }
    </style>
</head>
<body>
    <div class="box">
        <h2>Налаштування Wi-Fi</h2>
        <form action="/save_wifi" method="POST">
            <label>Назва мережі (SSID):</label>
            <input type="text" name="ssid" required placeholder="Введіть SSID роутера">
            <label>Пароль:</label>
            <input type="password" name="pass" placeholder="Введіть пароль">
            <input type="submit" value="Зберегти та підключитися">
        </form>
        <a href="/" class="back">← Назад до монітора</a>
    </div>
</body>
</html>
)rawliteral";

void WebGateway::begin(BatteryPack* packs, SemaphoreHandle_t mutex) {
    _packs = packs;
    _mutex = mutex;

    _prefs.begin("bms_wifi", false);
    _ssid = _prefs.getString("ssid", "");
    _pass = _prefs.getString("pass", "");

    if (_ssid.length() > 0) {
        Serial.printf("[Wi-Fi] Saved credentials found. Connecting to '%s'...\n", _ssid.c_str());
        tryConnectSTA();
    } else {
        Serial.println("[Wi-Fi] No credentials saved. Starting AP Setup Mode...");
        startAP();
    }

    _server = new AsyncWebServer(80);
    setupRoutes();
    _server->begin();
}

void WebGateway::tryConnectSTA() {
    _apMode = false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid.c_str(), _pass.c_str());

    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 8000) {
        delay(200);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[Wi-Fi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[Wi-Fi] Failed to connect to saved network. Fallback to AP Mode!");
        startAP();
    }
}

void WebGateway::startAP() {
    _apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("BMS-Display-AP", "12345678");
    Serial.printf("[Wi-Fi] AP Started: 'BMS-Display-AP' (pass: 12345678), IP: %s\n", WiFi.softAPIP().toString().c_str());

    if (!_dnsServer) {
        _dnsServer = new DNSServer();
    }
    _dnsServer->start(53, "*", WiFi.softAPIP());
}

void WebGateway::loop() {
    if (_apMode && _dnsServer) {
        _dnsServer->processNextRequest();
    }
}

void WebGateway::setupRoutes() {
    _server->on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", INDEX_HTML);
    });

    _server->on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", WIFI_HTML);
    });

    _server->on("/save_wifi", HTTP_POST, [this](AsyncWebServerRequest *request){
        String new_ssid = "";
        String new_pass = "";
        if (request->hasParam("ssid", true)) new_ssid = request->getParam("ssid", true)->value();
        if (request->hasParam("pass", true)) new_pass = request->getParam("pass", true)->value();

        if (new_ssid.length() > 0) {
            _prefs.putString("ssid", new_ssid);
            _prefs.putString("pass", new_pass);
            request->send(200, "text/html", "<html><body style='background:#0f172a;color:#fff;font-family:sans-serif;padding:30px;text-align:center;'><h2>Налаштування збережено!</h2><p>Прилад перезавантажується для підключення до мережі " + new_ssid + "...</p></body></html>");
            delay(1500);
            ESP.restart();
        } else {
            request->send(400, "text/plain", "Bad Request: SSID is empty");
        }
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
                    totV = _packs[i].voltage;
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
                    pObj["temp_avg"] = _packs[i].tempAvg;
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
    if (_apMode) {
        return WiFi.softAPIP().toString();
    }
    return "Не підключено";
}

bool WebGateway::isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

bool WebGateway::isAPMode() {
    return _apMode;
}
