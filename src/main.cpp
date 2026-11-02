#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "esp_camera.h"

// --- Wi-Fi Credentials ---
const char* ssid = "home-automation";
const char* password = "darksidecookies";

// --- OLED Configuration (SH1107 128x128 I2C on 0x3C) ---
U8G2_SH1107_SEEED_128X128_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// --- Web Server on Port 80 ---
WebServer server(80);

// --- Seeed Studio XIAO ESP32-S3 Sense Camera Pinout (OV2640 / OV3660) ---
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

bool camera_initialized = false;

// --- OLED Status Screens ---
void drawConnectingScreen(int dots) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(16, 25, "E.C.H.O.S.");
    u8g2.drawHLine(0, 32, 128);

    u8g2.drawStr(10, 55, "Connecting WiFi");
    
    char dot_str[5] = "";
    for (int i = 0; i < dots; i++) {
        dot_str[i] = '.';
    }
    dot_str[dots] = '\0';
    u8g2.drawStr(10, 72, dot_str);

    u8g2.drawStr(10, 105, "SSID:");
    u8g2.drawStr(10, 120, ssid);
    u8g2.sendBuffer();
}

void drawCameraServerScreen(const String& ip_str) {
    u8g2.clearBuffer();

    // 1. Title Bar
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(14, 18, "CAMERA ONLINE");
    u8g2.drawHLine(0, 24, 128);

    // 2. URL Section
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 40, "Web Browser URL:");

    // Use a compact 5x7 font for the full http:// link (21 chars * 5px = 105px wide, fits easily)
    u8g2.setFont(u8g2_font_5x7_tr);
    String full_url = "http://" + ip_str;
    u8g2.drawStr(0, 54, full_url.c_str());

    u8g2.drawHLine(0, 66, 128);

    // 3. Endpoints Info
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 82, "Endpoints:");
    u8g2.drawStr(0, 96, "/        -> Web UI");
    u8g2.drawStr(0, 110, "/capture -> Raw JPG");
    u8g2.drawStr(0, 124, "Status: Ready");

    u8g2.sendBuffer();
}

void drawErrorScreen(const char* msg) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(15, 30, "SYSTEM ERROR");
    u8g2.drawHLine(0, 38, 128);
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(10, 65, msg);
    u8g2.sendBuffer();
}

// --- Camera Setup Function ---
bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    if (psramFound()) {
        config.frame_size = FRAMESIZE_VGA; // 640x480 for quick transmission
        config.jpeg_quality = 12;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_QVGA; // 320x240 fallback
        config.jpeg_quality = 16;
        config.fb_count = 1;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera initialization failed with code 0x%x\n", err);
        return false;
    }

    // Apply basic sensor orientation adjustments
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_vflip(s, 0); // Flip vertically if mounted upside down
        s->set_hmirror(s, 1);
    }

    return true;
}

// --- HTTP Handlers ---
void handleCapture() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        server.send(500, "text/plain", "Failed to capture image from camera buffer");
        return;
    }

    WiFiClient client = server.client();
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: image/jpeg\r\n";
    response += "Content-Disposition: inline; filename=capture.jpg\r\n";
    response += "Content-Length: " + String(fb->len) + "\r\n";
    response += "Connection: close\r\n\r\n";
    client.print(response);

    client.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>E.C.H.O.S. Vision</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #121212; color: #fff; text-align: center; margin: 0; padding: 20px; }
        h1 { color: #00e676; margin-bottom: 8px; }
        p { color: #888; margin-top: 0; }
        .frame-box { max-width: 640px; margin: 0 auto; border: 2px solid #333; border-radius: 8px; overflow: hidden; background: #000; min-height: 240px; display: flex; align-items: center; justify-content: center; }
        img { width: 100%; height: auto; display: block; }
        .btn { background: #00e676; color: #000; font-weight: bold; border: none; padding: 12px 28px; margin-top: 20px; font-size: 16px; border-radius: 6px; cursor: pointer; transition: 0.2s; }
        .btn:hover { background: #00b359; }
    </style>
</head>
<body>
    <h1>E.C.H.O.S. Vision Capture</h1>
    <p>Live snapshot from Seeed Studio XIAO ESP32-S3 Sense</p>
    <div class="frame-box">
        <img id="photo" src="/capture" alt="Captured Frame" />
    </div>
    <br>
    <button class="btn" onclick="refreshPhoto()">Capture New Snapshot</button>

    <script>
        function refreshPhoto() {
            var img = document.getElementById("photo");
            img.src = "/capture?t=" + new Date().getTime();
        }
    </script>
</body>
</html>
)rawliteral";

    server.send(200, "text/html", html);
}

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // 1. Initialize Display
    u8g2.begin();
    u8g2.clearBuffer();

    // 2. Initialize Camera
    if (!initCamera()) {
        drawErrorScreen("Camera Init Failed!");
        while (1) { delay(1000); }
    }
    camera_initialized = true;

    // 3. Connect to Wi-Fi with animated status on screen
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int dot_counter = 1;
    while (WiFi.status() != WL_CONNECTED) {
        drawConnectingScreen(dot_counter);
        dot_counter = (dot_counter % 4) + 1;
        delay(400);
        Serial.print(".");
    }

    Serial.println("\nWiFi Connected!");
    String local_ip = WiFi.localIP().toString();
    Serial.print("Local IP: ");
    Serial.println(local_ip);

    // 4. Register HTTP Routes & Start Server
    server.on("/", HTTP_GET, handleRoot);
    server.on("/capture", HTTP_GET, handleCapture);
    server.begin();

    // 5. Draw IP and Endpoints on OLED
    drawCameraServerScreen(local_ip);
}

void loop() {
    server.handleClient();
}