#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Arduino_GFX_Library.h>

// --- Піни SD картки JC3248W535C ---
// SD_MMC 1-bit mode:
#define SD_CLK   12
#define SD_CMD   11
#define SD_D0    13
// SPI mode:
#define SD_CS    10

// --- Піни дисплея ---
#define LCD_CS    45
#define LCD_SCLK  47
#define LCD_D0    21
#define LCD_D1    48
#define LCD_D2    40
#define LCD_D3    39
#define LCD_BL    1

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_GFX *panel = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, 320, 480);
Arduino_Canvas *gfx = new Arduino_Canvas(320, 480, panel, 0, 0, 0);

SPIClass sdSPI(FSPI);
fs::FS* currentFS = nullptr;
bool sdReady = false;

void updateScreen(const char* status, const char* detail = "", uint16_t color = RGB565_WHITE) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_CYAN);
  gfx->setTextSize(2);
  gfx->setCursor(20, 30);
  gfx->println("JC3248W535 USB Dumper");

  gfx->setTextColor(color);
  gfx->setTextSize(2);
  gfx->setCursor(20, 80);
  gfx->println(status);

  if (strlen(detail) > 0) {
    gfx->setTextColor(RGB565_LIGHTGREY);
    gfx->setTextSize(1);
    gfx->setCursor(20, 120);
    gfx->println(detail);
  }

  gfx->flush();
}

bool initSD() {
  // Спроба 1: Апаратний режим SD_MMC (1-bit)
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  if (SD_MMC.begin("/sdcard", true)) {
    uint8_t cardType = SD_MMC.cardType();
    if (cardType != CARD_NONE) {
      currentFS = &SD_MMC;
      return true;
    }
    SD_MMC.end();
  }

  // Спроба 2: Режим SPI
  sdSPI.begin(SD_CLK, SD_D0, SD_CMD, SD_CS);
  if (SD.begin(SD_CS, sdSPI, 4000000U)) {
    uint8_t cardType = SD.cardType();
    if (cardType != CARD_NONE) {
      currentFS = &SD;
      return true;
    }
    SD.end();
  }

  return false;
}

void listDirRecursive(File dir) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    String path = entry.path();
    if (entry.isDirectory()) {
      listDirRecursive(entry);
    } else {
      Serial.printf("FILE:%s:%u\n", path.c_str(), (unsigned int)entry.size());
    }
    entry.close();
  }
}

void sendFile(const String& path) {
  if (!currentFS) return;
  File f = currentFS->open(path, FILE_READ);
  if (!f) {
    Serial.println("ERR:OPEN_FAILED");
    return;
  }

  size_t total = f.size();
  Serial.printf("START_FILE:%s:%u\n", path.c_str(), total);
  Serial.flush();

  uint8_t buf[1024];
  size_t bytesRemaining = total;
  while (bytesRemaining > 0) {
    size_t toRead = bytesRemaining < sizeof(buf) ? bytesRemaining : sizeof(buf);
    size_t bytesRead = f.read(buf, toRead);
    if (bytesRead == 0) break;

    while (Serial.availableForWrite() < bytesRead) {
      delay(1);
    }
    Serial.write(buf, bytesRead);
    bytesRemaining -= bytesRead;
  }
  f.close();
  Serial.flush();
  Serial.println("\nEND_FILE");
  Serial.flush();
}

void setup() {
  Serial.begin(115200);

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  gfx->begin();
  updateScreen("Checking SD card...", "", RGB565_YELLOW);

  sdReady = initSD();
  if (sdReady) {
    uint64_t cardSize = 0;
    if (currentFS == &SD_MMC) {
      cardSize = SD_MMC.cardSize() / (1024 * 1024);
      updateScreen("SD_MMC: MOUNTED", String("Size: " + String((unsigned long)cardSize) + " MB").c_str(), RGB565_GREEN);
    } else {
      cardSize = SD.cardSize() / (1024 * 1024);
      updateScreen("SD_SPI: MOUNTED", String("Size: " + String((unsigned long)cardSize) + " MB").c_str(), RGB565_GREEN);
    }
    Serial.println("STATUS:SD_OK");
  } else {
    updateScreen("SD Card: NOT FOUND", "Insert card and restart", RGB565_RED);
    Serial.println("STATUS:SD_FAIL");
  }
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "PING") {
      Serial.println("PONG");
    } else if (cmd == "STATUS") {
      if (!sdReady) {
        sdReady = initSD();
      }
      if (sdReady) {
        Serial.println("STATUS:SD_OK");
      } else {
        Serial.println("STATUS:SD_FAIL");
      }
    } else if (cmd == "LIST") {
      if (!sdReady || !currentFS) {
        Serial.println("ERR:NO_SD");
      } else {
        File root = currentFS->open("/");
        listDirRecursive(root);
        root.close();
        Serial.println("END_LIST");
      }
    } else if (cmd.startsWith("GET ")) {
      String path = cmd.substring(4);
      path.trim();
      updateScreen("Sending file...", path.c_str(), RGB565_CYAN);
      sendFile(path);
      updateScreen("SD Card: Ready", "Waiting for command...", RGB565_GREEN);
    }
  }
}
