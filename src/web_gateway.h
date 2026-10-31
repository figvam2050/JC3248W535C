#ifndef WEB_GATEWAY_H
#define WEB_GATEWAY_H

#include "bms_data.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>

class WebGateway {
public:
    void begin(BatteryPack* packs, SemaphoreHandle_t mutex);
    String getIpAddress();
    bool isConnected();

private:
    AsyncWebServer* _server = nullptr;
    BatteryPack* _packs = nullptr;
    SemaphoreHandle_t _mutex = nullptr;

    void setupRoutes();
};

extern WebGateway Gateway;

#endif // WEB_GATEWAY_H

