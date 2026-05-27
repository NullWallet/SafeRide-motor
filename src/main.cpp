#include <esp_now.h>
#include <WiFi.h>
#include <Adafruit_MPU6050.h>

#include "comms.hpp"
#include "vars.hpp"

// --- Ignition safety thresholds ---
#define BAC_LIMIT          0.05f      // g/dL — PH legal limit (RA 10586)

// ─────────────────────────────────────────────────────────────────────────────
// Immediate lock: BAC over limit OR we have never heard from the driver helmet.
// Does NOT include helmet-off — that goes through the 30 s countdown instead.
// ─────────────────────────────────────────────────────────────────────────────
bool shouldImmediatelyLock() {
    if (!driverState.received)            return true;   // no data yet
    if (driverState.bacLevel >= BAC_LIMIT) return true;   // drunk
    return false;
}

// Returns true if at least one helmet is currently off (triggers countdown).
bool isHelmetOff() {
    if (!driverState.helmetOn)                          return true;
    if (passengerState.received && !passengerState.helmetOn) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
void silenceSpeaker() {
    if (!crashDetected) {               // never interrupt crash alert
        countdownBeepOn = false;
        digitalWrite(SPEAKER, LOW);
    }
}

void notifyBLE(const char* msg) {
    if (deviceConnected) {
        pCharacteristic->setValue(msg);
        pCharacteristic->notify();
    }
}

void lockIgnition(const char* reason) {
    Serial.printf("Ignition LOCKED — %s\n", reason);
    setRelay(true);
}

void unlockIgnition(const char* reason) {
    Serial.printf("Ignition UNLOCKED — %s\n", reason);
    setRelay(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Countdown helpers
// ─────────────────────────────────────────────────────────────────────────────
void startCountdown() {
    countdownActive   = true;
    countdownStartMs  = millis();
    lastBeepSecond    = -1;
    countdownBeepOn   = false;
    Serial.println(">>> Helmet removed! 30-second countdown started.");
}

void cancelCountdown() {
    countdownActive = false;
    silenceSpeaker();
    Serial.println("Countdown CANCELLED — helmet restored.");
}

// Called every loop() tick while countdownActive == true.
// Returns true when the countdown has finished and ignition must be cut.
bool tickCountdown() {
    unsigned long elapsed = millis() - countdownStartMs;
    int elapsedSec = (int)(elapsed / 1000);

    // ── Phase 1 (0–24 s): one short beep per second ──────────────────────────
    if (elapsed < COUNTDOWN_CONT_MS) {
        if (elapsedSec != lastBeepSecond) {
            lastBeepSecond = elapsedSec;
            Serial.printf("  Countdown: %d / 30 s\n", elapsedSec + 1);

            if (!crashDetected) {
                digitalWrite(SPEAKER, HIGH);
                countdownBeepOn  = true;
                countdownBeepOnMs = millis();
            }
        }
        // Turn the pulse off after BEEP_PULSE_MS
        if (countdownBeepOn && (millis() - countdownBeepOnMs >= BEEP_PULSE_MS)) {
            if (!crashDetected) digitalWrite(SPEAKER, LOW);
            countdownBeepOn = false;
        }
        return false; // not done yet
    }

    // ── Phase 2 (25–30 s): continuous beep ───────────────────────────────────
    if (elapsed < COUNTDOWN_TOTAL_MS) {
        if (!crashDetected) digitalWrite(SPEAKER, HIGH);
        // Log once per second during this phase
        if (elapsedSec != lastBeepSecond) {
            lastBeepSecond = elapsedSec;
            Serial.printf("  FINAL WARNING: %d / 30 s — ignition cut imminent!\n", elapsedSec + 1);
        }
        return false;
    }

    // ── Phase 3 (≥ 30 s): cut ignition ───────────────────────────────────────
    countdownActive = false;
    silenceSpeaker();
    Serial.println(">>> Countdown elapsed — cutting ignition.");
    return true; // caller should lock ignition
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(SPEAKER, OUTPUT);
    pinMode(RELAY, OUTPUT);

    // Default: ignition OFF until we hear from helmets
    setRelay(true);

    speakerTimer.attach_ms(500, alert); // crash-alert ticker (unchanged)

    if (!mpu.begin()) {
        Serial.println("Failed to find MPU6050 chip. Check your wiring!");
        while (1) delay(10);
    }
    mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
    Serial.println("MPU6050 found!");

    WiFi.mode(WIFI_MODE_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(OnDataRecv);

    setupBLE();
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────────────────────
void loop() {

    // ── Crash Detection (unchanged) ──────────────────────────────────────────
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    float magnitude = sqrt(
        a.acceleration.x * a.acceleration.x +
        a.acceleration.y * a.acceleration.y +
        a.acceleration.z * a.acceleration.z
    );

    if (magnitude > CRASH_THRESHOLD) {
        if (millis() - lastCrashTime > CRASH_COOLDOWN_MS) {
            Serial.printf("Crash detected! Force: %.2f m/s²\n", magnitude);
            triggerCrashAlert();
            resetCrashTimer.once_ms(3000, resetCrashFlag);
            lastCrashTime = millis();
        }
    }

    // ── Ignition / Relay Control ─────────────────────────────────────────────

    // 1. Conditions that lock ignition immediately (BAC / no data).
    if (shouldImmediatelyLock()) {
        if (countdownActive) cancelCountdown();
        if (!ignitionDisabled) lockIgnition("BAC/no-data");
        delay(10);
        return;
    }

    // 2. From here we know: driver data received AND BAC is OK.
    bool helmetOff = isHelmetOff();

    if (!helmetOff) {
        // ── Helmets are on ───────────────────────────────────────────────────
        if (countdownActive) cancelCountdown();

        // Re-enable if ignition was cut (by countdown or prior lock)
        if (ignitionDisabled) {
            ignitionCutByCountdown = false;
            unlockIgnition("helmets OK");
        }

    } else {
        // ── At least one helmet is off ───────────────────────────────────────

        // Start countdown only if ignition is currently enabled (motor is running).
        if (!countdownActive && !ignitionDisabled) {
            startCountdown();
        }

        // Tick the countdown. If it returns true, the 30 s elapsed → cut ignition.
        if (countdownActive) {
            if (tickCountdown()) {
                ignitionCutByCountdown = true;
                lockIgnition("30 s helmet countdown elapsed");
            }
        }
    }

    delay(10);
}