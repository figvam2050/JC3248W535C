#ifndef RS485_SNIFFER_H
#define RS485_SNIFFER_H

#include "bms_data.h"

// JST 1.25 Power/UART connector (P1)
#define RS485_RX_PIN 44
#define RS485_TX_PIN 43 // Transmit pin for Active Master polling mode

class RS485Sniffer {
public:
    void begin(HardwareSerial* serialPort = &Serial2);
    void update(BatteryPack* packs, SemaphoreHandle_t mutex);
    
    bool demoMode = false; // Default: real RS485 listening (demo only on explicit user toggle)
    void updateDemo(BatteryPack* packs, SemaphoreHandle_t mutex);

    // Active Master Polling mode (for bench testing packs without an inverter)
    void setActiveMaster(bool enable);
    bool isActiveMaster() const { return _activeMaster; }
    void setMaxPollAddrs(uint8_t maxPacks) { _maxPollAddrs = (maxPacks >= 1 && maxPacks <= MAX_BATTERIES) ? maxPacks : 4; }
    uint8_t getMaxPollAddrs() const { return _maxPollAddrs; }

private:
    HardwareSerial* _port = nullptr;
    uint8_t _rxBuf[512];
    size_t _rxLen = 0;
    
    bool _activeMaster = false;
    uint32_t _lastPollMs = 0;
    uint8_t _pollAddr = 1;
    uint8_t _maxPollAddrs = 4; // Poll pack 1 to 4 by default in master mode

    uint16_t calculateCRC(const uint8_t* buf, size_t len);
    void parseFrame(const uint8_t* frame, size_t len, BatteryPack* packs, SemaphoreHandle_t mutex);
    void sendPollFrame(uint8_t addr);
};

extern RS485Sniffer Sniffer;

#endif // RS485_SNIFFER_H

