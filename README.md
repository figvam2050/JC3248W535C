# ESP32-S3 3.5" IPS JC3248W535C Display Project

Цей проект налаштовано для плати **JC3248W535C** (ESP32-S3 16MB Flash, 8MB OPI PSRAM, 3.5" 320x480 IPS екран з контролером AXS15231B QSPI та ємнісним сенсором AXS15231B I2C).

---

## 🔒 1. Заводський бекап (Рідна прошивка)

Повний побайтовий образ всієї пам'яті (Flash 16 МБ) успішно збережено у:
```
backup/jc3248w535c_factory_full_16MB.bin
```
Цей файл містить:
- Bootloader
- Таблицю розділів (Partition Table)
- NVS (налаштування)
- Повну заводську програму (`app0`) разом з усіма вбудованими зображеннями, шрифтами, додатками та медіа-файлами

### ↺ Як відновити заводську прошивку:
У будь-який момент ви можете повністю повернути плату до стану «з заводу», виконавши в терміналі:
```bash
./restore_factory.sh
```
або вручну через `esptool`:
```bash
esptool --port /dev/cu.usbmodem101 write-flash 0x0 backup/jc3248w535c_factory_full_16MB.bin
```

---

## ⚙️ 2. Конфігурація PlatformIO (`platformio.ini`)

- **Платформа:** `espressif32`
- **Плата:** `esp32-s3-devkitc-1`
- **Пам'ять:** 16MB Flash (QIO), 8MB PSRAM (OPI)
- **USB CDC:** Увімкнено (друк `Serial` через вбудований USB-порт)
- **Основна графічна бібліотека:** `moononournation/GFX Library for Arduino`

---

## 📌 3. Розпіновка (Pinout)

### Дисплей (QSPI AXS15231B):
| Сигнал | GPIO ESP32-S3 |
|---|---|
| LCD_CS | GPIO 45 |
| LCD_SCLK | GPIO 47 |
| LCD_D0 | GPIO 21 |
| LCD_D1 | GPIO 48 |
| LCD_D2 | GPIO 40 |
| LCD_D3 | GPIO 39 |
| LCD_BL (Підсвітка) | GPIO 1 |

### Ємнісний тачскрін (I2C):
| Сигнал | GPIO ESP32-S3 |
|---|---|
| TOUCH_SDA | GPIO 4 |
| TOUCH_SCL | GPIO 8 |
| TOUCH_INT | GPIO 11 |
| TOUCH_RST | GPIO 12 |
| I2C Address | 0x3B |

---

## 🚀 4. Збірка та прошивка

- **Зібрати проект:** `pio run`
- **Завантажити на плату:** `pio run -t upload`
- **Монітор порту:** `pio device monitor`

