#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <lvgl.h>
#include "bms_data.h"
#include "rs485_sniffer.h"
#include "web_gateway.h"

// --- Display Pins JC3248W535C (QSPI) ---
#define LCD_CS    45
#define LCD_SCLK  47
#define LCD_D0    21
#define LCD_D1    48
#define LCD_D2    40
#define LCD_D3    39
#define LCD_BL    1

// --- Touch Pins & Address ---
#define TOUCH_ADDR       0x3B
#define TOUCH_SDA        4
#define TOUCH_SCL        8
#define TOUCH_RST_PIN    12
#define TOUCH_INT_PIN    11
#define TOUCH_I2C_CLOCK  400000
#define AXS_MAX_TOUCH    1

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 480

// Display & Canvas
static Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
static Arduino_GFX *panel = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, SCREEN_WIDTH, SCREEN_HEIGHT);
static Arduino_Canvas *gfx = new Arduino_Canvas(SCREEN_WIDTH, SCREEN_HEIGHT, panel, 0, 0, 0);

// LVGL Buffers
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

// Data synchronization
static SemaphoreHandle_t dataMutex = NULL;
static BatteryPack g_packs[MAX_BATTERIES];

// UI Objects
static lv_obj_t *tabview = NULL;
static lv_obj_t *tab_overview = NULL;

// Overview labels & meters
static lv_obj_t *lbl_sys_status = NULL;
static lv_obj_t *lbl_sys_voltage = NULL;
static lv_obj_t *lbl_sys_current = NULL;
static lv_obj_t *lbl_sys_power = NULL;
static lv_obj_t *lbl_sys_soc = NULL;
static lv_obj_t *bar_sys_soc = NULL;
static lv_obj_t *lbl_sys_delta = NULL;
static lv_obj_t *lbl_sys_packs = NULL;
static lv_obj_t *lbl_wifi_ip = NULL;

// Dynamic Pack Tabs structure
struct PackUI {
    lv_obj_t *tab;
    lv_obj_t *lbl_status;
    lv_obj_t *lbl_vi_pwr;
    lv_obj_t *bar_soc;
    lv_obj_t *lbl_soc;
    lv_obj_t *lbl_temps;
    lv_obj_t *lbl_delta;
    lv_obj_t *lbl_cells[CELLS_PER_BATTERY];
    lv_obj_t *cell_cards[CELLS_PER_BATTERY];
    bool created;
};
static PackUI g_pack_ui[MAX_BATTERIES];

// --- Touch Reading ---
static bool touch_read_hw(uint16_t &x, uint16_t &y) {
    uint8_t data[AXS_MAX_TOUCH * 6 + 2] = {0};
    const uint8_t read_cmd[11] = {
        0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00,
        (uint8_t)((AXS_MAX_TOUCH * 6 + 2) >> 8),
        (uint8_t)((AXS_MAX_TOUCH * 6 + 2) & 0xff),
        0x00, 0x00, 0x00
    };

    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(read_cmd, sizeof(read_cmd));
    if (Wire.endTransmission() != 0) return false;

    if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (size_t)sizeof(data)) != sizeof(data)) return false;
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = Wire.read();
    }

    if (data[1] > 0 && data[1] <= AXS_MAX_TOUCH) {
        x = ((data[2] & 0x0F) << 8) | data[3];
        y = ((data[4] & 0x0F) << 8) | data[5];
        return true;
    }
    return false;
}

// --- LVGL Display Flush Callback ---
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);

    if (lv_disp_flush_is_last(disp)) {
        gfx->flush();
    }
    lv_disp_flush_ready(disp);
}

// --- LVGL Touch Input Read Callback ---
static void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    uint16_t touchX = 0, touchY = 0;
    bool touched = touch_read_hw(touchX, touchY);

    if (touched) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touchX;
        data->point.y = touchY;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// --- Helper Card Styling ---
static void create_card_box(lv_obj_t *parent, lv_obj_t *&box, int w, int h) {
    box = lv_obj_create(parent);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x374151), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_all(box, 6, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
}

// --- Create Tab for a Battery Pack ---
static void create_battery_tab(uint8_t pack_idx) {
    char tab_name[16];
    snprintf(tab_name, sizeof(tab_name), "Pack #%u", pack_idx + 1);
    
    lv_obj_t *tab = lv_tabview_add_tab(tabview, tab_name);
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x111827), 0);
    lv_obj_set_style_pad_all(tab, 6, 0);

    PackUI &ui = g_pack_ui[pack_idx];
    ui.tab = tab;

    // Header Card
    lv_obj_t *header_card;
    create_card_box(tab, header_card, 308, 95);
    lv_obj_align(header_card, LV_ALIGN_TOP_MID, 0, 0);

    ui.lbl_status = lv_label_create(header_card);
    lv_label_set_text(ui.lbl_status, "STANDBY");
    lv_obj_set_style_text_color(ui.lbl_status, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_text_font(ui.lbl_status, &lv_font_montserrat_16, 0);
    lv_obj_align(ui.lbl_status, LV_ALIGN_TOP_LEFT, 2, 0);

    ui.lbl_delta = lv_label_create(header_card);
    lv_label_set_text(ui.lbl_delta, "Delta: -- mV");
    lv_obj_set_style_text_color(ui.lbl_delta, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_text_font(ui.lbl_delta, &lv_font_montserrat_14, 0);
    lv_obj_align(ui.lbl_delta, LV_ALIGN_TOP_RIGHT, -2, 0);

    ui.lbl_vi_pwr = lv_label_create(header_card);
    lv_label_set_text(ui.lbl_vi_pwr, "51.20 V | 0.00 A | 0 W");
    lv_obj_set_style_text_color(ui.lbl_vi_pwr, lv_color_white(), 0);
    lv_obj_set_style_text_font(ui.lbl_vi_pwr, &lv_font_montserrat_14, 0);
    lv_obj_align(ui.lbl_vi_pwr, LV_ALIGN_LEFT_MID, 2, -2);

    // SoC Bar
    ui.bar_soc = lv_bar_create(header_card);
    lv_obj_set_size(ui.bar_soc, 230, 14);
    lv_obj_align(ui.bar_soc, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    lv_bar_set_range(ui.bar_soc, 0, 100);
    lv_obj_set_style_bg_color(ui.bar_soc, lv_color_hex(0x374151), 0);
    lv_obj_set_style_bg_color(ui.bar_soc, lv_color_hex(0x10B981), LV_PART_INDICATOR);

    ui.lbl_soc = lv_label_create(header_card);
    lv_label_set_text(ui.lbl_soc, "100%");
    lv_obj_set_style_text_color(ui.lbl_soc, lv_color_white(), 0);
    lv_obj_set_style_text_font(ui.lbl_soc, &lv_font_montserrat_12, 0);
    lv_obj_align(ui.lbl_soc, LV_ALIGN_BOTTOM_RIGHT, -2, -2);

    // Temperatures label
    ui.lbl_temps = lv_label_create(tab);
    lv_label_set_text(ui.lbl_temps, "PCB: 25C | Avg: 24C | Max: 25C");
    lv_obj_set_style_text_color(ui.lbl_temps, lv_color_hex(0x9CA3AF), 0);
    lv_obj_set_style_text_font(ui.lbl_temps, &lv_font_montserrat_12, 0);
    lv_obj_align(ui.lbl_temps, LV_ALIGN_TOP_LEFT, 8, 102);

    // 15 Cells Grid: 5 rows x 3 columns
    int start_y = 122;
    for (int i = 0; i < CELLS_PER_BATTERY; i++) {
        int row = i / 3;
        int col = i % 3;
        int x = 6 + col * (96 + 8);
        int y = start_y + row * (48 + 5);

        create_card_box(tab, ui.cell_cards[i], 96, 48);
        lv_obj_set_pos(ui.cell_cards[i], x, y);

        lv_obj_t *c_lbl = lv_label_create(ui.cell_cards[i]);
        char c_text[20];
        snprintf(c_text, sizeof(c_text), "C%02d: 3300", i + 1);
        lv_label_set_text(c_lbl, c_text);
        lv_obj_set_style_text_font(c_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(c_lbl, lv_color_white(), 0);
        lv_obj_align(c_lbl, LV_ALIGN_CENTER, 0, 0);
        ui.lbl_cells[i] = c_lbl;
    }

    ui.created = true;
}

// --- Build UI ---
static void build_ui() {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x111827), 0);

    tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 38);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x111827), 0);
    
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0x9CA3AF), 0);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0x38BDF8), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tab_btns, lv_color_hex(0x38BDF8), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_14, 0);

    // === TAB 1: System Overview ===
    tab_overview = lv_tabview_add_tab(tabview, "Overview");
    lv_obj_set_style_bg_color(tab_overview, lv_color_hex(0x111827), 0);
    lv_obj_set_style_pad_all(tab_overview, 6, 0);

    // Title / Wi-Fi Card
    lv_obj_t *title_card;
    create_card_box(tab_overview, title_card, 308, 44);
    lv_obj_align(title_card, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *title_lbl = lv_label_create(title_card);
    lv_label_set_text(title_lbl, "BMS SNIFFER GATEWAY");
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 2, 0);

    lbl_wifi_ip = lv_label_create(title_card);
    lv_label_set_text(lbl_wifi_ip, "Wi-Fi: Connecting...");
    lv_obj_set_style_text_color(lbl_wifi_ip, lv_color_hex(0x9CA3AF), 0);
    lv_obj_set_style_text_font(lbl_wifi_ip, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_wifi_ip, LV_ALIGN_RIGHT_MID, -2, 0);

    // Main Power & Energy Card
    lv_obj_t *pwr_card;
    create_card_box(tab_overview, pwr_card, 308, 140);
    lv_obj_align(pwr_card, LV_ALIGN_TOP_MID, 0, 50);

    lbl_sys_status = lv_label_create(pwr_card);
    lv_label_set_text(lbl_sys_status, "NORMAL OPERATION");
    lv_obj_set_style_text_color(lbl_sys_status, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_text_font(lbl_sys_status, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_sys_status, LV_ALIGN_TOP_LEFT, 4, 2);

    lbl_sys_power = lv_label_create(pwr_card);
    lv_label_set_text(lbl_sys_power, "0.00 kW");
    lv_obj_set_style_text_color(lbl_sys_power, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_sys_power, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_sys_power, LV_ALIGN_LEFT_MID, 4, -14);

    lbl_sys_voltage = lv_label_create(pwr_card);
    lv_label_set_text(lbl_sys_voltage, "51.20 V");
    lv_obj_set_style_text_color(lbl_sys_voltage, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_text_font(lbl_sys_voltage, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl_sys_voltage, LV_ALIGN_BOTTOM_LEFT, 4, -4);

    lbl_sys_current = lv_label_create(pwr_card);
    lv_label_set_text(lbl_sys_current, "0.0 A");
    lv_obj_set_style_text_color(lbl_sys_current, lv_color_hex(0xFB7185), 0);
    lv_obj_set_style_text_font(lbl_sys_current, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl_sys_current, LV_ALIGN_BOTTOM_RIGHT, -4, -4);

    // SoC Card
    lv_obj_t *soc_card;
    create_card_box(tab_overview, soc_card, 308, 90);
    lv_obj_align(soc_card, LV_ALIGN_TOP_MID, 0, 196);

    lv_obj_t *soc_title = lv_label_create(soc_card);
    lv_label_set_text(soc_title, "BANK STATE OF CHARGE");
    lv_obj_set_style_text_color(soc_title, lv_color_hex(0x9CA3AF), 0);
    lv_obj_set_style_text_font(soc_title, &lv_font_montserrat_12, 0);
    lv_obj_align(soc_title, LV_ALIGN_TOP_LEFT, 4, 2);

    lbl_sys_soc = lv_label_create(soc_card);
    lv_label_set_text(lbl_sys_soc, "100%");
    lv_obj_set_style_text_color(lbl_sys_soc, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_text_font(lbl_sys_soc, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_sys_soc, LV_ALIGN_TOP_RIGHT, -4, 0);

    bar_sys_soc = lv_bar_create(soc_card);
    lv_obj_set_size(bar_sys_soc, 290, 20);
    lv_bar_set_range(bar_sys_soc, 0, 100);
    lv_bar_set_value(bar_sys_soc, 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_sys_soc, lv_color_hex(0x374151), 0);
    lv_obj_set_style_bg_color(bar_sys_soc, lv_color_hex(0x10B981), LV_PART_INDICATOR);
    lv_obj_align(bar_sys_soc, LV_ALIGN_BOTTOM_MID, 0, -6);

    // Details Card
    lv_obj_t *det_card;
    create_card_box(tab_overview, det_card, 308, 100);
    lv_obj_align(det_card, LV_ALIGN_TOP_MID, 0, 292);

    lbl_sys_packs = lv_label_create(det_card);
    lv_label_set_text(lbl_sys_packs, "Packs Online: 0 / 16");
    lv_obj_set_style_text_color(lbl_sys_packs, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_sys_packs, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_sys_packs, LV_ALIGN_TOP_LEFT, 4, 6);

    lbl_sys_delta = lv_label_create(det_card);
    lv_label_set_text(lbl_sys_delta, "Bank Max Cell Delta: 0 mV");
    lv_obj_set_style_text_color(lbl_sys_delta, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_text_font(lbl_sys_delta, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_sys_delta, LV_ALIGN_TOP_LEFT, 4, 34);

    lv_obj_t *sniff_mode_lbl = lv_label_create(det_card);
    lv_label_set_text(sniff_mode_lbl, "Sniffer Bus: RX GPIO 44 (Passive)");
    lv_obj_set_style_text_color(sniff_mode_lbl, lv_color_hex(0x9CA3AF), 0);
    lv_obj_set_style_text_font(sniff_mode_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(sniff_mode_lbl, LV_ALIGN_BOTTOM_LEFT, 4, -4);
}

// --- Periodic UI Refresh from BMS Data ---
static void update_ui() {
    BankSummary summary;
    BatteryPack local_packs[MAX_BATTERIES];

    // Compute Bank Summary locally from g_packs safely with mutex
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(local_packs, g_packs, sizeof(g_packs));
        xSemaphoreGive(dataMutex);
    } else {
        return;
    }

    float vSum = 0;
    float iSum = 0;
    float socSum = 0;
    uint8_t count = 0;
    uint16_t gMin = 9999;
    uint16_t gMax = 0;
    bool alarms = false;

    for (int i = 0; i < MAX_BATTERIES; i++) {
        if (local_packs[i].isOnline) {
            vSum += local_packs[i].voltage;
            iSum += local_packs[i].current;
            socSum += local_packs[i].soc;
            count++;
            if (local_packs[i].cellMin < gMin && local_packs[i].cellMin > 0) gMin = local_packs[i].cellMin;
            if (local_packs[i].cellMax > gMax) gMax = local_packs[i].cellMax;
            if (local_packs[i].warningRaw || local_packs[i].protectionRaw || local_packs[i].errorRaw) alarms = true;
        }
    }

    summary.onlineCount = count;
    summary.totalVoltage = (count > 0) ? (vSum / count) : 0.0f; // Battery bank voltage in parallel
    summary.totalCurrent = iSum;
    summary.totalPowerKw = (summary.totalVoltage * summary.totalCurrent) / 1000.0f;
    summary.avgSoc = (count > 0) ? (socSum / count) : 0.0f;
    summary.globalMinCell = (count > 0) ? gMin : 0;
    summary.globalMaxCell = (count > 0) ? gMax : 0;
    summary.globalDeltaMv = (count > 0 && gMax >= gMin) ? (gMax - gMin) : 0;
    summary.hasAlarms = alarms;

    // Update Wi-Fi Status label
    if (Gateway.isConnected()) {
        char ip_str[32];
        snprintf(ip_str, sizeof(ip_str), "IP: %s", Gateway.getIpAddress().c_str());
        lv_label_set_text(lbl_wifi_ip, ip_str);
    } else {
        lv_label_set_text(lbl_wifi_ip, "Wi-Fi: Connecting...");
    }

    // Update Overview Card
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f V", summary.totalVoltage);
    lv_label_set_text(lbl_sys_voltage, buf);

    snprintf(buf, sizeof(buf), "%+.1f A", summary.totalCurrent);
    lv_label_set_text(lbl_sys_current, buf);

    snprintf(buf, sizeof(buf), "%.2f kW", summary.totalPowerKw);
    lv_label_set_text(lbl_sys_power, buf);

    snprintf(buf, sizeof(buf), "%.0f%%", summary.avgSoc);
    lv_label_set_text(lbl_sys_soc, buf);
    lv_bar_set_value(bar_sys_soc, (int32_t)summary.avgSoc, LV_ANIM_ON);

    snprintf(buf, sizeof(buf), "Packs Online: %u (Demo: %s)", summary.onlineCount, Sniffer.demoMode ? "ON" : "OFF");
    lv_label_set_text(lbl_sys_packs, buf);

    snprintf(buf, sizeof(buf), "Bank Max Cell Delta: %u mV", summary.globalDeltaMv);
    lv_label_set_text(lbl_sys_delta, buf);

    // Update or Create Dynamic Tabs for each pack
    for (uint8_t i = 0; i < MAX_BATTERIES; i++) {
        BatteryPack &p = local_packs[i];
        if (!p.isOnline) continue;

        if (!g_pack_ui[i].created) {
            create_battery_tab(i);
        }

        PackUI &ui = g_pack_ui[i];

        // Status & Alarms
        if (p.warningRaw || p.protectionRaw || p.errorRaw) {
            lv_label_set_text(ui.lbl_status, "ALARM / WARNING");
            lv_obj_set_style_text_color(ui.lbl_status, lv_color_hex(0xEF4444), 0);
        } else if (p.current > 0.5f) {
            lv_label_set_text(ui.lbl_status, "CHARGING");
            lv_obj_set_style_text_color(ui.lbl_status, lv_color_hex(0x10B981), 0);
        } else if (p.current < -0.5f) {
            lv_label_set_text(ui.lbl_status, "DISCHARGING");
            lv_obj_set_style_text_color(ui.lbl_status, lv_color_hex(0x38BDF8), 0);
        } else {
            lv_label_set_text(ui.lbl_status, "STANDBY");
            lv_obj_set_style_text_color(ui.lbl_status, lv_color_hex(0x9CA3AF), 0);
        }

        // V, I, Pwr
        snprintf(buf, sizeof(buf), "%.2f V | %+.1f A | %.0f W", p.voltage, p.current, p.voltage * p.current);
        lv_label_set_text(ui.lbl_vi_pwr, buf);

        // SoC
        snprintf(buf, sizeof(buf), "%u%%", p.soc);
        lv_label_set_text(ui.lbl_soc, buf);
        lv_bar_set_value(ui.bar_soc, p.soc, LV_ANIM_OFF);

        // Delta
        snprintf(buf, sizeof(buf), "Delta: %u mV", p.cellDiff);
        lv_label_set_text(ui.lbl_delta, buf);

        // Temps
        snprintf(buf, sizeof(buf), "PCB: %dC | Avg: %dC | Max: %dC", p.tempPcb, p.tempAvg, p.tempMax);
        lv_label_set_text(ui.lbl_temps, buf);

        // 15 Cells
        for (int c = 0; c < CELLS_PER_BATTERY; c++) {
            bool bal = (p.balanceMask & (1 << c)) != 0;
            if (bal) {
                snprintf(buf, sizeof(buf), "C%02d:%u\n[BAL]", c + 1, p.cells[c]);
            } else {
                snprintf(buf, sizeof(buf), "C%02d:%u", c + 1, p.cells[c]);
            }
            lv_label_set_text(ui.lbl_cells[c], buf);

            // Highlight min (blue) and max (amber/red) or balancing
            if (bal) {
                lv_obj_set_style_bg_color(ui.cell_cards[c], lv_color_hex(0x831843), 0);
                lv_obj_set_style_text_color(ui.lbl_cells[c], lv_color_hex(0xF472B6), 0);
            } else if (p.cells[c] == p.cellMax && p.cellMax > p.cellMin) {
                lv_obj_set_style_bg_color(ui.cell_cards[c], lv_color_hex(0x78350F), 0);
                lv_obj_set_style_text_color(ui.lbl_cells[c], lv_color_hex(0xFBBF24), 0);
            } else if (p.cells[c] == p.cellMin && p.cellMax > p.cellMin) {
                lv_obj_set_style_bg_color(ui.cell_cards[c], lv_color_hex(0x1E3A8A), 0);
                lv_obj_set_style_text_color(ui.lbl_cells[c], lv_color_hex(0x60A5FA), 0);
            } else {
                lv_obj_set_style_bg_color(ui.cell_cards[c], lv_color_hex(0x1F2937), 0);
                lv_obj_set_style_text_color(ui.lbl_cells[c], lv_color_white(), 0);
            }
        }
    }
}

// --- Task: Sniffer & Network (Core 0) ---
void sniffer_task(void *pvParameters) {
    while (true) {
        Sniffer.update(g_packs, dataMutex);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n==============================================");
    Serial.println("  JC3248W535C Smart BMS Sniffer Gateway");
    Serial.println("==============================================");

    dataMutex = xSemaphoreCreateMutex();

    // 1. Backlight on
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);

    // 2. Touch reset & I2C
    pinMode(TOUCH_INT_PIN, INPUT_PULLUP);
    pinMode(TOUCH_RST_PIN, OUTPUT);
    digitalWrite(TOUCH_RST_PIN, LOW);
    delay(20);
    digitalWrite(TOUCH_RST_PIN, HIGH);
    delay(100);

    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(TOUCH_I2C_CLOCK);

    // 3. Init Display & Canvas (in PSRAM)
    Serial.println("[Display] Initializing QSPI AXS15231B with PSRAM Canvas...");
    if (!gfx->begin()) {
        Serial.println("[Display] Error: gfx->begin() failed!");
    }
    gfx->fillScreen(RGB565_BLACK);
    gfx->flush();

    // 4. Init LVGL
    Serial.println("[LVGL] Initializing LVGL 8.3...");
    lv_init();

    // Allocate draw buffer in PSRAM
    disp_draw_buf = (lv_color_t *)ps_malloc(SCREEN_WIDTH * 40 * sizeof(lv_color_t));
    if (!disp_draw_buf) {
        Serial.println("[LVGL] PSRAM alloc failed, fallback to internal RAM");
        disp_draw_buf = (lv_color_t *)malloc(SCREEN_WIDTH * 20 * sizeof(lv_color_t));
        lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, SCREEN_WIDTH * 20);
    } else {
        lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, SCREEN_WIDTH * 40);
    }

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Touch driver
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // 5. Build UI
    build_ui();
    Serial.println("[LVGL] UI Built successfully");

    // 6. Start RS485 Sniffer (Passive RX on GPIO 44)
    Sniffer.begin();

    // 7. Start Web Gateway & Wi-Fi
    Gateway.begin(g_packs, dataMutex);

    // 8. Launch Background Task for Sniffer & Web on Core 0
    xTaskCreatePinnedToCore(
        sniffer_task,
        "SnifferTask",
        4096,
        NULL,
        1,
        NULL,
        0
    );

    Serial.println("[System] Ready!");
}

void loop() {
    lv_timer_handler();

    static uint32_t last_ui_update = 0;
    if (millis() - last_ui_update >= 500) {
        last_ui_update = millis();
        update_ui();
    }

    delay(5);
}
