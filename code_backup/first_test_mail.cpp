#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>

// 1. Include Adafruit sensor headers first
#include <Adafruit_Sensor.h>
#include <Adafruit_Si7021.h>
#include <Adafruit_LIS3DH.h>

// 2. Prevent namespace collision with esp32-camera's sensor_t
#define sensor_t camera_sensor_t
#include "esp_camera.h"
#undef sensor_t

#include "driver/i2s.h"

// --- Pin Definitions ---
#define PIN_I2C_SDA      5   // D4
#define PIN_I2C_SCL      6   // D5
#define PIN_TOUCH        1   // D0

#define I2S_BCLK         44  // D7
#define I2S_LRC          7   // D8
#define I2S_DIN          8   // D9

// --- Camera Pinout for XIAO ESP32-S3 Sense ---
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

// --- Peripherals ---
U8G2_SH1107_128X128_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_Si7021 si7021;
Adafruit_LIS3DH lis3dh;

bool si7021_found = false;
bool lis3dh_found = false;
bool camera_found = false;

// Accelerometer Calibration & Smoothing
float base_y = 0.55;
float base_z = 0.14;
float smooth_offset_x = 0.0;
float smooth_offset_y = 0.0;

// --- I2S Audio Functions ---
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

// --- Camera Initialization ---
bool init_camera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;  // 320x240
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&config);
    return (err == ESP_OK);
}

void calibrate_lis3dh() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(12, 50, "CALIBRATING TILT");
    u8g2.drawStr(12, 70, "HOLD DESK STILL");
    u8g2.getU8x8()->x_offset = 2; // Locked display offset
    u8g2.sendBuffer();

    float sum_y = 0.0, sum_z = 0.0;
    const int SAMPLES = 50;

    for (int i = 0; i < SAMPLES; i++) {
        sensors_event_t event;
        lis3dh.getEvent(&event);
        sum_y += event.acceleration.y;
        sum_z += event.acceleration.z;
        delay(20);
    }

    base_y = sum_y / SAMPLES;
    base_z = sum_z / SAMPLES;
}

void setup() {
    Serial.begin(115200);
    delay(1500);

    // 1. I2C Bus Init
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

    // 2. Display Init
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(10, 20, "INITIALIZING...");
    u8g2.getU8x8()->x_offset = 2; // Adjust between 0, 2, 4, or 32
    u8g2.sendBuffer();

    // 3. Audio Init & Boot POST Beep
    setup_i2s();
    play_beep(880.0, 150); // High confirmation beep

    // 4. Sensors
    if (si7021.begin()) {
        si7021_found = true;
    }

    if (lis3dh.begin(0x18) || lis3dh.begin(0x19)) {
        lis3dh_found = true;
        lis3dh.setRange(LIS3DH_RANGE_2_G);
        calibrate_lis3dh();
    }

    // 5. Camera
    camera_found = init_camera();
}

void loop() {
    // 1. Read Touch on Pin D0
    int touch_val = touchRead(PIN_TOUCH);
    const int TOUCH_THRESHOLD = 35000;
    bool is_touched = (touch_val > TOUCH_THRESHOLD);

    // 2. Read Climate Data (Fahrenheit conversion)
    float temp_c = si7021_found ? si7021.readTemperature() : 0.0;
    float temp_f = (temp_c * 1.8) + 32.0;
    float humidity = si7021_found ? si7021.readHumidity() : 0.0;

    // 3. Accelerometer Tilt & Gaze Tracking
    float d_lr = 0.0;
    float d_fb = 0.0;

    if (lis3dh_found) {
        sensors_event_t event;
        lis3dh.getEvent(&event);

        d_lr = event.acceleration.y - base_y;
        d_fb = event.acceleration.z - base_z;

        // Swapped mapping: Z governs horizontal, Y governs vertical
        float axis_horizontal = -(event.acceleration.z - base_z);
        float axis_vertical   =  (event.acceleration.y - base_y);

        float target_x = constrain(axis_horizontal * 4.5f, -22.0f, 22.0f);
        float target_y = constrain(axis_vertical   * 3.0f, -12.0f, 12.0f);

        // Low-pass exponential smoothing
        smooth_offset_x = (smooth_offset_x * 0.75f) + (target_x * 0.25f);
        smooth_offset_y = (smooth_offset_y * 0.75f) + (target_y * 0.25f);
    }

    int eye_offset_x = (int)smooth_offset_x;
    int eye_offset_y = (int)smooth_offset_y;

    // 4. Render to OLED Display
    u8g2.clearBuffer();

    const int eye_w = 28;
    const int eye_center_y = 42 + eye_offset_y;
    const int left_eye_x = 24 + eye_offset_x;
    const int right_eye_x = 76 + eye_offset_x;

    if (is_touched) {
        // Closed horizontal slit on touch
        int slit_h = 3;
        int slit_y = eye_center_y - (slit_h / 2);
        u8g2.drawBox(left_eye_x, slit_y, eye_w, slit_h);
        u8g2.drawBox(right_eye_x, slit_y, eye_w, slit_h);
    } else {
        // Open eyes smoothly gliding with tilt
        int open_h = 26;
        int open_y = eye_center_y - (open_h / 2);
        u8g2.drawRBox(left_eye_x, open_y, eye_w, open_h, 4);
        u8g2.drawRBox(right_eye_x, open_y, eye_w, open_h, 4);
    }

    // Telemetry Text
    u8g2.setFont(u8g2_font_profont11_mf);
    char buf[32];
    snprintf(buf, sizeof(buf), "T: %.1f F  H: %.0f%%", temp_f, humidity);
    u8g2.drawStr(12, 80, buf);

    snprintf(buf, sizeof(buf), "LR:%+.1f  FB:%+.1f", d_lr, d_fb);
    u8g2.drawStr(12, 96, buf);

    snprintf(buf, sizeof(buf), "Cam: %s | T: %d", camera_found ? "OK" : "ERR", touch_val);
    u8g2.drawStr(12, 112, buf);

    u8g2.getU8x8()->x_offset = 2; // Locked display offset
    u8g2.sendBuffer();

    delay(30);
}