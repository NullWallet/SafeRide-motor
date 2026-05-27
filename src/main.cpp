#include <esp_now.h>
#include <WiFi.h>
#include <Adafruit_MPU6050.h>

#include "comms.hpp"
#include "vars.hpp"

const unsigned long SIGNAL_TIMEOUT_MS = 15000UL;

// ─────────────────────────────────────────────────────────────────────────────
// Ignition state predicates
// ─────────────────────────────────────────────────────────────────────────────

// Immediate lock: no driver data yet, or driver is not sober.
// Helmet-off is handled separately (countdown vs. silent lock).
bool shouldImmediatelyLock()
{
    if (!driverState.received)
        return true; // no data yet
    if (!driverState.isSober)
        return true; // not sober (decided on helmet side)
    return false;
}

// Are all required helmets currently on?
//   - Driver helmet must be on.
//   - Passenger helmet only matters if a passenger is present (data received).
bool allHelmetsOn()
{
    if (!driverState.helmetOn)
        return false;
    if (passengerState.received && !passengerState.helmetOn)
        return false;
    return true;
}

// Did a helmet transition from on → off during this session?
// Only true if `everOn` has latched AND the helmet is now off.
// This is what gates the 30 s countdown.
bool helmetCameOffDuringRide()
{
    if (driverState.everOn && !driverState.helmetOn)
        return true;
    if (passengerState.everOn && !passengerState.helmetOn)
        return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
void silenceSpeaker()
{
    if (!crashDetected)
    { // never interrupt crash alert
        countdownBeepOn = false;
        digitalWrite(SPEAKER, LOW);
    }
}

void lockIgnition(const char *reason)
{
    Serial.printf("Ignition LOCKED — %s\n", reason);
    setRelay(true);
}

void unlockIgnition(const char *reason)
{
    Serial.printf("Ignition UNLOCKED — %s\n", reason);
    setRelay(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Countdown helpers
// ─────────────────────────────────────────────────────────────────────────────
void startCountdown()
{
    countdownActive = true;
    countdownStartMs = millis();
    lastBeepSecond = -1;
    countdownBeepOn = false;
    Serial.println(">>> Helmet removed! 30-second countdown started.");
}

void cancelCountdown()
{
    countdownActive = false;
    silenceSpeaker();
    Serial.println("Countdown CANCELLED — helmet restored.");
}

// Called every loop() tick while countdownActive == true.
// Returns true when the countdown has finished and ignition must be cut.
bool tickCountdown()
{
    unsigned long elapsed = millis() - countdownStartMs;
    int elapsedSec = (int)(elapsed / 1000);

    // ── Phase 1 (0–24 s): one short beep per second ──────────────────────────
    if (elapsed < COUNTDOWN_CONT_MS)
    {
        if (elapsedSec != lastBeepSecond)
        {
            lastBeepSecond = elapsedSec;
            Serial.printf("  Countdown: %d / 30 s\n", elapsedSec + 1);

            if (!crashDetected)
            {
                digitalWrite(SPEAKER, HIGH);
                countdownBeepOn = true;
                countdownBeepOnMs = millis();
            }
        }
        // Turn the pulse off after BEEP_PULSE_MS
        if (countdownBeepOn && (millis() - countdownBeepOnMs >= BEEP_PULSE_MS))
        {
            if (!crashDetected)
                digitalWrite(SPEAKER, LOW);
            countdownBeepOn = false;
        }
        return false; // not done yet
    }

    // ── Phase 2 (25–30 s): continuous beep ───────────────────────────────────
    if (elapsed < COUNTDOWN_TOTAL_MS)
    {
        if (!crashDetected)
            digitalWrite(SPEAKER, HIGH);
        if (elapsedSec != lastBeepSecond)
        {
            lastBeepSecond = elapsedSec;
            Serial.printf("  FINAL WARNING: %d / 30 s — ignition cut imminent!\n", elapsedSec + 1);
        }
        return false;
    }

    // ── Phase 3 (≥ 30 s): cut ignition ───────────────────────────────────────
    countdownActive = false;
    silenceSpeaker();
    Serial.println(">>> Countdown elapsed — cutting ignition.");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);

    pinMode(SPEAKER, OUTPUT);
    pinMode(RELAY, OUTPUT);

    // Default: ignition OFF until we hear from helmets
    setRelay(true);

    // speakerTimer.attach_ms(500, alert); // crash-alert ticker

    if (!mpu.begin())
    {
        Serial.println("Failed to find MPU6050 chip. Check your wiring!");
        while (1)
            delay(10);
    }
    mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
    Serial.println("MPU6050 found!");

    WiFi.mode(WIFI_MODE_STA);
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_send_cb([](const uint8_t *mac, esp_now_send_status_t status)
                             { Serial.printf("[ESP-NOW TX] %02X:%02X:%02X:%02X:%02X:%02X -> %s\n",
                                             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                                             status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL"); });
    esp_now_register_recv_cb(OnDataRecv);

    setupBLE();
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────────────────────
void loop()
{
    // Check Driver signal
    if (driverState.received && (millis() - driverState.lastUpdateMs > SIGNAL_TIMEOUT_MS))
    {
        Serial.println("WARNING: Driver helmet signal lost!");
        driverState.received = false;
        driverState.helmetOn = false;
        notifyBLE("HELMET:DRIVER:0"); // ← tell the app
        notifyBLE("SOBER:0");         // ← no data = treat as not sober
    }

    // Check Passenger signal
    if (passengerState.received && (millis() - passengerState.lastUpdateMs > SIGNAL_TIMEOUT_MS))
    {
        Serial.println("Passenger left or signal lost. Clearing passenger state.");
        passengerState.received = false;
        passengerState.helmetOn = false;
        passengerState.everOn = false;
        notifyBLE("HELMET:PASSENGER:0"); // ← tell the app
    }
    // ── Crash Detection ──────────────────────────────────────────────────────
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    float magnitude = sqrt(
        a.acceleration.x * a.acceleration.x +
        a.acceleration.y * a.acceleration.y +
        a.acceleration.z * a.acceleration.z);

    if (magnitude > CRASH_THRESHOLD)
    {
        if (millis() - lastCrashTime > CRASH_COOLDOWN_MS)
        {
            Serial.printf("Crash detected! Force: %.2f m/s²\n", magnitude);
            triggerCrashAlert();
            resetCrashTimer.once_ms(3000, resetCrashFlag);
            lastCrashTime = millis();
        }
    }

    // ── Ignition / Relay Control ─────────────────────────────────────────────

    // 1. Immediate lock: no driver data, or driver not sober.
    if (shouldImmediatelyLock())
    {
        if (countdownActive)
            cancelCountdown();
        if (!ignitionDisabled)
            lockIgnition("not sober / no data");
        delay(10);
        return;
    }

    // 2. Driver data received and sober — decide based on helmet state.
    if (allHelmetsOn())
    {
        // ── Helmets on → unlock ──────────────────────────────────────────────
        if (countdownActive)
            cancelCountdown();
        if (ignitionDisabled)
        {
            ignitionCutByCountdown = false;
            unlockIgnition("helmets OK");
        }
    }
    else if (helmetCameOffDuringRide())
    {
        // ── Helmet came off mid-ride → 30 s countdown ────────────────────────
        if (!countdownActive && !ignitionDisabled)
        {
            startCountdown();
        }
        if (countdownActive && tickCountdown())
        {
            ignitionCutByCountdown = true;
            lockIgnition("30 s helmet countdown elapsed");
        }
    }
    else
    {
        // ── Initial state: helmet has not been worn yet → stay locked, silent ─
        if (countdownActive)
            cancelCountdown();
        if (!ignitionDisabled)
            lockIgnition("helmet not yet worn");
    }

    delay(10);
}