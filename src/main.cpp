#include <esp_now.h>
#include <WiFi.h>
#include <Adafruit_MPU6050.h>

#include "comms.hpp"
#include "vars.hpp"

// --- Ignition safety thresholds ---
#define BAC_LIMIT 0.1f           // g/dL — PH legal limit (RA 10586) 0.05%
#define HELMET_TIMEOUT_MS 10000   // ms to wait for a helmet packet before locking

// Returns true if ignition should be DISABLED
bool shouldDisableIgnition() {
    // Lock if we haven't heard from the driver helmet at all yet
    if (!driverState.received) return true;

    // Lock if driver helmet is off
    if (!driverState.helmetOn) return true;

    // Lock if BAC is over the limit
    if (driverState.bacLevel >= BAC_LIMIT) return true;

    // Lock if passenger helmet data was received but helmet is off
    // (only enforces passenger check once we've heard from them)
    if (passengerState.received && !passengerState.helmetOn) return true;

    return false;
}

void setup() {
    Serial.begin(115200);

    pinMode(SPEAKER, OUTPUT);
    pinMode(RELAY, OUTPUT);

    // Default: ignition OFF until we hear from helmets
    setRelay(false);

    speakerTimer.attach_ms(500, alert);

    if (!mpu.begin()) {
        Serial.println("Failed to find MPU6050 chip. Check your Wiring!");
        while (1) delay(10);
    }
    mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
    Serial.println("MPU6050 Found!");

    WiFi.mode(WIFI_MODE_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(OnDataRecv);

    setupBLE();
}

void loop() {
    // --- Crash Detection ---
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    float x = a.acceleration.x;
    float y = a.acceleration.y;
    float z = a.acceleration.z;
    float magnitude = sqrt((x * x) + (y * y) + (z * z));

    if (magnitude > CRASH_THRESHOLD) {
        if (millis() - lastCrashTime > CRASH_COOLDOWN_MS) {
            Serial.println("Crash Detected!");
            Serial.print("Impact Force (m/s^2): ");
            Serial.println(magnitude);

            triggerCrashAlert();
            resetCrashTimer.once_ms(3000, resetCrashFlag);
            lastCrashTime = millis();
        }
    }

    // --- Ignition / Relay Control ---
    bool disable = shouldDisableIgnition();

    Serial.print("Ignition status: ");;
    Serial.println(disable ? "DISABLED" : "ENABLED");

    if (disable != ignitionDisabled) {   // only call setRelay on state change
        setRelay(disable);

        if (disable) {
            Serial.println("Ignition LOCKED.");
            if (deviceConnected) {
                pCharacteristic->setValue("LOCK");
                pCharacteristic->notify();
            }
        } else {
            Serial.println("Ignition UNLOCKED.");
            if (deviceConnected) {
                pCharacteristic->setValue("UNLOCK");
                pCharacteristic->notify();
            }
        }
    }

    delay(10);
}