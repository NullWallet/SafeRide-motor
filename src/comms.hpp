#pragma once
#include <cstring>
#include <vars.hpp>

typedef unsigned char uint8_t;

enum HelmetTypes
{
    Driver,
    Passenger
};

// MUST match SafeRide-Driver/src/var.hpp exactly (byte-for-byte) for ESP-NOW.
struct DriverData
{
    HelmetTypes helmetType;
    bool helmetOn;
    bool isSober;
};

// Per-helmet state tracked on the motor side.
struct HelmetState
{
    bool received = false;
    bool helmetOn = false;
    bool isSober = false;
    bool everOn = false; // sticky: true once helmetOn was seen as true at least once
    unsigned long lastUpdateMs = 0;
};

HelmetState driverState;
HelmetState passengerState;

// Add a new struct for BAC data sent from helmet → motor
struct BacData
{
    float bac;
};

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    // ── Existing DriverData handler ───────────────────────────────────────
    if (len == sizeof(DriverData))
    {
        DriverData incoming;
        memcpy(&incoming, incomingData, sizeof(incoming));

        if (incoming.helmetType == HelmetTypes::Driver)
        {
            driverState.received = true;
            driverState.helmetOn = incoming.helmetOn;
            driverState.isSober = incoming.isSober;
            driverState.lastUpdateMs = millis();
            if (incoming.helmetOn)
                driverState.everOn = true;

            notifyBLE(incoming.helmetOn ? "HELMET:DRIVER:1" : "HELMET:DRIVER:0");
            notifyBLE(incoming.isSober ? "SOBER:1" : "SOBER:0");
        }
        else if (incoming.helmetType == HelmetTypes::Passenger)
        {
            passengerState.received = true;
            passengerState.helmetOn = incoming.helmetOn;
            passengerState.lastUpdateMs = millis();
            if (incoming.helmetOn)
                passengerState.everOn = true;

            notifyBLE(incoming.helmetOn ? "HELMET:PASSENGER:1" : "HELMET:PASSENGER:0");
        }
        return;
    }

    // ── BAC result from driver helmet ─────────────────────────────────────
    if (len == sizeof(BacData))
    {
        BacData bac;
        memcpy(&bac, incomingData, sizeof(bac));
        Serial.printf("[ESP-NOW] BAC received: %.4f\n", bac.bac);

        // Forward to app as "BAC:0.0450"
        char msg[32];
        snprintf(msg, sizeof(msg), "BAC:%.4f", bac.bac);
        notifyBLE(msg);
        return;
    }

    Serial.printf("WARNING: Unknown packet size %d\n", len);
}