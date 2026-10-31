#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_Si7021.h>
#include <Adafruit_LIS3MDL.h>
#include "driver/i2s.h"

// --- Pin Definitions ---
#define PIN_I2C_SDA      5   // D4
#define PIN_I2C_SCL      6   // D5
#define PIN_TOUCH        1   // D0 (Touch 1)

#define I2S_BCLK         44  // D7
#define I2S_LRC          7   // D8
#define I2S_DIN          8   // D9

// --- Peripherals ---
// SH1107 128x128 I2C Display
U8G2_SH1107_128X128_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
Adafruit_Si7021 si7021;
Adafruit_LIS3MDL lis3mdl;

bool si7021_found = false;
bool lis3mdl_found = false;

// --- I2S Tone Generator Setup ---
void setup_i2s() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRC,
        .data_out_num = I2S_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
}

void play_beep(float freq, int duration_ms) {
    const int sample_rate = 16000;
    int samples = (sample_rate * duration_ms) / 1000;
    int16_t buffer[128];
    size_t bytes_written;

    for (int i = 0; i < samples; i += 64) {
        int chunk = min(64, samples - i);
        for (int j = 0; j < chunk; j++) {
            int16_t val = (int16_t)(sin((2.0 * PI * freq * (i + j)) / sample_rate) * 6000);
            buffer[j * 2] = val;     // Left
            buffer[j * 2 + 1] = val; // Right
        }
        i2s_write(I2S_NUM_0, buffer, chunk * 4, &bytes_written, portMAX_DELAY);
    }
}

void scan_i2c() {
    Serial.println("\n--- Starting I2C Scan ---");
    byte count = 0;
    for (byte addr = 1; addr < 127; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("Found I2C device at 0x%02X\n", addr);
            count++;
        }
    }
    Serial.printf("Total devices detected: %u\n", count);
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== E.C.H.O.S. Bench Test Initializing ===");

    // 1. Initialize I2C Bus
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
    scan_i2c();

    // 2. Initialize OLED
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(10, 20, "E.C.H.O.S. BOOT");
    u8g2.sendBuffer();

    // 3. Initialize Sensors
    if (si7021.begin()) {
        si7021_found = true;
        Serial.println("[OK] Si7021 Climate Sensor ready.");
    } else {
        Serial.println("[WARN] Si7021 not detected.");
    }

    if (lis3mdl.begin_I2C()) {
        lis3mdl_found = true;
        Serial.println("[OK] LIS3MDL Magnetometer ready.");
    } else {
        Serial.println("[WARN] LIS3MDL not detected.");
    }

    // 4. Initialize I2S & Play Audio Test
    setup_i2s();
    Serial.println("[AUDIO] Playing POST Beep...");
    play_beep(880.0, 150); // High confirmation beep
}

void loop() {
    // Read Touch baseline (ESP32-S3 touchRead returns raw reading; lower value = active touch)
    int touch_val = touchRead(PIN_TOUCH);

    // Read Sensors
    float temp = si7021_found ? si7021.readTemperature() : 0.0;
    float humidity = si7021_found ? si7021.readHumidity() : 0.0;

    sensors_event_t event;
    if (lis3mdl_found) {
        lis3mdl.getEvent(&event);
    }

    // Update Display
    u8g2.clearBuffer();
    
    // Draw Simple Droid Eyes
    int eye_h = (touch_val < 30000) ? 4 : 24; // Blink if tapped
    u8g2.drawRBox(24, 20, 28, eye_h, 4);
    u8g2.drawRBox(76, 20, 28, eye_h, 4);

    // Telemetry Readouts
    u8g2.setFont(u8g2_font_profont11_mf);
    char buf[32];
    snprintf(buf, sizeof(buf), "T: %.1f C  H: %.0f%%", temp, humidity);
    u8g2.drawStr(10, 85, buf);

    snprintf(buf, sizeof(buf), "Touch Raw: %d", touch_val);
    u8g2.drawStr(10, 102, buf);

    if (touch_val < 30000) {
        u8g2.drawStr(10, 118, ">> TOUCH ACTIVE <<");
    } else {
        u8g2.drawStr(10, 118, "Status: Standby");
    }

    u8g2.sendBuffer();

    // Serial Logging
    Serial.printf("Touch: %d | Temp: %.1f C | Hum: %.1f%%\n", touch_val, temp, humidity);

    delay(100);
}