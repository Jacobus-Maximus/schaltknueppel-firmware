// Schaltknueppel Installer
// (c) 2026 Jacob Richter, licensed under CC BY-NC 4.0 (https://creativecommons.org/licenses/by-nc/4.0/)
//
// Prepares a new Waveshare ESP32-S3-Touch-LCD-1.46B for the Schaltknueppel firmware. Flash this sketch once
// with the Arduino IDE (settings in README.md), then open the Schaltknueppel app on the phone: it finds the board
// as "Schaltknueppel Setup", sees version 0.0.1 and offers the newest firmware from GitHub. The firmware comes
// over Bluetooth into the second app slot; after a restart the board runs it and this installer is not used again.
//
// What it does, and nothing else:
//   - Bluetooth LE with the service the app looks for (Nordic UART service, see below)
//   - answers the app's {"config":"get"} with its version and "installer":true
//   - receives a firmware image (frames OTA_START, DATA, END from the app) into the free OTA slot, checks it and
//     restarts into it; only images of the project "schaltknueppel", and only for 15 minutes after power-on
//   - shows its state on the round display
//
// Needs the library "Adafruit GFX Library" (Arduino IDE: Tools > Manage Libraries). The display driver
// (esp_lcd_spd2010.c/.h) is Espressif's, Apache License 2.0.

#include <Arduino.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

#include "freertos/message_buffer.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_lcd_spd2010.h"

/********************* Settings *********************/

#define INSTALLER_VERSION   "0.0.1"             // always lower than any firmware, so the app offers the update
#define DEVICE_NAME         "Schaltknueppel Setup"
#define PROJECT_NAME        "schaltknueppel"    // only firmware built from this project is accepted
#define WINDOW_MS           (15UL * 60 * 1000)  // updates are accepted this long after power-on
#define FRAME_TIMEOUT_MS    15000               // a transfer without frames for this long is dropped

/********************* Board pins (Waveshare ESP32-S3-Touch-LCD-1.46B) *********************/

#define PIN_LCD_SCK         40
#define PIN_LCD_D0          46
#define PIN_LCD_D1          45
#define PIN_LCD_D2          42
#define PIN_LCD_D3          41
#define PIN_LCD_CS          21
#define PIN_BACKLIGHT       5
#define PIN_I2C_SDA         11
#define PIN_I2C_SCL         10
#define EXIO_ADDRESS        0x20    // TCA9554 IO expander, the display's reset line is on it
#define EXIO_LCD_RESET      2       // its pins count from 1
#define LCD_SIZE            412
#define LCD_RADIUS          206
#define STRIP_LINES         24

/********************* Protocol of the Schaltknueppel app *********************/

// Nordic UART service: text lines on RX, upload frames on FILE, answers on STATUS
#define UUID_SERVICE        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define UUID_RX             "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define UUID_FILE           "6E400004-B5A3-F393-E0A9-E50E24DCCA9E"
#define UUID_STATUS         "6E400005-B5A3-F393-E0A9-E50E24DCCA9E"

// Upload frames from the phone, numbers little-endian
#define FRAME_START         0x01    // a picture for the SD card: not handled here
#define FRAME_DATA          0x02    // offset:u32 payload
#define FRAME_END           0x03    // crc32:u32 (as zlib)
#define FRAME_ABORT         0x04
#define FRAME_OTA_START     0x05    // size:u32
#define FRAME_LIST          0x10    // list the pictures: answered with an empty list

// Status notifications: code:u8 value:u32
#define ST_OK               0
#define ST_DONE             1
#define ST_NO_CARD          2
#define ST_TOO_BIG          4
#define ST_WRITE            5
#define ST_OFFSET           6
#define ST_CRC              7
#define ST_NO_TRANSFER      8
#define ST_TIMEOUT          9
#define ST_FIRMWARE         12
#define ST_LIST_END         0x11
#define CONFIG_PART         0x21    // config answer, part of the JSON text
#define CONFIG_END          0x22    // last part

/********************* Display *********************/

static esp_lcd_panel_handle_t s_panel;
static GFXcanvas16 *s_canvas;       // the whole picture, in PSRAM
static uint16_t *s_strip;           // DMA buffer for sending it to the panel

static const uint16_t COLOR_BG = 0x0000;
static const uint16_t COLOR_TEXT = 0xF79E;
static const uint16_t COLOR_MUTED = 0x9CF3;
static const uint16_t COLOR_ACCENT = 0xFB43;    // orange
static const uint16_t COLOR_OK = 0x4E2E;        // green
static const uint16_t COLOR_ERROR = 0xF186;     // red
static const uint16_t COLOR_TRACK = 0x2104;

static void exio_write(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(EXIO_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

static void display_begin()
{
    // Reset line low, then high (TCA9554: register 3 = direction, 1 = outputs)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
    exio_write(0x03, 0x00);
    exio_write(0x01, 0xFF & ~(1 << (EXIO_LCD_RESET - 1)));
    delay(100);
    exio_write(0x01, 0xFF);
    delay(100);

    spi_bus_config_t bus = {};
    bus.data0_io_num = PIN_LCD_D0;
    bus.data1_io_num = PIN_LCD_D1;
    bus.sclk_io_num = PIN_LCD_SCK;
    bus.data2_io_num = PIN_LCD_D2;
    bus.data3_io_num = PIN_LCD_D3;
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = 2048;
    bus.flags = SPICOMMON_BUSFLAG_MASTER;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = PIN_LCD_CS;
    io_config.dc_gpio_num = -1;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 80 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = 1;
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io));

    spd2010_vendor_config_t vendor = {};
    vendor.flags.use_qspi_interface = 1;
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor;
    ESP_ERROR_CHECK(esp_lcd_new_panel_spd2010(io, &panel_config, &s_panel));
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_disp_on_off(s_panel, true);

    s_canvas = new GFXcanvas16(LCD_SIZE, LCD_SIZE);
    s_strip = (uint16_t *)heap_caps_malloc(LCD_SIZE * STRIP_LINES * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, HIGH);
}

// The panel wants the bytes of each pixel swapped
static void display_show()
{
    if (!s_canvas || !s_canvas->getBuffer() || !s_strip) {
        return;
    }
    const uint16_t *frame = s_canvas->getBuffer();
    for (int y = 0; y < LCD_SIZE; y += STRIP_LINES) {
        int rows = min(STRIP_LINES, LCD_SIZE - y);
        for (int i = 0; i < LCD_SIZE * rows; i++) {
            uint16_t c = frame[y * LCD_SIZE + i];
            s_strip[i] = (c >> 8) | (c << 8);
        }
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_SIZE, y + rows, s_strip);
    }
}

// Centred text with its baseline at y (pixels from the top)
static void text_center(const char *text, const GFXfont *font, uint16_t color, int y)
{
    int16_t x1, y1;
    uint16_t w, h;
    s_canvas->setFont(font);
    s_canvas->setTextColor(color);
    s_canvas->getTextBounds(text, 0, y, &x1, &y1, &w, &h);
    s_canvas->setCursor(LCD_RADIUS - (int)w / 2 - x1, y);
    s_canvas->print(text);
}

// Ring along the edge: track, and the first part of it in the accent colour (percent 0..100)
static void ring(int percent, uint16_t color)
{
    for (int r = LCD_RADIUS - 14; r < LCD_RADIUS - 4; r++) {
        s_canvas->drawCircle(LCD_RADIUS, LCD_RADIUS, r, COLOR_TRACK);
    }
    if (percent <= 0) {
        return;
    }
    int steps = percent * 360 / 100;
    for (int a = 0; a < steps; a++) {
        float rad = (a - 90) * DEG_TO_RAD;
        int x = LCD_RADIUS + (int)((LCD_RADIUS - 9) * cosf(rad));
        int y = LCD_RADIUS + (int)((LCD_RADIUS - 9) * sinf(rad));
        s_canvas->fillCircle(x, y, 5, color);
    }
}

// Every screen keeps its text inside the circle: lines between y 120 and 300 are at most 290 px wide
static void screen(const char *title1, const char *title2, const char *line1, const char *line2, uint16_t accent,
                   int percent)
{
    if (!s_canvas) {
        return;
    }
    s_canvas->fillScreen(COLOR_BG);
    ring(percent, accent);
    text_center("Schaltknueppel", &FreeSans12pt7b, COLOR_MUTED, 130);
    text_center(title1, &FreeSansBold18pt7b, COLOR_TEXT, 190);
    if (title2[0]) {
        text_center(title2, &FreeSansBold18pt7b, COLOR_TEXT, 232);
    }
    text_center(line1, &FreeSans12pt7b, COLOR_MUTED, 282);
    text_center(line2, &FreeSans9pt7b, accent, 316);
    display_show();
}

/********************* Bluetooth *********************/

static BLEServer *s_server;
static BLECharacteristic *s_status;
static volatile bool s_connected = false;
static MessageBufferHandle_t s_frames;      // upload frames, from the Bluetooth task to the update task
static volatile uint32_t s_screen_dirty = 1;

// Notifications go out in parts no larger than the phone accepts
static size_t notify_max()
{
    uint16_t mtu = s_server ? s_server->getPeerMTU(s_server->getConnId()) : 23;
    return mtu > 3 ? mtu - 3 : 20;
}

static void notify(const uint8_t *data, size_t len)
{
    if (!s_connected) {
        return;
    }
    s_status->setValue((uint8_t *)data, len);
    s_status->notify();
    delay(5);
}

static void status_send(uint8_t code, uint32_t value)
{
    uint8_t frame[5] = { code, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24) };
    notify(frame, sizeof(frame));
}

// The app asks for the board's state after connecting; the installer only has its version
static void config_answer()
{
    String json = String("{\"ok\":true,\"installer\":true,\"name\":\"") + DEVICE_NAME + "\",\"fw\":{\"version\":\"" +
                  INSTALLER_VERSION + "\",\"date\":\"" + __DATE__ + "\",\"time\":\"\"}}";
    size_t part_max = notify_max() - 1;
    uint8_t frame[256];
    part_max = min(part_max, sizeof(frame) - 1);
    size_t sent = 0;
    do {
        size_t part = min(part_max, json.length() - sent);
        frame[0] = sent + part == json.length() ? CONFIG_END : CONFIG_PART;
        memcpy(frame + 1, json.c_str() + sent, part);
        notify(frame, part + 1);
        sent += part;
    } while (sent < json.length());
}

static String s_line;
static volatile bool s_config_asked = false;

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override
    {
        const uint8_t *data = c->getData();
        size_t len = c->getLength();
        for (size_t i = 0; i < len; i++) {
            char ch = (char)data[i];
            if (ch == '\n' || ch == '\r') {
                if (s_line.startsWith("{\"config\"")) {
                    s_config_asked = true;      // answered from the loop, not from the Bluetooth task
                }
                s_line = "";                    // time, notifications, music: not for the installer
            } else if (s_line.length() < 200) {
                s_line += ch;
            }
        }
    }
};

class FileCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override
    {
        // Copied away at once; when the buffer is full the frame is lost and the app resends from the offset
        xMessageBufferSend(s_frames, c->getData(), c->getLength(), 0);
    }
};

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *server) override
    {
        s_connected = true;
        s_screen_dirty = 1;
        Serial.println("phone connected");
    }
    void onDisconnect(BLEServer *server) override
    {
        s_connected = false;
        s_screen_dirty = 1;
        Serial.println("phone disconnected");
        BLEDevice::startAdvertising();
    }
};

static void bluetooth_begin()
{
    BLEDevice::init(DEVICE_NAME);
    BLEDevice::setMTU(247);
    s_server = BLEDevice::createServer();
    s_server->setCallbacks(new ServerCallbacks());
    BLEService *service = s_server->createService(UUID_SERVICE);
    BLECharacteristic *rx = service->createCharacteristic(UUID_RX, BLECharacteristic::PROPERTY_WRITE |
                                                                   BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(new RxCallbacks());
    BLECharacteristic *file = service->createCharacteristic(UUID_FILE, BLECharacteristic::PROPERTY_WRITE |
                                                                       BLECharacteristic::PROPERTY_WRITE_NR);
    file->setCallbacks(new FileCallbacks());
    s_status = service->createCharacteristic(UUID_STATUS, BLECharacteristic::PROPERTY_NOTIFY);
    service->start();
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(UUID_SERVICE);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
}

/********************* Firmware update *********************/

enum State { WAITING, RECEIVING, DONE, FAILED, CLOSED };
static volatile State s_state = WAITING;
static volatile uint32_t s_size = 0, s_received = 0;
static const char *s_fail_text = "";
static const esp_partition_t *s_partition = NULL;
static esp_ota_handle_t s_ota = 0;
static uint32_t s_crc = 0;
static uint32_t s_last_frame_ms = 0;
static bool s_offset_reported = false;

static uint32_t rd32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void transfer_close()
{
    if (s_partition) {
        esp_ota_abort(s_ota);
        s_partition = NULL;
    }
}

static void fail(uint8_t code, const char *text)
{
    Serial.printf("update failed: %s (status %u at %u of %u bytes)\n", text, code, (unsigned)s_received, (unsigned)s_size);
    transfer_close();
    status_send(code, s_received);
    s_fail_text = text;
    s_state = FAILED;
    s_screen_dirty = 1;
}

static void ota_start(const uint8_t *f, size_t len)
{
    transfer_close();
    s_received = 0;
    s_crc = 0;
    s_offset_reported = false;
    if (millis() > WINDOW_MS) {
        s_state = CLOSED;
        s_screen_dirty = 1;
        status_send(ST_FIRMWARE, 0);
        return;
    }
    s_size = len >= 5 ? rd32(f + 1) : 0;
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        fail(ST_FIRMWARE, "Partitionstabelle falsch");
        return;
    }
    if (s_size == 0 || s_size > part->size) {
        fail(ST_TOO_BIG, "Datei zu gross");
        return;
    }
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &s_ota) != ESP_OK) {
        fail(ST_WRITE, "Speicher-Fehler");
        return;
    }
    s_partition = part;
    s_state = RECEIVING;
    s_screen_dirty = 1;
    Serial.printf("receiving %u bytes into %s\n", (unsigned)s_size, part->label);
    status_send(ST_OK, 0);
}

static void ota_data(const uint8_t *f, size_t len)
{
    if (!s_partition) {
        status_send(ST_NO_TRANSFER, 0);
        return;
    }
    if (len < 6) {
        return;
    }
    uint32_t offset = rd32(f + 1);
    size_t payload = len - 5;
    if (offset != s_received) {
        if (!s_offset_reported) {
            s_offset_reported = true;           // once; the app continues from the offset it is told
            status_send(ST_OFFSET, s_received);
        }
        return;
    }
    s_offset_reported = false;
    if (s_received + payload > s_size) {
        fail(ST_TOO_BIG, "Datei zu gross");
        return;
    }
    if (esp_ota_write(s_ota, f + 5, payload) != ESP_OK) {
        fail(ST_WRITE, "Speicher-Fehler");
        return;
    }
    s_crc = esp_rom_crc32_le(s_crc, f + 5, payload);
    s_received += payload;
}

static void ota_end(const uint8_t *f, size_t len)
{
    if (!s_partition) {
        status_send(ST_NO_TRANSFER, 0);
        return;
    }
    if (len >= 5 && s_received < s_size) {
        status_send(ST_OFFSET, s_received);     // the END overtook resent frames: ask for the rest
        return;
    }
    if (len < 5 || s_received != s_size || rd32(f + 1) != s_crc) {
        fail(ST_CRC, "Uebertragung fehlerhaft");
        return;
    }
    const esp_partition_t *part = s_partition;
    s_partition = NULL;
    if (esp_ota_end(s_ota) != ESP_OK) {         // checks header, segments, checksum and hash
        s_partition = NULL;
        fail(ST_FIRMWARE, "Keine gueltige Firmware");
        return;
    }
    esp_app_desc_t incoming;
    if (esp_ota_get_partition_description(part, &incoming) != ESP_OK ||
        strncmp(incoming.project_name, PROJECT_NAME, sizeof(incoming.project_name)) != 0) {
        fail(ST_FIRMWARE, "Nicht die Schaltknueppel-Firmware");
        return;
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        fail(ST_FIRMWARE, "Speicher-Fehler");
        return;
    }
    Serial.printf("firmware %s stored in %s, restarting\n", incoming.version, part->label);
    status_send(ST_DONE, s_received);
    s_state = DONE;
    s_screen_dirty = 1;
}

static void update_task(void *arg)
{
    static uint8_t frame[520];
    while (true) {
        size_t len = xMessageBufferReceive(s_frames, frame, sizeof(frame), pdMS_TO_TICKS(500));
        if (len == 0) {
            if (s_partition && millis() - s_last_frame_ms > FRAME_TIMEOUT_MS) {
                fail(ST_TIMEOUT, "Zeit abgelaufen");
            }
            continue;
        }
        s_last_frame_ms = millis();
        switch (frame[0]) {
        case FRAME_OTA_START: ota_start(frame, len); break;
        case FRAME_DATA: ota_data(frame, len); break;
        case FRAME_END: ota_end(frame, len); break;
        case FRAME_ABORT: transfer_close(); s_state = WAITING; s_screen_dirty = 1; break;
        case FRAME_START: status_send(ST_NO_CARD, 0); break;     // pictures need the real firmware
        case FRAME_LIST: {
            uint8_t end[5] = { ST_LIST_END, 0, 0, 0, 0 };
            notify(end, sizeof(end));
            break;
        }
        default: break;
        }
    }
}

/********************* Main *********************/

static void screen_update()
{
    static uint32_t shown_percent = 0;
    switch (s_state) {
    case WAITING:
        if (millis() > WINDOW_MS) {
            s_state = CLOSED;
            screen_update();
            return;
        }
        if (s_connected) {
            screen("Verbunden", "", "Update in der App starten", DEVICE_NAME, COLOR_ACCENT, 0);
        } else {
            screen("Bereit zum", "Update", "In der App verbinden", DEVICE_NAME, COLOR_ACCENT, 0);
        }
        break;
    case RECEIVING: {
        uint32_t percent = s_size ? (uint64_t)s_received * 100 / s_size : 0;
        char line[32];
        snprintf(line, sizeof(line), "%u %%  -  Board anlassen", (unsigned)percent);
        screen("Firmware", "kommt", line, "Bitte warten", COLOR_ACCENT, percent);
        shown_percent = percent;
        break;
    }
    case DONE:
        screen("Fertig", "", "Board startet neu", "Weiter in der App", COLOR_OK, 100);
        break;
    case FAILED:
        screen("Fehler", "", s_fail_text, "In der App nochmal versuchen", COLOR_ERROR, 0);
        break;
    case CLOSED:
        screen("Zeit", "abgelaufen", "Board neu starten (Reset)", "dann innerhalb 15 min", COLOR_ERROR, 0);
        break;
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println();
    Serial.println("Schaltknueppel Installer " INSTALLER_VERSION);

    void *storage = heap_caps_malloc(64 * 1024, MALLOC_CAP_SPIRAM);
    static StaticMessageBuffer_t buffer_struct;
    s_frames = xMessageBufferCreateStatic(64 * 1024, (uint8_t *)storage, &buffer_struct);

    display_begin();
    bluetooth_begin();
    xTaskCreatePinnedToCore(update_task, "update", 6144, NULL, 5, NULL, 1);
    Serial.println("waiting for the app (Bluetooth name \"" DEVICE_NAME "\")");
}

void loop()
{
    static uint32_t last_draw = 0;
    static uint32_t done_at = 0;

    if (s_config_asked) {
        s_config_asked = false;
        config_answer();
    }
    // Progress is redrawn twice a second, every other change at once
    if (s_screen_dirty || (s_state == RECEIVING && millis() - last_draw > 500) ||
        (s_state == WAITING && millis() > WINDOW_MS)) {
        s_screen_dirty = 0;
        last_draw = millis();
        screen_update();
    }
    static uint32_t last_report = 0;
    if (millis() - last_report > 5000) {        // for the serial monitor: still alive and visible?
        last_report = millis();
        Serial.printf("[%lus] Bluetooth %s, %s, %s\n", millis() / 1000, BLEDevice::getAddress().toString().c_str(),
                      BLEDevice::getAdvertising()->isAdvertising() ? "visible" : "not visible",
                      s_connected ? "app connected" : "waiting for the app");
    }
    if (s_state == DONE) {
        if (!done_at) {
            done_at = millis();
        } else if (millis() - done_at > 1500) {     // the last answer goes out first
            ESP.restart();
        }
    }
    delay(20);
}
