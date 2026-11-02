#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <U8g2lib.h>

// --- Display Configuration ---
// SH1107 128x128 I2C configuration for E.C.H.O.S. (0x3C)
U8G2_SH1107_SEEED_128X128_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// --- Accelerometer Configuration ---
Adafruit_LIS3DH lis3dh = Adafruit_LIS3DH();
bool lis3dh_found = false;

// Calibration baselines
float base_y = 0.0f;
float base_z = 0.0f;

// Low-pass filtered gaze offsets
float smooth_offset_x = 0.0f;
float smooth_offset_y = 0.0f;

// --- Capacitive Touch Configuration (D0 / GPIO 1) ---
#define TOUCH_PIN D0
const uint32_t TOUCH_THRESHOLD = 33500; // Calibrated for copper foil tap
bool touch_last_state = false;
unsigned long last_touch_time = 0;
const unsigned long DEBOUNCE_DELAY_MS = 250;

// --- Face Expressions ---
enum FaceState {
    FACE_NEUTRAL,     // Rounded square baseline tracking gaze
    FACE_HAPPY,       // Cheerful smile cheeks
    FACE_ANGRY,       // Angled eyebrows sloping inward
    FACE_SAD,         // Drooping eyebrows sloping outward
    FACE_SLEEPY,      // Half-mast eyelids (replaces circular eyes)
    FACE_SUSPICIOUS,  // Narrow squinted slits
    FACE_COUNT
};

int current_face = FACE_NEUTRAL;

// Procedural blink timer
unsigned long last_blink_time = 0;
unsigned long blink_interval = 4000;
bool is_blinking = false;

// Speech / Mouth animation state
// Set is_speaking to true whenever audio output / TTS is active
bool is_speaking = true; 
int current_mouth_open = 0;

void drawEyes(int ox, int oy, FaceState face) {
    const int eye_w = 34;
    const int eye_h = 34;
    const int corner_r = 7;
    const int left_center_x = 38;
    const int right_center_x = 90;
    const int center_y = 52; // Shifted up slightly to leave comfortable room for mouth

    int lx = left_center_x - (eye_w / 2) + ox;
    int rx = right_center_x - (eye_w / 2) + ox;
    int ey = center_y - (eye_h / 2) + oy;

    if (is_blinking) {
        // Closed eyelid slits
        u8g2.drawRBox(lx, center_y + oy - 2, eye_w, 4, 2);
        u8g2.drawRBox(rx, center_y + oy - 2, eye_w, 4, 2);
        return;
    }

    switch (face) {
        case FACE_NEUTRAL:
            u8g2.drawRBox(lx, ey, eye_w, eye_h, corner_r);
            u8g2.drawRBox(rx, ey, eye_w, eye_h, corner_r);
            break;

        case FACE_HAPPY: {
            u8g2.drawRBox(lx, ey, eye_w, eye_h, corner_r);
            u8g2.drawRBox(rx, ey, eye_w, eye_h, corner_r);
            
            // Mask lower boundary upward to create smile cheeks
            u8g2.setDrawColor(0);
            u8g2.drawDisc(left_center_x + ox, ey + eye_h + 10, 24);
            u8g2.drawDisc(right_center_x + ox, ey + eye_h + 10, 24);
            u8g2.setDrawColor(1);
            break;
        }

        case FACE_ANGRY: {
            u8g2.drawRBox(lx, ey, eye_w, eye_h, corner_r);
            u8g2.drawRBox(rx, ey, eye_w, eye_h, corner_r);

            // Cut angled eyebrows sloping inward down to center
            u8g2.setDrawColor(0);
            u8g2.drawTriangle(lx - 2, ey - 2, 
                              lx + eye_w + 2, ey - 2, 
                              lx + eye_w + 2, ey + 18);
            u8g2.drawTriangle(rx - 2, ey - 2, 
                              rx + eye_w + 2, ey - 2, 
                              rx - 2, ey + 18);
            u8g2.setDrawColor(1);
            break;
        }

        case FACE_SAD: {
            u8g2.drawRBox(lx, ey, eye_w, eye_h, corner_r);
            u8g2.drawRBox(rx, ey, eye_w, eye_h, corner_r);

            // Cut angled eyebrows sloping outward down toward cheeks
            u8g2.setDrawColor(0);
            u8g2.drawTriangle(lx - 2, ey - 2, 
                              lx + eye_w + 2, ey - 2, 
                              lx - 2, ey + 18);
            u8g2.drawTriangle(rx - 2, ey - 2, 
                              rx + eye_w + 2, ey - 2, 
                              rx + eye_w + 2, ey + 18);
            u8g2.setDrawColor(1);
            break;
        }

        case FACE_SLEEPY: {
            // Half-mast squircle eyelids (replaces the circular eyes)
            u8g2.drawRBox(lx, ey, eye_w, eye_h, corner_r);
            u8g2.drawRBox(rx, ey, eye_w, eye_h, corner_r);

            u8g2.setDrawColor(0);
            u8g2.drawBox(lx - 2, ey - 2, eye_w + 4, 14);
            u8g2.drawBox(rx - 2, ey - 2, eye_w + 4, 14);
            u8g2.setDrawColor(1);
            break;
        }

        case FACE_SUSPICIOUS: {
            int narrow_h = 10;
            int sy = center_y - (narrow_h / 2) + (oy / 2);
            u8g2.drawRBox(lx, sy, eye_w, narrow_h, 3);
            u8g2.drawRBox(rx, sy, eye_w, narrow_h, 3);
            break;
        }

        default:
            u8g2.drawRBox(lx, ey, eye_w, eye_h, corner_r);
            u8g2.drawRBox(rx, ey, eye_w, eye_h, corner_r);
            break;
    }
}

void drawMouth(int ox, int oy, int open_amount, FaceState face) {
    const int mouth_center_x = 64 + (ox / 2);
    const int mouth_y = 96 + (oy / 3);
    const int mouth_w = 26;

    if (open_amount <= 2) {
        // Resting / Closed mouth styles based on emotional expression
        if (face == FACE_HAPPY) {
            u8g2.drawRBox(mouth_center_x - 13, mouth_y, mouth_w, 4, 1);
            u8g2.setDrawColor(0);
            u8g2.drawDisc(mouth_center_x, mouth_y - 2, 12);
            u8g2.setDrawColor(1);
        } else if (face == FACE_SAD) {
            u8g2.drawRBox(mouth_center_x - 13, mouth_y, mouth_w, 4, 1);
            u8g2.setDrawColor(0);
            u8g2.drawDisc(mouth_center_x, mouth_y + 5, 12);
            u8g2.setDrawColor(1);
        } else {
            // Standard small neutral slit
            u8g2.drawRBox(mouth_center_x - 13, mouth_y, mouth_w, 3, 1);
        }
    } else {
        // Explicitly clamped dimensions to prevent underflow / overflow
        int h = constrain(open_amount, 4, 18);
        int w = mouth_w - (h / 2); // Scales width proportionally down to 17px
        if (w < 14) w = 14;

        int rx = mouth_center_x - (w / 2);
        int ry = mouth_y - (h / 2);
        int r  = (h >= 6) ? 3 : 1; // Radius must always be less than half height

        u8g2.drawRBox(rx, ry, w, h, r);
    }
}

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // 1. Initialize OLED Display (SH1107 on 0x3C)
    u8g2.begin();
    u8g2.clearBuffer();

    // 2. Initialize LIS3DH Accelerometer
    if (lis3dh.begin(0x18) || lis3dh.begin(0x19) || lis3dh.begin(0x1C)) {
        lis3dh_found = true;
        lis3dh.setRange(LIS3DH_RANGE_2_G);

        sensors_event_t event;
        float sum_y = 0.0f;
        float sum_z = 0.0f;
        for (int i = 0; i < 20; i++) {
            lis3dh.getEvent(&event);
            sum_y += event.acceleration.y;
            sum_z += event.acceleration.z;
            delay(10);
        }
        base_y = sum_y / 20.0f;
        base_z = sum_z / 20.0f;
    }

    Serial.println("E.C.H.O.S. Display Engine with Dynamic Mouth & Face Cycle Active");
}

void loop() {
    unsigned long now = millis();

    // --- 1. Capacitive Touch State Check (Cycle Faces) ---
    uint32_t touch_val = (uint32_t)touchRead(TOUCH_PIN);
    bool touch_active = (touch_val > TOUCH_THRESHOLD);

    if (touch_active && !touch_last_state && (now - last_touch_time > DEBOUNCE_DELAY_MS)) {
        current_face = (current_face + 1) % FACE_COUNT;
        last_touch_time = now;
        Serial.printf("Touch Registered: %u -> Face switched to %d\n", touch_val, current_face);
    }
    touch_last_state = touch_active;

    // --- 2. Natural Blink Handling ---
    if (!is_blinking && (now - last_blink_time > blink_interval)) {
        is_blinking = true;
        last_blink_time = now;
    } else if (is_blinking && (now - last_blink_time > 140)) { // 140ms blink closure
        is_blinking = false;
        last_blink_time = now;
        blink_interval = random(2500, 7000);
    }

    // --- 3. Speech & Mouth Motion Simulation ---
    // If audio playback or streaming is active, animate procedural visemes
    if (is_speaking) {
        static unsigned long last_mouth_change = 0;
        static int target_open = 0;

        if (now - last_mouth_change > 70) { // Update phoneme aperture every 70ms
            target_open = random(0, 18);
            last_mouth_change = now;
        }
        // Smooth opening/closing motion
        current_mouth_open = (current_mouth_open * 2 + target_open) / 3;
    } else {
        current_mouth_open = 0;
    }

    // --- 4. Accelerometer Tilt & Gaze Tracking ---
    if (lis3dh_found) {
        sensors_event_t event;
        lis3dh.getEvent(&event);

        // Hardware orientation: Z governs horizontal, inverted Y governs vertical
        float axis_horizontal = -(event.acceleration.z - base_z);
        float axis_vertical   = -(event.acceleration.y - base_y);

        float target_x = constrain(axis_horizontal * 4.5f, -20.0f, 20.0f);
        float target_y = constrain(axis_vertical   * 2.5f, -10.0f, 10.0f);

        // Low-pass exponential smoothing
        smooth_offset_x = (smooth_offset_x * 0.75f) + (target_x * 0.25f);
        smooth_offset_y = (smooth_offset_y * 0.75f) + (target_y * 0.25f);
    }

    int eye_offset_x = (int)smooth_offset_x;
    int eye_offset_y = (int)smooth_offset_y;

    // --- 5. Render Full Face Frame ---
    u8g2.clearBuffer();
    drawEyes(eye_offset_x, eye_offset_y, (FaceState)current_face);
    drawMouth(eye_offset_x, eye_offset_y, current_mouth_open, (FaceState)current_face);
    u8g2.sendBuffer();

    delay(16); // ~60 FPS update cycle
}