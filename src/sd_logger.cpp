#include "sd_logger.h"

SdLogger Logger;

void SdLogger::begin() {
    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
    if (SD_MMC.begin("/sdcard", true /* 1-bit mode */, false /* format_if_empty */, 20000 /* 20MHz */)) {
        _cardAvailable = true;
        Serial.printf("[SD Logger] MicroSD card mounted! Size: %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));
    } else {
        _cardAvailable = false;
        Serial.println("[SD Logger] MicroSD card not detected or slot empty");
    }
}

bool SdLogger::toggleBankLog() {
    if (!_cardAvailable) return false;
    _bankLogging = !_bankLogging;

    if (_bankLogging) {
        // Init bank log file if not exists
        if (!SD_MMC.exists("/bms_bank_log.csv")) {
            File f = SD_MMC.open("/bms_bank_log.csv", FILE_WRITE);
            if (f) {
                f.println("Timestamp_ms,Avg_Voltage_V,Total_Current_A,Total_Power_kW,Avg_SOC,Online_Packs,Bank_Delta_mV,Min_Cell_mV,Max_Cell_mV");
                f.close();
            }
        }
    }
    return _bankLogging;
}

bool SdLogger::togglePackLog(uint8_t packIdx) {
    if (!_cardAvailable || packIdx >= MAX_BATTERIES) return false;
    _packLogging[packIdx] = !_packLogging[packIdx];

    if (_packLogging[packIdx]) {
        char path[32];
        snprintf(path, sizeof(path), "/pack_%02u_log.csv", packIdx + 1);
        if (!SD_MMC.exists(path)) {
            File f = SD_MMC.open(path, FILE_WRITE);
            if (f) {
                f.println("Timestamp_ms,Pack,Voltage_V,Current_A,Power_W,SOC,Cell_Min_mV,Cell_Max_mV,Delta_mV,Temp_Avg,Alarms,Cells_1_to_15");
                f.close();
            }
        }
    }
    return _packLogging[packIdx];
}

bool SdLogger::isPackLogging(uint8_t packIdx) const {
    if (packIdx >= MAX_BATTERIES) return false;
    return _packLogging[packIdx];
}

uint32_t SdLogger::getPackRows(uint8_t packIdx) const {
    if (packIdx >= MAX_BATTERIES) return 0;
    return _packRows[packIdx];
}

void SdLogger::update(const BatteryPack* packs, size_t count) {
    if (!_cardAvailable) return;

    // Log at 1-second intervals
    if (millis() - _lastLogMs < 1000) return;
    _lastLogMs = millis();

    uint32_t nowMs = millis();

    // 1. Bank Log
    if (_bankLogging) {
        float vSum = 0, iSum = 0, socSum = 0;
        uint16_t gMin = 9999, gMax = 0;
        uint8_t onlineCnt = 0;

        for (size_t i = 0; i < count; i++) {
            if (packs[i].isOnline) {
                vSum += packs[i].voltage;
                iSum += packs[i].current;
                socSum += packs[i].soc;
                onlineCnt++;
                if (packs[i].cellMin < gMin && packs[i].cellMin > 0) gMin = packs[i].cellMin;
                if (packs[i].cellMax > gMax) gMax = packs[i].cellMax;
            }
        }

        if (onlineCnt > 0) {
            File f = SD_MMC.open("/bms_bank_log.csv", FILE_APPEND);
            if (f) {
                float avgV = vSum / onlineCnt;
                float totKw = (avgV * iSum) / 1000.0f;
                float avgSoc = socSum / onlineCnt;
                uint16_t delta = (gMax >= gMin) ? (gMax - gMin) : 0;

                f.printf("%u,%.2f,%.1f,%.2f,%.0f,%u,%u,%u,%u\n",
                    nowMs, avgV, iSum, totKw, avgSoc, onlineCnt, delta, gMin, gMax);
                f.close();
                _bankRows++;
            }
        }
    }

    // 2. Individual Pack Logs
    for (size_t i = 0; i < count; i++) {
        if (_packLogging[i] && packs[i].isOnline) {
            char path[32];
            snprintf(path, sizeof(path), "/pack_%02u_log.csv", (unsigned int)(i + 1));
            File f = SD_MMC.open(path, FILE_APPEND);
            if (f) {
                f.printf("%u,%u,%.2f,%.1f,%.0f,%u,%u,%u,%u,%d,%u",
                    nowMs,
                    (unsigned int)(i + 1),
                    packs[i].voltage,
                    packs[i].current,
                    packs[i].voltage * packs[i].current,
                    packs[i].soc,
                    packs[i].cellMin,
                    packs[i].cellMax,
                    packs[i].cellDiff,
                    packs[i].tempAvg,
                    packs[i].warningRaw | packs[i].protectionRaw | packs[i].errorRaw
                );

                for (int c = 0; c < CELLS_PER_BATTERY; c++) {
                    f.printf(";%u", packs[i].cells[c]);
                }
                f.println();
                f.close();
                _packRows[i]++;
            }
        }
    }
}
